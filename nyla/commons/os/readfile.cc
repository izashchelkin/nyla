#include "nyla/commons/os/readfile.h"

#include <fstream>
#include <string>
#include <vector>

#include "absl/log/check.h"

namespace nyla {

std::vector<char> ReadFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  CHECK(file.is_open());

  std::vector<char> buffer(file.tellg());

  file.seekg(0);
  file.read(buffer.data(), buffer.size());

  file.close();
  return buffer;
}

}  // namespace nyla