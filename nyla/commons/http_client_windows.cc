#include "nyla/commons/http_client.h"

namespace nyla
{

auto API HttpPostJson(byteview url, byteview json_body, byteview api_key, region_alloc &alloc) -> http_response
{
    (void)url;
    (void)json_body;
    (void)api_key;
    (void)alloc;

    http_response result = {};
    result.status_code = -1;
    return result;
}

} // namespace nyla
