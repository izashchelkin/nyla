pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/fmt.h"

namespace nyla
{

typedef struct
{
    uint32_t img_x, img_y;
    int img_n, img_out_n;

    int read_from_callbacks;
    int buflen;
    unsigned char buffer_start[128];
    int callback_already_read;

    uint8_t *img_buffer, *img_buffer_end;
    uint8_t *img_buffer_original, *img_buffer_original_end;
} stbi__context;

typedef struct
{
    stbi__context *s;
    uint8_t *idata, *expanded, *out;
    int depth;
} stbi__png;

typedef struct
{
    int bits_per_channel;
    int num_channels;
    int channel_order;
} stbi__result_info;

typedef struct
{
    uint32_t length;
    uint32_t type;
} stbi__pngchunk;

enum
{
    STBI__SCAN_load = 0,
    STBI__SCAN_type,
    STBI__SCAN_header
};

static int stbi__png_test(stbi__context *s);
static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp);
static int stbi__png_is16(stbi__context *s);

struct PNGParser : public ByteParser
{
    uint32_t m_Depth;
    uint8_t *idata;

    void Init(const uint8_t *base, uint64_t size)
    {
        ByteParser::Init(base, size);
    }

    auto Parse() -> bool;

  private:
    auto Error(const char *err) -> bool
    {
        NYLA_LOG("%s", err);
        return false;
    }

    auto Error(const char *err, const char *detail) -> bool
    {
        NYLA_LOG(NYLA_SV_FMT " " NYLA_SV_FMT, err, detail);
        return false;
    }

    stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
    {
        stbi__pngchunk c;
        c.length = Pop32BE();
        c.type = Pop32BE();
        return c;
    }
};

} // namespace nyla
