#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct http_response
{
    int32_t status_code; // HTTP status code, -1 on curl/network failure
    byteview body;       // region-allocated response body
};

// POST JSON to an HTTPS URL via curl subprocess.
// url: full URL (e.g. "https://api.deepseek.com/v1/chat/completions")
// json_body: the POST body (already serialized JSON)
// api_key: Bearer token for Authorization header (empty = no auth)
// alloc: region allocator for response body
auto API HttpPostJson(byteview url, byteview json_body, byteview api_key, region_alloc &alloc) -> http_response;

} // namespace nyla
