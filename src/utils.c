#include <vulkan/vulkan.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"
#include "vkomp.h"

int _vkomp_intern_find_compute_queue_family(VkPhysicalDevice dev_phy) {
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(dev_phy, &queue_family_count, NULL);
  VkQueueFamilyProperties* queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
  vkGetPhysicalDeviceQueueFamilyProperties(dev_phy, &queue_family_count, queue_families);
  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      return i;
    }
  }
  free(queue_families);
  return -1;
}

static int vulkan_find_memory_type_index(
    VkPhysicalDevice dev_phy,
    uint32_t mem_type_bits,
    VkMemoryPropertyFlags desired_properties,
    VkMemoryPropertyFlags* actual_properties,
    uint32_t* mem_type_index
) {
  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(dev_phy, &mem_properties);

  // How does this search work?
  // See the documentation of VkPhysicalDeviceMemoryProperties for a detailed description.
  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
    if (
      (mem_type_bits & (1 << i)) &&
      ((mem_properties.memoryTypes[i].propertyFlags & desired_properties) == desired_properties)
    ) {
      *mem_type_index = i;
      *actual_properties = mem_properties.memoryTypes[i].propertyFlags;
      return 0;
    }
  }
  return VKOMP_ERROR_MEMORY_TYPE_NOT_FOUND;
}

int _vkomp_intern_setup_buffer(
  VkDevice device,
  size_t size,
  VkBuffer* buffer
) {
  VkBufferCreateInfo buf_create_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // buffer is exclusive to a single queue family at a time.
  };
  return vkCreateBuffer(device, &buf_create_info, NULL, buffer);
}


int _vkomp_intern_alloc_buffer_memory(
  VkDevice device,
  VkPhysicalDevice dev_phy,
  VkBuffer buffer,
  VkMemoryPropertyFlags mem_flags,
  VkMemoryPropertyFlags* actual_mem_properties,
  VkDeviceMemory* memory
) {
  // Creating a vulkan buffer doesn't allocate any memory. We do that manually now.
  // Required memory size is specified by the vulkan buffer.
  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);
  VkMemoryAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = mem_requirements.size,
  };

  // Different devices have different memory types. We must find a mem type which
  // satisfies the caller's needs, and tell the caller what other flags that
  // mem type supports.
  int err = vulkan_find_memory_type_index(
    dev_phy,
    mem_requirements.memoryTypeBits,
    mem_flags,
    actual_mem_properties,
    &allocate_info.memoryTypeIndex
  );
  if (err) return err;

  // Allocates memory on the device.
  err = vkAllocateMemory(device, &allocate_info, NULL, memory);
  if (err) return err;

  // Bind the vulkan buffer object to the memory backing.
  return vkBindBufferMemory(device, buffer, *memory, /* offset */ 0);
}


int _vkomp_intern_setup_descriptor_layout(
  VkDevice device,
  uint32_t binding_count,
  VkDescriptorSetLayout* descriptor_set_layout
) {
  VkDescriptorSetLayoutBinding* bindings = malloc(binding_count * sizeof(VkDescriptorSetLayoutBinding));

  for (uint32_t i = 0; i < binding_count; i++) {
    VkDescriptorSetLayoutBinding binding = {
      .binding = i,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    bindings[i] = binding;
  };

  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = binding_count,
    .pBindings = bindings,
  };

  int err = vkCreateDescriptorSetLayout(
    device,
    &descriptor_set_layout_create_info,
    NULL,
    descriptor_set_layout
  );
  free(bindings);
  return err;
}


int _vkomp_intern_setup_shader_module(
  VkDevice device,
  const uint8_t* shader_spv,
  size_t shader_spv_len,
  VkShaderModule* shader
) {
  VkShaderModuleCreateInfo shader_create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pCode = (uint32_t*) shader_spv,
    .codeSize = shader_spv_len,
  };
  return vkCreateShaderModule(device, &shader_create_info, NULL, shader);
}


int _vkomp_intern_setup_pipeline_layout(
  VkDevice device,
  VkDescriptorSetLayout descriptor_set_layout,
  size_t push_constants_size,
  VkPipelineLayout* pipeline_layout
) {
  VkPushConstantRange push_const_range = {
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .size = push_constants_size,
  };
  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &descriptor_set_layout,
  };
  if (push_constants_size > 0) {
    pipeline_layout_create_info.pPushConstantRanges = &push_const_range;
    pipeline_layout_create_info.pushConstantRangeCount = 1;
  }
  return vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, pipeline_layout);
}

void _vkomp_intern_setup_specialization_info(
  const void* const* specialization_constants,
  const size_t* specialization_constants_sizes,
  uint32_t specialization_constants_len,
  VkSpecializationInfo* spec_info
) {
  size_t spec_const_data_size = 0;
  for (uint32_t i = 0; i < specialization_constants_len; i++) {
    spec_const_data_size += specialization_constants_sizes[i];
  }

  VkSpecializationMapEntry* spec_const_entries
    = malloc(sizeof(VkSpecializationMapEntry) * specialization_constants_len);
  uint8_t* spec_const_data
    = malloc(spec_const_data_size);

  size_t ctr = 0;
  for (uint32_t i = 0; i < specialization_constants_len; i++) {
    spec_const_entries[i] = (VkSpecializationMapEntry) {
      .constantID = i,
      .offset = ctr,
      .size = specialization_constants_sizes[i],
    };
    memcpy(
      &spec_const_data[ctr],
      specialization_constants[i],
      specialization_constants_sizes[i]
    );
    ctr += specialization_constants_sizes[i];
  }

  spec_info->mapEntryCount = specialization_constants_len;
  spec_info->pMapEntries = spec_const_entries;
  spec_info->dataSize = spec_const_data_size;
  spec_info->pData = (void*) spec_const_data;
}

void _vkomp_intern_free_specialization_info(VkSpecializationInfo* spec_info) {
  if (spec_info != NULL) {
    free((void*) spec_info->pData);
    free((void*) spec_info->pMapEntries);
    spec_info->pData = NULL;
    spec_info->pMapEntries = NULL;
  }
}

int _vkomp_intern_setup_pipeline(
  VkDevice device,
  VkShaderModule shader,
  VkPipelineLayout pipeline_layout,
  const VkSpecializationInfo* spec_info,
  VkPipeline* pipeline
) {
  VkPipelineShaderStageCreateInfo shader_stage_create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = shader,
    .pName = "main",
    .pSpecializationInfo = spec_info,
  };
  VkComputePipelineCreateInfo pipeline_create_info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = shader_stage_create_info,
    .layout = pipeline_layout,
  };
  return vkCreateComputePipelines(
    device,
    VK_NULL_HANDLE, // pipeline cache, TODO
    1, // create_infos_len
    &pipeline_create_info,
    NULL, // callbacks
    pipeline
  );
}

int _vkomp_intern_setup_command_buffer(
  VkDevice device,
  VkCommandPool cmd_pool,
  VkCommandBuffer* cmd_buf
) {
  VkCommandBufferAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = cmd_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  return vkAllocateCommandBuffers(device, &alloc_info, cmd_buf);
}


int _vkomp_intern_setup_descriptor_set(
  VkDevice device,
  VkDescriptorPool descriptor_pool,
  VkDescriptorSetLayout descriptor_set_layout,
  VkDescriptorSet* descriptor_set
) {
  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = descriptor_pool, // pool to allocate from.
    .descriptorSetCount = 1, // allocate a single descriptor set.
    .pSetLayouts = &descriptor_set_layout,
  };
  return vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, descriptor_set);
}


int _vkomp_intern_setup_event(VkDevice device, VkEvent* event) {
  VkEventCreateInfo event_create_info = {
    .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
    .flags = VK_EVENT_CREATE_DEVICE_ONLY_BIT, // only the device uses the event
  };
  return vkCreateEvent(device, &event_create_info, NULL, event);
}


int _vkomp_intern_setup_descriptor_pool(
  VkDevice device,
  uint32_t descriptor_count,
  uint32_t descriptor_sets_count,
  VkDescriptorPool* descriptor_pool
) {
  VkDescriptorPoolSize descriptor_pool_size = {
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = descriptor_count,
  };

  VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = descriptor_sets_count,
    .poolSizeCount = 1,
    .pPoolSizes = &descriptor_pool_size,
  };

  return vkCreateDescriptorPool(
    device,
    &descriptor_pool_create_info,
    NULL,
    descriptor_pool
  );
}


// Connect a storage buffer with the descriptor.
void _vkomp_intern_bind_buffer_to_descriptor(
  VkDevice device,
  VkBuffer buffer,
  size_t buffer_size,
  uint32_t binding_index,
  VkDescriptorSet descriptor_set
) {
  // Specify the buffer to bind to the descriptor.
  VkDescriptorBufferInfo descriptor_buffer_info = {
    .buffer = buffer,
    .offset = 0,
    .range = buffer_size,
  };

  VkWriteDescriptorSet write_descriptor_set = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = descriptor_set, // write to this descriptor set.
    .dstBinding = binding_index,
    .descriptorCount = 1, // update a single descriptor.
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &descriptor_buffer_info,
  };

  vkUpdateDescriptorSets(
    device,
    1, &write_descriptor_set,
    0, NULL
  );
}
