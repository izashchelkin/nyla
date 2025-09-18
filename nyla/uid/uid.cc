#include "nyla/uid/uid.h"

namespace nyla {

Uid NextUid() {
  static Uid uid = 1;
  return uid++;
}

}  // namespace nyla
