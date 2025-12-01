// #include <cstdint>
//
// #include "nyla/commons/memory/align.h"
// #include "nyla/commons/memory/charview.h"
// #include "nyla/fwk/vk_mem.h"
// #include "nyla/vulkan/vulkan.h"
//
// namespace nyla {
//
// static uint32_t FindMemoryTypeIndex(VkMemoryRequirements mem_requirements, VkMemoryPropertyFlags properties) {
//   static const VkPhysicalDeviceMemoryProperties mem_propertities = [] {
//     VkPhysicalDeviceMemoryProperties mem_propertities;
//     vkGetPhysicalDeviceMemoryProperties(vk.phys_device, &mem_propertities);
//     return mem_propertities;
//   }();
//
//   for (uint32_t i = 0; i < mem_propertities.memoryTypeCount; ++i) {
//     if (!(mem_requirements.memoryTypeBits & (1 << i))) {
//       continue;
//     }
//
//     if ((mem_propertities.memoryTypes[i].propertyFlags & properties) != properties) {
//       continue;
//     }
//
//     return i;
//   }
//
//   CHECK(false);
// }
//
// namespace {
//
// struct Buf {
//   uint32_t size;
//   uint32_t written;
//   VkBufferUsageFlags usage;
//   VkMemoryPropertyFlags mem_properties;
//   VkBuffer buf;
//   VkDeviceMemory mem;
//   char* mapped;
// };
//
// }  // namespace
//
// static void InitBuf(Buf& buf) {
//   const VkBufferCreateInfo buffer_create_info{
//       .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
//       .size = buf.size,
//       .usage = buf.usage,
//       .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
//   };
//   VK_CHECK(vkCreateBuffer(vk.dev, &buffer_create_info, nullptr, &buf.buf));
//
//   VkMemoryRequirements mem_requirements;
//   vkGetBufferMemoryRequirements(vk.dev, buf.buf, &mem_requirements);
//
//   const VkMemoryAllocateInfo memory_alloc_info{
//       .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
//       .allocationSize = mem_requirements.size,
//       .memoryTypeIndex = FindMemoryTypeIndex(mem_requirements, buf.mem_properties),
//   };
//
//   VK_CHECK(vkAllocateMemory(vk.dev, &memory_alloc_info, nullptr, &buf.mem));
//   VK_CHECK(vkBindBufferMemory(vk.dev, buf.buf, buf.mem, 0));
// }
//
// static void MapBuf(const Buf& buf) {
//   vkMapMemory(vk.dev, buf.mem, 0, buf.size, 0, (void**)&buf.mapped);
// }
//
// static Buf static_buf;
// static Buf staging_buf;
// static Buf dynamic_buf;
//
// void VulkMemInit(uint32_t static_vertbuf_size, uint32_t dynamic_vertbuf_size) {
//   staging_buf = {
//       .size = 64 * (1 << 20),
//       .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//       .mem_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
//   };
//   InitBuf(staging_buf);
//   MapBuf(staging_buf);
//
//   static_buf = {
//       .size = static_vertbuf_size,
//       .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
//       .mem_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//   };
//   InitBuf(static_buf);
//
//   dynamic_buf = {
//       .size = dynamic_vertbuf_size,
//       .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
//       .mem_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
//   };
//   InitBuf(dynamic_buf);
//   MapBuf(dynamic_buf);
// }
//
// void VulkMemStaticCopy(CharView data) {
//   // CHECK_LE(static_buf.written + data.size(), static_buf.size);
//
//   VulkWaitTimeline(vk.transfer_queue.timeline, *vk.transfer_queue.cmd_done);
//
//   const VkCommandBufferBeginInfo command_buffer_begin_info{
//       .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
//   };
//
//   VK_CHECK(vkBeginCommandBuffer(*vk.transfer_queue.cmd, &command_buffer_begin_info));
//   {
//     const VkBufferCopy copy_region{
//         .srcOffset = 0,
//         .dstOffset = static_buf.written,
//         .size = VK_WHOLE_SIZE,
//     };
//     vkCmdCopyBuffer(*vk.transfer_queue.cmd, staging_buf.buf, static_buf.buf, 1, &copy_region);
//   }
//   VK_CHECK(vkEndCommandBuffer(*vk.transfer_queue.cmd));
//
//   const VkTimelineSemaphoreSubmitInfo timeline_submit_info{
//       .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
//       .signalSemaphoreValueCount = 1,
//       .pSignalSemaphoreValues = &(*vk.transfer_queue.cmd_done = vk.transfer_queue.timeline_next++),
//   };
//   const VkSubmitInfo submit_info{
//       .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//       .pNext = &timeline_submit_info,
//       .commandBufferCount = 1,
//       .pCommandBuffers = vk.transfer_queue.cmd,
//       .signalSemaphoreCount = 1,
//       .pSignalSemaphores = &vk.transfer_queue.timeline,
//   };
//   vkQueueSubmit(vk.transfer_queue.queue, 1, &submit_info, VK_NULL_HANDLE);
// }
//
// void VkMemStaticFlush() {
//   //
// }
//
// void VkMemDynamicCopy() {
//   vk.cur.iframe;
// }
//
// static void Test(VkCommandPool command_pool, VkQueue transfer_queue, VkDeviceSize data_size, const void* src_data,
//                  VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& buffer_memory) {
//   VkBuffer staging_buffer;
//   VkDeviceMemory staging_buffer_memory;
//   Vulkan_CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer,
//                       staging_buffer_memory);
//
//   void* dst_data;
//   vkMapMemory(vk.dev, staging_buffer_memory, 0, data_size, 0, &dst_data);
//   memcpy(dst_data, src_data, data_size);
//   vkUnmapMemory(vk.dev, staging_buffer_memory);
//
//   Vulkan_CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer,
//                       buffer_memory);
//
//   const VkCommandBufferAllocateInfo command_buffer_alloc_info{
//       .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
//       .commandPool = command_pool,
//       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//       .commandBufferCount = 1,
//   };
//
//   VkCommandBuffer command_buffer;
//   VK_CHECK(vkAllocateCommandBuffers(vk.dev, &command_buffer_alloc_info, &command_buffer));
//
//   const VkCommandBufferBeginInfo command_buffer_begin_info{
//       .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
//       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
//   };
//
//   VK_CHECK(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));
//
//   const VkBufferCopy copy_region{
//       .srcOffset = 0,
//       .dstOffset = 0,
//       .size = data_size,
//   };
//   vkCmdCopyBuffer(command_buffer, staging_buffer, buffer, 1, &copy_region);
//
//   vkEndCommandBuffer(command_buffer);
//
//   const VkSubmitInfo submit_info{
//       .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//       .commandBufferCount = 1,
//       .pCommandBuffers = &command_buffer,
//   };
//   vkQueueSubmit(transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
//   vkQueueWaitIdle(transfer_queue);
//   vkFreeCommandBuffers(vk.dev, command_pool, 1, &command_buffer);
//   vkDestroyBuffer(vk.dev, staging_buffer, nullptr);
//   vkFreeMemory(vk.dev, staging_buffer_memory, nullptr);
// }
//
// }  // namespace nyla