#pragma once
#include <stdlib.h>
#include <vulkan/vulkan.h>

// Find a queue family with VK_QUEUE_COMPUTE_BIT. Returns -1 if no valid
// compute queue family exists on the device.
int vulkan_find_compute_queue_family(VkPhysicalDevice dev_phy);

// Setup a buffer of a given size.
int vulkan_setup_buffer(
  VkDevice device,
  size_t size,
  VkBuffer* buffer
);

// Allocate memory to back a buffer.
int vulkan_alloc_buffer_memory(
  VkDevice device,
  VkPhysicalDevice dev_phy,
  VkBuffer buffer,
  VkMemoryPropertyFlags mem_flags,
  VkMemoryPropertyFlags* actual_mem_properties,
  VkDeviceMemory* memory
);

// Setup a descriptor layout with `binding_count` storage buffers.
int vulkan_setup_descriptor_layout(
  VkDevice device,
  uint32_t binding_count,
  VkDescriptorSetLayout* descriptor_set_layout
);

int vulkan_setup_shader_module(
  VkDevice device,
  const uint8_t* shader_spv,
  size_t shader_spv_len,
  VkShaderModule* shader
);

// Set up the vulkan pipeline layout, telling vulkan what descriptors
// will be used by the pipeline and how to structure them.
int vulkan_setup_pipeline_layout(
  VkDevice device,
  VkDescriptorSetLayout descriptor_set_layout,
  size_t push_constants_size,
  VkPipelineLayout* pipeline_layout
);

// Sets up specialization constants, which can be used to dynamically
// set work group size and other compile-time parameters.
void vulkan_setup_specialization_info(
  const void* const* specialization_constants,
  const size_t* specialization_constants_sizes,
  uint32_t specialization_constants_len,
  VkSpecializationInfo* spec_info
);
// Free the specialization info struct.
void vulkan_free_specialization_info(VkSpecializationInfo* spec_info);

int vulkan_setup_pipeline(
  VkDevice device,
  VkShaderModule shader,
  VkPipelineLayout pipeline_layout,
  const VkSpecializationInfo* spec_info,
  VkPipeline* pipeline
);

int vulkan_setup_command_buffer(
  VkDevice device,
  VkCommandPool cmd_pool,
  VkCommandBuffer* cmd_buf
);

int vulkan_setup_descriptor_set(
  VkDevice device,
  VkDescriptorPool descriptor_pool,
  VkDescriptorSetLayout descriptor_set_layout,
  VkDescriptorSet* descriptor_set
);

int vulkan_setup_event(VkDevice device, VkEvent* event);

int vulkan_setup_descriptor_pool(
  VkDevice device,
  uint32_t descriptor_count,
  uint32_t descriptor_sets_count,
  VkDescriptorPool* descriptor_pool
);

void vulkan_bind_buffer_to_descriptor(
  VkDevice device,
  VkBuffer buffer,
  size_t buffer_size,
  uint32_t binding_index,
  VkDescriptorSet descriptor_set
);

int vulkan_write_command_buffer(
  VkCommandBuffer cmd_buf,
  VkPipeline pipeline,
  VkPipelineLayout pipeline_layout,
  VkDescriptorSet descriptor_set,
  uint32_t work_group_count,
  const void* push_constants,
  size_t push_constants_size,
  VkBuffer* copy_sources,
  VkBuffer* copy_dests,
  const size_t* copy_sizes,
  uint32_t copy_ops_len,
  const VkEvent* prev_event,
  const VkEvent* done_event
);
