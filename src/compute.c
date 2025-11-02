#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "utils.h"
#include "vkomp.h"

#define MIN(a, b) (a < b ? a : b)

void vkomp_context_free(VkompContext ctx) {
  if (ctx.device != NULL) {
    vkDestroyCommandPool(ctx.device, ctx.cmd_pool, NULL);
    vkDestroyDevice(ctx.device, NULL);
    free(ctx.device_info);
  }
}

int vkomp_context_init(VkompDeviceInfo device_info, VkompContext* ctx) {
  // Create the logical device handle
  float priority = 1.0;
  VkDeviceQueueCreateInfo queue_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = device_info.compute_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
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

  ctx->device = device;
  ctx->cmd_pool = cmd_pool;

  // The context is passed around a lot, we should probably not be
  // copying this data around every time.
  ctx->device_info = malloc(sizeof(VkompDeviceInfo));
  *ctx->device_info = device_info;

  return 0;
}


void vkomp_buffer_free(VkompContext ctx, VkompBuffer compbuf) {
  if (ctx.device != NULL) {
    vkFreeMemory(ctx.device, compbuf.memory, NULL);
    vkDestroyBuffer(ctx.device, compbuf.buffer, NULL);
  }
}

int vkomp_buffer_init(
  VkompContext ctx,
  size_t size,
  VkompBufferType buf_type,
  VkompBuffer* compbuf
) {
  VkBuffer buffer;
  int err = _vkomp_intern_setup_buffer(ctx.device, size, &buffer);
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
  err = _vkomp_intern_alloc_buffer_memory(
    ctx.device,
    ctx.device_info->dev_phy,
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
  if (!compbuf.is_host_visible) {
    return VKOMP_ERROR_BUFFER_NOT_HOST_VISIBLE;
  }

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

void vkomp_flow_stage_execution_resources_free(
  VkDevice device,
  VkompFlowStageExecutionResources resources
) {
  vkDestroyPipeline(device, resources.pipeline, NULL);
  vkDestroyPipelineLayout(device, resources.pipeline_layout, NULL);
  vkDestroyShaderModule(device, resources.shader, NULL);
  vkDestroyDescriptorSetLayout(device, resources.descriptor_set_layout, NULL);
  vkDestroyEvent(device, resources.done_event, NULL);
}

int vkomp_flow_stage_execution_resources_init(
  VkompContext ctx,
  VkompFlowStage stage,
  VkDescriptorPool descriptor_pool,
  VkompFlowStageExecutionResources* resources
) {
  int err;

  // To be freed if this function fails.
  VkShaderModule        shader                = NULL;
  VkDescriptorSetLayout descriptor_set_layout = NULL;
  VkPipelineLayout      pipeline_layout       = NULL;
  VkPipeline            pipeline              = NULL;
  VkEvent               done_event            = NULL;

  // No need to free.
  VkCommandBuffer cmd_buf = NULL;
  VkDescriptorSet descriptor_set = NULL;

  // Build the shader and its related resources, but only if the stage has defined shader code.
  if (stage.shader_spv_len > 0) {
    err = _vkomp_intern_setup_shader_module(ctx.device, stage.shader_spv, stage.shader_spv_len, &shader);
    if (err) return err;

    // Create the descriptor set layout
    err = _vkomp_intern_setup_descriptor_layout(ctx.device, stage.compute_buffers_len, &descriptor_set_layout);
    if (err) goto cleanup;

    // Create the pipeline layout
    err = _vkomp_intern_setup_pipeline_layout(
      ctx.device,
      descriptor_set_layout,
      stage.push_constants_size,
      &pipeline_layout
    );
    if (err) goto cleanup;

    // Create the specialization constants info
    VkSpecializationInfo spec_info;
    VkSpecializationInfo* spec_info_ptr = NULL;
    if (stage.specialization_constants_len > 0) {
      spec_info_ptr = &spec_info;
      _vkomp_intern_setup_specialization_info(
        stage.specialization_constants,
        stage.specialization_constants_sizes,
        stage.specialization_constants_len,
        spec_info_ptr
      );
    }

    // Create the compute pipeline object.
    err = _vkomp_intern_setup_pipeline(ctx.device, shader, pipeline_layout, spec_info_ptr, &pipeline);
    _vkomp_intern_free_specialization_info(spec_info_ptr);
    if (err) goto cleanup;

    // Allocate a descriptor set from the descriptor pool
    err = _vkomp_intern_setup_descriptor_set(
      ctx.device,
      descriptor_pool,
      descriptor_set_layout,
      &descriptor_set
    );
    if (err) goto cleanup;
  }

  // Allocate a command buffer from the command pool.
  err = _vkomp_intern_setup_command_buffer(ctx.device, ctx.cmd_pool, &cmd_buf);
  if (err) goto cleanup;

  // Create an event which marks this stage as done.
  err = _vkomp_intern_setup_event(ctx.device, &done_event);
  if (err) goto cleanup;

  resources->pipeline = pipeline;
  resources->pipeline_layout = pipeline_layout;
  resources->shader = shader;
  resources->descriptor_set_layout = descriptor_set_layout;
  resources->descriptor_set = descriptor_set;
  resources->done_event = done_event;
  resources->cmd_buf = cmd_buf;

  return 0;

cleanup:
  vkDestroyPipeline(ctx.device, pipeline, NULL);
  vkDestroyPipelineLayout(ctx.device, pipeline_layout, NULL);
  vkDestroyShaderModule(ctx.device, shader, NULL);
  vkDestroyDescriptorSetLayout(ctx.device, descriptor_set_layout, NULL);
  vkDestroyEvent(ctx.device, done_event, NULL);
  return err;
}

void vkomp_flow_free(VkompContext ctx, VkompFlow flow) {
  if (ctx.device != NULL) {
    for (uint32_t i = 0; i < flow.stages_len; i++) {
      vkomp_flow_stage_execution_resources_free(ctx.device, flow.stages_resources[i]);
    }
    free(flow.stages_resources);
    free(flow.stages);
    vkDestroyDescriptorPool(ctx.device, flow.descriptor_pool, NULL);
  }
}

static int _vkomp_intern_write_copy_op(VkCommandBuffer cmd_buf, VkompBufferCopyOp copy_op) {
  size_t copy_size = copy_op.size;
  if (copy_size == 0) {
    copy_size = MIN(
      copy_op.src->size - copy_op.src_offset,
      copy_op.dest->size - copy_op.dest_offset
    );
  }

  if (
    copy_op.src_offset + copy_size > copy_op.src->size ||
    copy_op.dest_offset + copy_size > copy_op.dest->size
  )
    return VKOMP_ERROR_COPY_OP_OUT_OF_BOUNDS;


  VkBufferCopy regions = {
    .size = copy_size,
    .srcOffset = copy_op.src_offset,
    .dstOffset = copy_op.dest_offset,
  };
  vkCmdCopyBuffer(
    cmd_buf,
    copy_op.src->buffer,
    copy_op.dest->buffer,
    1, // region count
    &regions // regions
  );
  return 0;
}


int vkomp_flow_init(
  VkompContext ctx,
  VkompFlowStage* stages,
  uint32_t stages_len,
  VkompFlow* flow
) {
  // To be freed on error
  VkDescriptorPool descriptor_pool = NULL;

  // Count how many descriptors we need and resources we have allocated.
  uint32_t stages_resources_counter = 0;
  uint32_t descriptor_count = 0;
  for (uint32_t i = 0; i < stages_len; i++) {
    descriptor_count += stages[i].compute_buffers_len;
  }

  // Set up the descriptor pool
  int err = _vkomp_intern_setup_descriptor_pool(
    ctx.device,
    descriptor_count,
    stages_len,
    &descriptor_pool
  );
  if (err) return err;

  VkompFlowStageExecutionResources* stages_resources
    = malloc(stages_len * sizeof(VkompFlowStageExecutionResources));

  // Compile, bind, and allocate resources for each stage of the compute shader flow.
  for (uint32_t i = 0; i < stages_len; i++) {
    err = vkomp_flow_stage_execution_resources_init(
      ctx,
      stages[i],
      descriptor_pool,
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
      _vkomp_intern_bind_buffer_to_descriptor(
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
    VkCommandBuffer  cmd_buf         = stages_resources[i].cmd_buf;
    VkPipeline       pipeline        = stages_resources[i].pipeline;
    VkPipelineLayout pipeline_layout = stages_resources[i].pipeline_layout;
    VkDescriptorSet  descriptor_set  = stages_resources[i].descriptor_set;


    // Write the commands to the command buffer.
    VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    err = vkBeginCommandBuffer(cmd_buf, &begin_info);
    if (err) goto cleanup;

    // We need to bind a pipeline, AND a descriptor set before we dispatch.
    // The validation layer will NOT give warnings if you forget these, so be very careful not to forget them.
    if (pipeline != NULL)  {
      vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(
        cmd_buf,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout,
        0, // set number of first descriptor_set to be bound
        1, // number of descriptor sets
        &descriptor_set,
        0,  // offset count
        NULL // offsets array
      );
    }

    // Bind push constants
    if (stages[i].push_constants_size > 0) {
      vkCmdPushConstants(
        cmd_buf,
        pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0, //  offset
        stages[i].push_constants_size,
        stages[i].push_constants
      );
    }

    if (i > 0) {
      vkCmdWaitEvents(
        cmd_buf,
        1, // num events
        &stages_resources[i - 1].done_event,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        0, NULL, 0, NULL, 0, NULL
      );
    }

    // pre-shader copy ops.
    for (uint32_t j = 0; j < stages[i].copy_ops_len; j++) {
      VkompBufferCopyOp copy_op = stages[i].copy_ops[j];
      if (copy_op.before_shader) {
        err = _vkomp_intern_write_copy_op(cmd_buf, copy_op);
        if (err) goto cleanup;
      }
    }

    // Dispatch the compute shader.
    if (pipeline != NULL) {
      vkCmdDispatch(
        cmd_buf,
        stages[i].work_group_count, // X dimension workgroups
        1,  // Y dimension workgroups
        1   // Z dimension workgroups
      );
    }

    // post-shader copy ops.
    for (uint32_t j = 0; j < stages[i].copy_ops_len; j++) {
      VkompBufferCopyOp copy_op = stages[i].copy_ops[j];
      if (!copy_op.before_shader) {
        err = _vkomp_intern_write_copy_op(cmd_buf, copy_op);
        if (err) goto cleanup;
      }
    }

    if (i + 1 < stages_len) {
      vkCmdSetEvent(
        cmd_buf,
        stages_resources[i].done_event,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
      );
    }
    err = vkEndCommandBuffer(cmd_buf);
    if (err) goto cleanup;
  }

  // Finally initialize the flow struct.
  flow->stages_len = stages_len;
  flow->stages_resources = stages_resources;
  flow->descriptor_pool = descriptor_pool;
  flow->stages = malloc(stages_len * sizeof(VkompFlowStage));
  for (uint32_t i = 0; i < stages_len; i++) {
    flow->stages[i] = stages[i];
  }

  return 0;

cleanup:
  for (uint32_t i = 0; i < stages_resources_counter; i++)
    vkomp_flow_stage_execution_resources_free(ctx.device, stages_resources[i]);
  free(stages_resources);
  vkDestroyDescriptorPool(ctx.device, descriptor_pool, NULL);
  return err;
}

int vkomp_flow_run(
  VkompContext ctx,
  VkompFlow flow
) {
  VkQueue queue;
  vkGetDeviceQueue(ctx.device, ctx.device_info->compute_queue_family, 0, &queue);

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
