#pragma once

#include <span>

namespace nyla
{

auto Spawn(std::span<const char *const> cmd) -> bool;

}