#include "nyla/commons/http_client.h"

#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/subprocess.h"

namespace nyla
{

constexpr uint64_t kMaxUrlLen = 0x1000; // 4 KB max URL

auto API HttpPostJson(byteview url, byteview json_body, byteview api_key, region_alloc &alloc) -> http_response
{
    http_response result = {};
    result.status_code = -1;

    if (url.size == 0 || url.size > kMaxUrlLen)
        return result;

    // Build null-terminated URL on the allocator
    span<char> urlBuf = RegionAlloc::AllocArray<char>(alloc, url.size + 1);
    MemCpy(urlBuf.data, url.data, url.size);
    urlBuf.data[url.size] = '\0';

    // Build curl args inline
    inline_vec<const char *, 20> args;
    InlineVec::Clear(args);
    InlineVec::Append(args, "curl");
    InlineVec::Append(args, "-s");
    InlineVec::Append(args, "-D");
    InlineVec::Append(args, "/dev/stderr");
    InlineVec::Append(args, "-o");
    InlineVec::Append(args, "-");
    InlineVec::Append(args, "-X");
    InlineVec::Append(args, "POST");
    InlineVec::Append(args, "-H");
    InlineVec::Append(args, "Content-Type: application/json");

    // Authorization header (optional)
    span<char> authBuf;
    if (api_key.size > 0)
    {
        // Build "Authorization: Bearer <key>" as a null-terminated string
        constexpr uint64_t kPrefixLen = sizeof("Authorization: Bearer ") - 1;
        uint64_t authLen = kPrefixLen + api_key.size;
        authBuf = RegionAlloc::AllocArray<char>(alloc, authLen + 1);
        MemCpy(authBuf.data, "Authorization: Bearer ", kPrefixLen);
        MemCpy(authBuf.data + kPrefixLen, api_key.data, api_key.size);
        authBuf.data[authLen] = '\0';

        InlineVec::Append(args, "-H");
        InlineVec::Append(args, authBuf.data);
    }

    InlineVec::Append(args, "--data-binary");
    InlineVec::Append(args, "@-");
    InlineVec::Append(args, urlBuf.data);
    InlineVec::Append(args, nullptr); // sentinel

    auto proc = SubprocessRun(span<const char *const>{(const char *const *)args.begin(), args.size}, alloc, json_body,
                              30'000); // 30 second timeout
    if (proc.exit_code != 0)
        return result;

    // Parse HTTP status code from stderr's first line
    // Format: "HTTP/1.1 200 OK\r\n..." or "HTTP/2 200 \r\n..."
    int32_t status = -1;
    if (proc.stderr_data.size >= 12) // "HTTP/1.1 NNN" minimum
    {
        uint8_t const *p = proc.stderr_data.data;
        uint8_t const *end = p + proc.stderr_data.size;

        // Skip "HTTP/" (5 chars)
        if (p + 5 < end && p[0] == 'H' && p[1] == 'T' && p[2] == 'T' && p[3] == 'P' && p[4] == '/')
        {
            p += 5;
            // Skip version number until space
            while (p < end && *p != ' ')
                p++;
            // Skip space
            if (p < end)
                p++;
            // Parse status code
            while (p < end && *p >= '0' && *p <= '9')
            {
                if (status < 0)
                    status = 0;
                status = status * 10 + (int32_t)(*p - '0');
                p++;
            }
        }
    }
    result.status_code = status;
    result.body = proc.stdout_data;
    return result;
}

} // namespace nyla
