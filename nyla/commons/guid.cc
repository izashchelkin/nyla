#include "nyla/commons/guid.h"

namespace nyla {

guid_t NextGuid() {
  static guid_t uid = 10000;
  return uid++;
}

}  // namespace nyla
