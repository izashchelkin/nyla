#include "nyla/vulkan/vulkan.h"

#include "absl/log/check.h"
#include "volk/volk.h"

namespace nyla {}  // namespace nyla

int main() {
  CHECK(volkInitialize() == VK_SUCCESS);

  VkInstance instance{};
  {
    VkInstanceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    CHECK(vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS);
  }
  volkLoadInstance(instance);

  {
    VkDeviceCreateInfo info;
  }
}
