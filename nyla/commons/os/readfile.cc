#include "nyla/commons/os/readfile.h"

#include <fstream>
#include <string>
#include <vector>

#include "absl/log/check.h"

namespace nyla
{

static auto ReadFileInternal(std::ifstream &file) -> std::vector<char>
{
    CHECK(file.is_open());

    std::vector<char> buffer(file.tellg());

    file.seekg(0);
    file.read(buffer.data(), buffer.size());

    file.close();
    return buffer;
}

auto ReadFile(const std::string &filename) -> std::vector<char>
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    return ReadFileInternal(file);
}

} // namespace nyla