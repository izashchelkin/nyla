#pragma once

#include <cstdint>

#include "nyla/commons/containers/map.h"

namespace nyla
{

struct AbstractInputId
{
    uint32_t tag;
    uint32_t code;
};

struct AbstractInputMapping
{
    int val;

    AbstractInputMapping()
    {
        static int next = 0;
        val = next++;
    }
};

void AbstractInputProcessFrame();
void AbstractInputHandlePressed(AbstractInputId id, uint64_t ts);
void AbstractInputHandleReleased(AbstractInputId id);
void AbstractInputRelease(AbstractInputMapping mapping);

void AbstractInputMapId(AbstractInputMapping mapping, AbstractInputId id);
bool Pressed(AbstractInputMapping mapping);
uint32_t PressedFor(AbstractInputMapping mapping, uint64_t now);

} // namespace nyla