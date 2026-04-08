#include "nyla/commons/region_alloc.h"

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/log.h"
#include "nyla/commons/platform_base.h"

namespace nyla
{

namespace RegionAlloc
{

void NYLA_API CommitPages(region_alloc &self)
{
    NYLA_DASSERT(self.commitedEnd < self.at);

    uint8_t *oldCommitedEnd = self.commitedEnd;
    self.commitedEnd = AlignedUp(self.at, Platform::GetMemPageSize());
    Platform::CommitMemPages(oldCommitedEnd, self.commitedEnd);
}

} // namespace RegionAlloc

} // namespace nyla