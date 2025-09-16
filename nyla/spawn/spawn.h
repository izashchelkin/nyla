#ifndef NYLA_SPAWN_H
#define NYLA_SPAWN_H

#include <span>

namespace nyla {

bool Spawn(std::span<const char* const> cmd);

}

#endif
