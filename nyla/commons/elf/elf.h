#pragma once

#include "nyla/commons/byteparser.h"
#include "nyla/commons/region_alloc.h"
#include <cstdint>

namespace nyla
{

struct Elf64Header
{
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t programHeaderTableOffset;
    uint64_t sectionHeaderTableOffset;
    uint32_t flags;
    uint16_t elfHeaderSize;
    uint16_t programHeaderEntrySize;
    uint16_t programHeaderEntryCount;
    uint16_t sectionHeaderEntrySize;
    uint16_t sectionHeaderEntryCount;
    uint16_t sectionHeaderStringTableIndex;
};
static_assert(sizeof(Elf64Header) == 64);

struct Elf64ProgramHeader
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtualAddress;
    uint64_t physicalAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint64_t align;
};
static_assert(sizeof(Elf64ProgramHeader) == 56);

struct Elf64SectionHeader
{
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t align;
    uint64_t entrySizae;
};
static_assert(sizeof(Elf64SectionHeader) == 64);

class ElfParser : public ByteParser
{
  public:
    void Init(RegionAlloc *alloc, const char *base, uint32_t size)
    {
        m_Alloc = alloc;
        m_At = base;
        m_Left = size;
    }

    void Parse();

  private:
    RegionAlloc *m_Alloc;
};

} // namespace nyla