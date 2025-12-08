#pragma once

#include <string>
#include <vector>

namespace nyla
{

auto ReadFile(const std::string &filename) -> std::vector<std::byte>;

} // namespace nyla