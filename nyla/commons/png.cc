#include <cstdint>

#include "nyla/commons/mem.h"
#include "nyla/commons/png.h"
#include "nyla/commons/word.h"

#define STBI__PNG_TYPE(a, b, c, d)                                                                                     \
    (((unsigned)(a) << 24) + ((unsigned)(b) << 16) + ((unsigned)(c) << 8) + (unsigned)(d))
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

auto PNGParser::Parse(Instance &self, ScanType scan) -> bool
{
    uint8_t palette[1024], pal_img_n = 0;
    uint8_t has_trans = 0, tc[3] = {0};
    uint16_t tc16[3];
    uint32_t ioff = 0, idata_limit = 0, i, pal_len = 0;
    int first = 1, k, interlace = 0, color = 0, is_iphone = 0;
    stbi__context *s = z->s;

    z->expanded = NULL;
    z->idata = NULL;
    z->out = NULL;

    if (!MemStartsWith(self.m_At, self.m_Left, PNGSig, sizeof(PNGSig)))
        return Error(self, "Invalid PNG signature");

    if (!stbi__check_png_header(s))
        return 0;

    if (scan == STBI__SCAN_type)
        return 1;

    for (;;)
    {
        stbi__pngchunk c = stbi__get_chunk_header(s);
        switch (c.type)
        {
        case STBI__PNG_TYPE('C', 'g', 'B', 'I'):
            is_iphone = 1;
            Advance(c.length);
            break;
        case STBI__PNG_TYPE('I', 'H', 'D', 'R'): {
            int comp, filter;
            if (!first)
                return Error("multiple IHDR", "Corrupt PNG");
            first = 0;
            if (c.length != 13)
                return Error("bad IHDR len", "Corrupt PNG");
            s->img_x = Pop32BE();
            s->img_y = Pop32BE();
            if (s->img_y > STBI_MAX_DIMENSIONS)
                return Error("too large", "Very large image (corrupt?)");
            if (s->img_x > STBI_MAX_DIMENSIONS)
                return Error("too large", "Very large image (corrupt?)");

            m_Depth = Pop();
            switch (m_Depth)
            {
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
                break;

            default:
                return Error("1/2/4/8/16-bit only", "PNG not supported: 1/2/4/8/16-bit only");
            }

            color = Pop();
            if (color > 6)
                return Error("bad ctype", "Corrupt PNG");
            if (color == 3 && m_Depth == 16)
                return Error("bad ctype", "Corrupt PNG");
            if (color == 3)
                pal_img_n = 3;
            else if (color & 1)
                return Error("bad ctype", "Corrupt PNG");
            comp = Pop();
            if (comp)
                return Error("bad comp method", "Corrupt PNG");
            filter = Pop();
            if (filter)
                return Error("bad filter method", "Corrupt PNG");
            interlace = Pop();
            if (interlace > 1)
                return Error("bad interlace method", "Corrupt PNG");
            if (!s->img_x || !s->img_y)
                return Error("0-pixel image", "Corrupt PNG");
            if (!pal_img_n)
            {
                s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
                if ((1 << 30) / s->img_x / s->img_n < s->img_y)
                    return Error("too large", "Image too large to decode");
            }
            else
            {
                // if paletted, then pal_n is our final components, and
                // img_n is # components to decompress/filter.
                s->img_n = 1;
                if ((1 << 30) / s->img_x / 4 < s->img_y)
                    return Error("too large", "Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
        }

        case STBI__PNG_TYPE('P', 'L', 'T', 'E'): {
            if (first)
                return Error("first not IHDR", "Corrupt PNG");
            if (c.length > 256 * 3)
                return Error("invalid PLTE", "Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length)
                return Error("invalid PLTE", "Corrupt PNG");
            for (i = 0; i < pal_len; ++i)
            {
                palette[i * 4 + 0] = Pop();
                palette[i * 4 + 1] = Pop();
                palette[i * 4 + 2] = Pop();
                palette[i * 4 + 3] = 255;
            }
            break;
        }

        case STBI__PNG_TYPE('t', 'R', 'N', 'S'): {
            if (first)
                return Error("first not IHDR", "Corrupt PNG");
            if (z->idata)
                return Error("tRNS after IDAT", "Corrupt PNG");
            if (pal_img_n)
            {
                if (scan == STBI__SCAN_header)
                {
                    s->img_n = 4;
                    return 1;
                }
                if (pal_len == 0)
                    return Error("tRNS before PLTE", "Corrupt PNG");
                if (c.length > pal_len)
                    return Error("bad tRNS len", "Corrupt PNG");
                pal_img_n = 4;
                for (i = 0; i < c.length; ++i)
                    palette[i * 4 + 3] = Pop();
            }
            else
            {
                if (!(s->img_n & 1))
                    return Error("tRNS with alpha", "Corrupt PNG");
                if (c.length != (uint32_t)s->img_n * 2)
                    return Error("bad tRNS len", "Corrupt PNG");
                has_trans = 1;
                // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
                if (scan == STBI__SCAN_header)
                {
                    ++s->img_n;
                    return 1;
                }
                if (m_Depth == 16)
                {
                    for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                        tc16[k] = Pop16BE();                // copy the values as-is
                }
                else
                {
                    for (k = 0; k < s->img_n && k < 3; ++k)
                    {
                        tc[k] = (unsigned char)(Pop16BE() & 255) *
                                stbi__depth_scale_table[m_Depth]; // non 8-bit images will be larger
                    }
                }
            }
            break;
        }

        case STBI__PNG_TYPE('I', 'D', 'A', 'T'): {
            if (first)
                return Error("first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len)
                return Error("no PLTE", "Corrupt PNG");
            if (scan == STBI__SCAN_header)
            {
                // header scan definitely stops at first IDAT
                if (pal_img_n)
                    s->img_n = pal_img_n;
                return 1;
            }
            if (c.length > (1u << 30))
                return Error("IDAT size limit", "IDAT section larger than 2^30 bytes");
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
                    return Error("outofmem", "Out of memory");
                z->idata = p;
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
                return 1;
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
            return 1;
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

namespace
{

int stbi__err(const char *str)
{
    // stbi__g_failure_reason = str;
    return 0;
}

int stbi__err(const char *str, const char *str1)
{
    // stbi__g_failure_reason = str;
    return 0;
}

stbi_inline static uint8_t stbi__get8(stbi__context *s)
{
    if (s->img_buffer < s->img_buffer_end)
        return *s->img_buffer++;
    else
        return 0;
}

static int stbi__get16be(stbi__context *s)
{
    int z = stbi__get8(s);
    return (z << 8) + stbi__get8(s);
}

static uint32_t Pop32BE(stbi__context *s)
{
    uint32_t z = stbi__get16be(s);
    return (z << 16) + stbi__get16be(s);
}

int stbi__check_png_header(stbi__context *s)
{
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    int i;
    for (i = 0; i < 8; ++i)
        if (stbi__get8(s) != png_sig[i])
            return stbi__err("bad png sig", "Not a PNG");
    return 1;
}

int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
}

} // namespace

} // namespace nyla::ByteParser::PNGParser