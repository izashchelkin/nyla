#include "nyla/commons/os/readfile.h"
#include "nyla/commons/assert.h"

#include <fstream>
#include <string>
#include <vector>

namespace nyla
{

static auto ReadFileInternal(std::ifstream &file) -> std::vector<std::byte>
{
    NYLA_ASSERT(file.is_open());

    std::vector<std::byte> buffer(file.tellg());

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), static_cast<long>(buffer.size()));

    file.close();
    return buffer;
}

auto ReadFile(const std::string &filename) -> std::vector<std::byte>
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    return ReadFileInternal(file);
}

} // namespace nyla