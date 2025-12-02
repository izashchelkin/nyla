#include "nyla/rhi/rhi_handle.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla {

using namespace rhi_internal;
using namespace rhi_vulkan_internal;

namespace rhi_vulkan_internal {

VkFormat ConvertRhiTextureFormatIntoVkFormat(RhiTextureFormat format) {
  switch (format) {
    case RhiTextureFormat::None:
      break;

    case RhiTextureFormat::R8G8B8A8_sRGB:
      return VK_FORMAT_R8G8B8A8_SRGB;
  }

  CHECK(false);
  return static_cast<VkFormat>(0);
}

RhiTextureFormat ConvertVkFormatIntoRhiTextureFormat(VkFormat format) {
  CHECK(false);
  return static_cast<RhiTextureFormat>(0);
}

}  // namespace rhi_vulkan_internal

RhiTexture RhiCreateTexture(RhiTextureDesc desc) {
  VulkanTextureData texture_data{
      .format = ConvertRhiTextureFormatIntoVkFormat(desc.format),
      .memory_property_flags = ConvertRhiMemoryUsageIntoVkMemoryPropertyFlags(desc.memory_usage),
  };

  const VkImageCreateInfo image_create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = texture_data.format,
      .extent = {desc.width, desc.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VK_CHECK(vkCreateImage(vk.dev, &image_create_info, vk.alloc, &texture_data.image));

  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(vk.dev, texture_data.image, &memory_requirements);

  const VkMemoryAllocateInfo memory_alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = FindMemoryTypeIndex(memory_requirements, texture_data.memory_property_flags),
  };
  vkAllocateMemory(vk.dev, &memory_alloc_info, vk.alloc, &texture_data.memory);

  const VkImageViewCreateInfo image_view_create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = texture_data.image,
      .format = texture_data.format,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  vkCreateImageView(vk.dev, &image_view_create_info, vk.alloc, &texture_data.image_view);

  return RhiHandleAcquire(rhi_handles.textures, texture_data);
}

void RhiDestroyTexture(RhiTexture texture) {
  VulkanTextureData texture_data = RhiHandleRelease(rhi_handles.textures, texture);

  vkDestroyImageView(vk.dev, texture_data.image_view, vk.alloc);
  vkDestroyImage(vk.dev, texture_data.image, vk.alloc);
  vkFreeMemory(vk.dev, texture_data.memory, vk.alloc);
}

}  // namespace nyla