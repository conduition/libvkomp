#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "utils.h"
#include "vkomp.h"

#define MIN(a, b) (a < b ? a : b)

void vkomp_context_free(VkompContext ctx) {
  vkDestroyCommandPool(ctx.device, ctx.cmd_pool, NULL);
  vkDestroyDevice(ctx.device, NULL);
}

int vkomp_context_init(VkompDeviceInfo device_info, VkompContext* ctx) {
  if (device_info.compute_queue_family < 0)
    return VKOMP_ERROR_DEVICE_CANNOT_COMPUTE;

  // Create the logical device handle
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = device_info.compute_queue_family,
      .queueCount = 1,
  };
  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pQueueCreateInfos = &queue_create_info,
      .queueCreateInfoCount = 1,
  };
  VkDevice device;
  int err = vkCreateDevice(device_info.dev_phy, &device_create_info, NULL, &device);
  if (err) return err;

  // Create a command pool
  VkCommandPoolCreateInfo cmd_pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = device_info.compute_queue_family,
  };
  VkCommandPool cmd_pool;
  err = vkCreateCommandPool(device, &cmd_pool_create_info, NULL, &cmd_pool);
  if (err) {
    vkDestroyDevice(device, NULL);
    return err;
  }

  ctx->dev_phy = device_info.dev_phy;
  ctx->device = device;
  ctx->queue_family_index = device_info.compute_queue_family;
  ctx->cmd_pool = cmd_pool;

  return 0;
}


void vkomp_buffer_free(VkompContext ctx, VkompBuffer compbuf) {
  vkFreeMemory(ctx.device, compbuf.memory, NULL);
  vkDestroyBuffer(ctx.device, compbuf.buffer, NULL);
}

int vkomp_buffer_init(
  VkompContext ctx,
  size_t size,
  VkompBufferType buf_type,
  VkompBuffer* compbuf
) {
  VkBuffer buffer;
  int err = vulkan_setup_buffer(ctx.device, size, &buffer);
  if (err) return err;

  VkMemoryPropertyFlags mem_flags;
  switch (buf_type) {
    case VKOMP_BUFFER_TYPE_HOST:
      mem_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      break;
    case VKOMP_BUFFER_TYPE_DEVICE:
      mem_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
    default:
      return VKOMP_ERROR_INVALID_BUFFER_TYPE;
  }

  VkMemoryPropertyFlags actual_mem_properties;
  VkDeviceMemory memory;
  err = vulkan_alloc_buffer_memory(
    ctx.device,
    ctx.dev_phy,
    buffer,
    mem_flags,
    &actual_mem_properties,
    &memory
  );
  if (err) {
    vkDestroyBuffer(ctx.device, buffer, NULL);
    return err;
  }

  compbuf->size = size;
  compbuf->buffer = buffer;
  compbuf->memory = memory;
  compbuf->buf_type = buf_type;
  compbuf->is_host_visible = !!(actual_mem_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  compbuf->is_device_local = !!(actual_mem_properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  return 0;
}

int vkomp_buffer_map(VkompContext ctx, VkompBuffer compbuf, void** mapped) {
  return vkMapMemory(
    ctx.device,
    compbuf.memory,
    0, // offset
    compbuf.size,
    0, // flags
    mapped
  );
}

void vkomp_buffer_unmap(VkompContext ctx, VkompBuffer compbuf) {
  vkUnmapMemory(ctx.device, compbuf.memory);
}

void vkomp_flow_stage_compiled_free(VkDevice device, VkompFlowStageCompiled compiled) {
  vkDestroyPipeline(device, compiled.pipeline, NULL);
  vkDestroyPipelineLayout(device, compiled.pipeline_layout, NULL);
  vkDestroyShaderModule(device, compiled.shader, NULL);
  vkDestroyDescriptorSetLayout(device, compiled.descriptor_set_layout, NULL);
}

int vkomp_flow_stage_compiled_init(
  VkDevice device,
  VkompFlowStage stage,
  VkompFlowStageCompiled* compiled
) {
  // Create the shader modules.
  VkShaderModule shader;
  int err = vulkan_setup_shader_module(device, stage.shader_spv, stage.shader_spv_len, &shader);
  if (err) return err;
  compiled->shader = shader;

  // Create the descriptor set layout
  VkDescriptorSetLayout descriptor_set_layout;
  err = vulkan_setup_descriptor_layout(device, stage.compute_buffers_len, &descriptor_set_layout);
  if (err) {
    vkomp_flow_stage_compiled_free(device, *compiled);
    return err;
  }
  compiled->descriptor_set_layout = descriptor_set_layout;

  // Create the pipeline layout
  VkPipelineLayout pipeline_layout;
  err = vulkan_setup_pipeline_layout(
    device,
    descriptor_set_layout,
    stage.push_constants_size,
    &pipeline_layout
  );
  if (err) {
    vkomp_flow_stage_compiled_free(device, *compiled);
    return err;
  }
  compiled->pipeline_layout = pipeline_layout;

  // Create the specialization constants info
  VkSpecializationInfo spec_info;
  VkSpecializationInfo* spec_info_ptr = NULL;
  if (stage.specialization_constants_len > 0) {
    spec_info_ptr = &spec_info;
    vulkan_setup_specialization_info(
      stage.specialization_constants,
      stage.specialization_constants_sizes,
      stage.specialization_constants_len,
      spec_info_ptr
    );
  }

  // Create the compute pipeline object.
  VkPipeline pipeline;
  err = vulkan_setup_pipeline(device, shader, pipeline_layout, spec_info_ptr, &pipeline);
  vulkan_free_specialization_info(spec_info_ptr);
  if (err) {
    vkomp_flow_stage_compiled_free(device, *compiled);
    return err;
  }
  compiled->pipeline = pipeline;

  return 0;
}

void vkomp_flow_stage_execution_resources_free(
  VkDevice device,
  VkompFlowStageExecutionResources resources
) {
  vkDestroyEvent(device, resources.done_event, NULL);
}

int vkomp_flow_stage_execution_resources_init(
  VkompContext ctx,
  VkompFlowStageCompiled compiled,
  VkDescriptorPool descriptor_pool,
  VkompFlowStageExecutionResources* resources
) {
  // Allocate a command buffer from the command pool.
  int err = vulkan_setup_command_buffer(ctx.device, ctx.cmd_pool, &resources->cmd_buf);
  if (err) return err;

  // Allocate a descriptor set from the descriptor pool
  err = vulkan_setup_descriptor_set(
    ctx.device,
    descriptor_pool,
    compiled.descriptor_set_layout,
    &resources->descriptor_set
  );
  if (err) return err;

  // Create an event which marks this stage as done.
  err = vulkan_setup_event(ctx.device, &resources->done_event);
  if (err) return err;

  return 0;
}

void vkomp_flow_free(VkompContext ctx, VkompFlow flow) {
  for (uint32_t i = 0; i < flow.stages_len; i++) {
    vkomp_flow_stage_compiled_free(ctx.device, flow.stages_compiled[i]);
    vkomp_flow_stage_execution_resources_free(ctx.device, flow.stages_resources[i]);
  }
  free(flow.stages_resources);
  free(flow.stages_compiled);
  free(flow.stages);
  vkDestroyDescriptorPool(ctx.device, flow.descriptor_pool, NULL);
}

int vkomp_flow_init(
  VkompContext ctx,
  VkompFlowStage* stages,
  uint32_t stages_len,
  VkompFlow* flow
) {
  int err;
  uint32_t stages_compiled_counter = 0;
  uint32_t stages_resources_counter = 0;
  uint32_t descriptor_count = 0;

  VkompFlowStageCompiled* stages_compiled
    = malloc(stages_len * sizeof(VkompFlowStageCompiled));

  VkompFlowStageExecutionResources* stages_resources
    = malloc(stages_len * sizeof(VkompFlowStageExecutionResources));

  // Compile each stage of the compute shader flow
  for (uint32_t i = 0; i < stages_len; i++) {
    err = vkomp_flow_stage_compiled_init(ctx.device, stages[i], &stages_compiled[i]);
    if (err) goto cleanup;
    stages_compiled_counter += 1;
    descriptor_count += stages[i].compute_buffers_len;
  }

  // Set up the descriptor pool
  err = vulkan_setup_descriptor_pool(ctx.device, descriptor_count, stages_len, &flow->descriptor_pool);
  if (err) goto cleanup;

  // Allocate resources to run each of the flow stages.
  for (uint32_t i = 0; i < stages_len; i++) {
    err = vkomp_flow_stage_execution_resources_init(
      ctx,
      stages_compiled[i],
      flow->descriptor_pool,
      &stages_resources[i]
    );
    if (err) goto cleanup;
    stages_resources_counter += 1;
  }

  // Bind each stage's compute buffers to its descriptor set.
  for (uint32_t i = 0; i < stages_len; i++) {
    uint32_t compute_buffers_len = stages[i].compute_buffers_len;
    for (uint32_t j = 0; j < compute_buffers_len; j++) {
      VkompBuffer* compbuf = &stages[i].compute_buffers[j];
      vulkan_bind_buffer_to_descriptor(
        ctx.device,
        compbuf->buffer,
        compbuf->size,
        j, // binding_index
        stages_resources[i].descriptor_set
      );
    }
  }

  // Fill each stage's command buffers.
  for (uint32_t i = 0; i < stages_len; i++) {
    uint32_t copy_ops_len = stages[i].copy_ops_len;
    VkBuffer* copy_sources = malloc(copy_ops_len * sizeof(VkBuffer));
    VkBuffer* copy_dests   = malloc(copy_ops_len * sizeof(VkBuffer));
    size_t*   copy_sizes   = malloc(copy_ops_len * sizeof(size_t));

    for (uint32_t j = 0; j < copy_ops_len; j++) {
      VkompBufferCopyOp copy_op = stages[i].copy_ops[j];
      copy_sources[i] = copy_op.src->buffer;
      copy_dests[i]   = copy_op.dest->buffer;
      copy_sizes[i]   = MIN(copy_op.src->size, copy_op.dest->size);
    }

    err = vulkan_write_command_buffer(
      stages_resources[i].cmd_buf,
      stages_compiled[i].pipeline,
      stages_compiled[i].pipeline_layout,
      stages_resources[i].descriptor_set,
      stages[i].work_group_count,
      stages[i].push_constants,
      stages[i].push_constants_size,
      copy_sources,
      copy_dests,
      copy_sizes,
      copy_ops_len,
      (i > 0) ? &stages_resources[i - 1].done_event : NULL,
      (i + 1 < stages_len) ? &stages_resources[i].done_event : NULL
    );
    free(copy_sources);
    free(copy_dests);
    free(copy_sizes);
    if (err) goto cleanup;
  }

  // Finally initialize the flow struct.
  flow->stages_len = stages_len;
  flow->stages_compiled = stages_compiled;
  flow->stages_resources = stages_resources;
  flow->stages = malloc(stages_len * sizeof(VkompFlowStage));
  for (uint32_t i = 0; i < stages_len; i++) {
    flow->stages[i] = stages[i];
  }

  return 0;

cleanup:
  for (uint32_t i = 0; i < stages_compiled_counter; i++)
    vkomp_flow_stage_compiled_free(ctx.device, stages_compiled[i]);
  for (uint32_t i = 0; i < stages_resources_counter; i++)
    vkomp_flow_stage_execution_resources_free(ctx.device, stages_resources[i]);
  free(stages_compiled);
  free(stages_resources);
  vkDestroyDescriptorPool(ctx.device, flow->descriptor_pool, NULL);
  return err;
}

int vkomp_flow_run(
  VkompContext ctx,
  VkompFlow flow
) {
  VkQueue queue;
  vkGetDeviceQueue(ctx.device, ctx.queue_family_index, 0, &queue);

  // We create a fence to await the final output.
  VkFenceCreateInfo fence_create_info = {
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };

  VkFence fence;
  int err = vkCreateFence(ctx.device, &fence_create_info, NULL, &fence);
  if (err) return err;

  // Submit all commands.
  VkSubmitInfo* submit_infos = malloc(sizeof(VkSubmitInfo) * flow.stages_len);
  for (uint32_t i = 0; i < flow.stages_len; i++) {
    submit_infos[i] = (VkSubmitInfo) {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &flow.stages_resources[i].cmd_buf,
    };
  }

  // We submit the command buffers on the queue, at the same time giving a fence.
  err = vkQueueSubmit(queue, flow.stages_len, submit_infos, fence);
  if (err) goto cleanup;

  err = vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, 100e9);

cleanup:
  vkDestroyFence(ctx.device, fence, NULL);
  return err;
}
