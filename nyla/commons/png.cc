#include <cstdint>

#include "nyla/commons/mem.h"
#include "nyla/commons/png.h"
#include "nyla/commons/word.h"

#define STBI_MAX_DIMENSIONS (1 << 24)

namespace nyla::ByteParser::PNGParser
{

namespace
{

const uint8_t DepthScaleTable[9] = {0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01};
const uint8_t PNGSig[8]{137, 80, 78, 71, 13, 10, 26, 10};

auto Error(Instance &self, const char *err) -> bool
{
    NYLA_LOG("%s", err);
    return false;
}

auto Error(Instance &self, const char *err, const char *detail) -> bool
{
    NYLA_LOG(NYLA_SV_FMT " " NYLA_SV_FMT, err, detail);
    return false;
}

INLINE PNGChunk ReadChunkHeader(Instance &self)
{
    PNGChunk c;
    c.length = Read32BE(self);
    c.type = Read32BE(self);
    return c;
}

} // namespace

auto PNGParser::Parse(Instance &self, Scan scan) -> bool
{
    uint8_t palette[1024], pal_img_n = 0;
    uint8_t has_trans = 0, tc[3] = {0};
    uint16_t tc16[3];
    uint32_t ioff = 0, idata_limit = 0, i, pal_len = 0;
    int first = 1, k, interlace = 0, color = 0, is_iphone = 0;

    self.expanded = NULL;
    self.idata = NULL;
    self.out = NULL;

    if (!MemStartsWith(self.m_At, self.m_Left, PNGSig, sizeof(PNGSig)))
        return Error(self, "Invalid PNG signature");

    if (scan == Scan::Type)
        return true;

    for (;;)
    {
        auto c = ReadChunkHeader(self);
        switch (c.type)
        {

        case DWordBE("CgBI"): {
            is_iphone = 1;
            Advance(self, c.length);
            break;
        }

        case DWordBE("IHDR"): {
            int comp, filter;
            if (!first)
                return Error(self, "multiple IHDR", "Corrupt PNG");
            first = 0;
            if (c.length != 13)
                return Error(self, "bad IHDR len", "Corrupt PNG");
            self.img_x = Read32BE(self);
            self.img_y = Read32BE(self);
            if (self.img_y > STBI_MAX_DIMENSIONS)
                return Error(self, "too large", "Very large image (corrupt?)");
            if (self.img_x > STBI_MAX_DIMENSIONS)
                return Error(self, "too large", "Very large image (corrupt?)");

            self.m_Depth = Read(self);
            switch (self.m_Depth)
            {
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
                break;

            default:
                return Error(self, "1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
            }

            color = Read(self);
            if (color > 6)
                return Error(self, "bad ctype", "Corrupt PNG");
            if (color == 3 && self.m_Depth == 16)
                return Error(self, "bad ctype", "Corrupt PNG");
            if (color == 3)
                pal_img_n = 3;
            else if (color & 1)
                return Error(self, "bad ctype", "Corrupt PNG");
            comp = Read(self);
            if (comp)
                return Error(self, "bad comp method", "Corrupt PNG");
            filter = Read(self);
            if (filter)
                return Error(self, "bad filter method", "Corrupt PNG");
            interlace = Read(self);
            if (interlace > 1)
                return Error(self, "bad interlace method", "Corrupt PNG");
            if (!self.img_x || !self.img_y)
                return Error(self, "0-pixel image", "Corrupt PNG");
            if (!pal_img_n)
            {
                self.img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
                if ((1 << 30) / self.img_x / self.img_n < self.img_y)
                    return Error(self, "too large", "Image too large to decode");
            }
            else
            {
                // if paletted, then pal_n is our final components, and
                // img_n is # components to decompress/filter.
                self.img_n = 1;
                if ((1 << 30) / self.img_x / 4 < self.img_y)
                    return Error(self, "too large", "Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
        }

        case DWordBE("PLTE"): {
            if (first)
                return Error(self, "first not IHDR", "Corrupt PNG");
            if (c.length > 256 * 3)
                return Error(self, "invalid PLTE", "Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length)
                return Error(self, "invalid PLTE", "Corrupt PNG");
            for (i = 0; i < pal_len; ++i)
            {
                palette[i * 4 + 0] = Read(self);
                palette[i * 4 + 1] = Read(self);
                palette[i * 4 + 2] = Read(self);
                palette[i * 4 + 3] = 255;
            }
            break;
        }

        case DWordBE("tRNS"): {
            if (first)
                return Error(self, "first not IHDR", "Corrupt PNG");
            if (self.idata)
                return Error(self, "tRNS after IDAT", "Corrupt PNG");
            if (pal_img_n)
            {
                if (scan == Scan::Header)
                {
                    self.img_n = 4;
                    return true;
                }
                if (pal_len == 0)
                    return Error(self, "tRNS before PLTE", "Corrupt PNG");
                if (c.length > pal_len)
                    return Error(self, "bad tRNS len", "Corrupt PNG");
                pal_img_n = 4;
                for (i = 0; i < c.length; ++i)
                    palette[i * 4 + 3] = Read(self);
            }
            else
            {
                if (!(self.img_n & 1))
                    return Error(self, "tRNS with alpha", "Corrupt PNG");
                if (c.length != (uint32_t)self.img_n * 2)
                    return Error(self, "bad tRNS len", "Corrupt PNG");
                has_trans = 1;
                // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
                if (scan == Scan::Header)
                {
                    ++self.img_n;
                    return true;
                }
                if (self.m_Depth == 16)
                {
                    for (k = 0; k < self.img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                        tc16[k] = Read16BE(self);             // copy the values as-is
                }
                else
                {
                    for (k = 0; k < self.img_n && k < 3; ++k)
                    {
                        tc[k] = (unsigned char)(Read16BE(self) & 255) *
                                DepthScaleTable[self.m_Depth]; // non 8-bit images will be larger
                    }
                }
            }
            break;
        }

        case DWordBE("IDAT"): {
            if (first)
                return Error(self, "first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len)
                return Error(self, "no PLTE", "Corrupt PNG");
            if (scan == Scan::Header)
            {
                // header scan definitely stops at first IDAT
                if (pal_img_n)
                    self.img_n = pal_img_n;
                return true;
            }
            if (c.length > (1u << 30))
                return Error(self, "IDAT size limit", "IDAT section larger than 2^30 bytes");
            if ((int)(ioff + c.length) < (int)ioff)
                return 0;
            if (ioff + c.length > idata_limit)
            {
                uint32_t idata_limit_old = idata_limit;
                unsigned char *p;
                if (idata_limit == 0)
                    idata_limit = c.length > 4096 ? c.length : 4096;
                while (ioff + c.length > idata_limit)
                    idata_limit *= 2;
                // STBI_NOTUSED(idata_limit_old);
                p = (unsigned char *)STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit);
                if (p == NULL)
                    return Error(self, "outofmem", "Out of memory");
                self.idata = p;
            }
            if (!stbi__getn(s, z->idata + ioff, c.length))
                return Error("outofdata", "Corrupt PNG");
            ioff += c.length;
            break;
        }

        case STBI__PNG_TYPE('I', 'E', 'N', 'D'): {
            uint32_t raw_len, bpl;
            if (first)
                return Error("first not IHDR", "Corrupt PNG");
            if (scan != STBI__SCAN_load)
                return true;
            if (z->idata == NULL)
                return Error("no IDAT", "Corrupt PNG");
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
            raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
            z->expanded = (stbi_uc *)stbi_zlib_decode_malloc_guesssize_headerflag((char *)z->idata, ioff, raw_len,
                                                                                  (int *)&raw_len, !is_iphone);
            if (z->expanded == NULL)
                return 0; // zlib should set error
            STBI_FREE(z->idata);
            z->idata = NULL;
            if ((req_comp == s->img_n + 1 && req_comp != 3 && !pal_img_n) || has_trans)
                s->img_out_n = s->img_n + 1;
            else
                s->img_out_n = s->img_n;
            if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace))
                return 0;
            if (has_trans)
            {
                if (z->depth == 16)
                {
                    if (!stbi__compute_transparency16(z, tc16, s->img_out_n))
                        return 0;
                }
                else
                {
                    if (!stbi__compute_transparency(z, tc, s->img_out_n))
                        return 0;
                }
            }
            if (is_iphone && stbi__de_iphone_flag && s->img_out_n > 2)
                stbi__de_iphone(z);
            if (pal_img_n)
            {
                // pal_img_n == 3 or 4
                s->img_n = pal_img_n; // record the actual colors we had
                s->img_out_n = pal_img_n;
                if (req_comp >= 3)
                    s->img_out_n = req_comp;
                if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
                    return 0;
            }
            else if (has_trans)
            {
                // non-paletted image with tRNS -> source image has (constant) alpha
                ++s->img_n;
            }
            STBI_FREE(z->expanded);
            z->expanded = NULL;
            // end of PNG chunk, read and skip CRC
            Pop32BE(s);
            return true;
        }

        default:
            // if critical, fail
            if (first)
                return stbi__err("first not IHDR", "Corrupt PNG");
            if ((c.type & (1 << 29)) == 0)
            {
#ifndef STBI_NO_FAILURE_STRINGS
                // not threadsafe
                static char invalid_chunk[] = "XXXX PNG chunk not known";
                invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
                invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
                invalid_chunk[2] = STBI__BYTECAST(c.type >> 8);
                invalid_chunk[3] = STBI__BYTECAST(c.type >> 0);
#endif
                return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
            }
            stbi__skip(s, c.length);
            break;
        }
        // end of PNG chunk, read and skip CRC
        Pop32BE(s);
    }
}

} // namespace nyla::ByteParser::PNGParser