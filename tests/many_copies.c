#include <stdlib.h>
#include <vkomp.h>

#include "shaders/many_copies.h"
#include "utils.h"

#define ERR_INVALID_OUTPUT 51
#define N_THREADS 100
#define WORK_GROUP_SIZE 32

int main() {
  init_test();

  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkInstance       instance   = NULL;
  VkompContext     ctx        = {0};
  VkompFlow        flow       = {0};
  VkompBuffer      host_buf1  = {0};
  VkompBuffer      host_buf2  = {0};
  VkompBuffer      device_buf = {0};

  VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  int err = vkCreateInstance(&create_info, NULL, &instance);
  if (err) {
    eprintf("error creating vulkan instance\n");
    return err;
  }

  VkompDeviceInfo device;
  err = vkomp_get_best_device(instance, &device);
  if (err) {
    eprintf("error getting best vulkan device\n");
    goto cleanup;
  }

  err = vkomp_context_init(device, &ctx);
  if (err) {
    eprintf("error initializing VkompContext\n");
    goto cleanup;
  }

  // Initialize the device buf;
  err = vkomp_buffer_init(
    ctx,
    N_THREADS * sizeof(uint32_t),
    VKOMP_BUFFER_TYPE_DEVICE,
    &device_buf
  );
  if (err) {
    eprintf("error initializing device buffer\n");
    goto cleanup;
  }
  printf("created device local buffer of size %lu\n", device_buf.size);

  // Create two host-visible compute buffers
  VkompBuffer* host_bufs[] = { &host_buf1, &host_buf2 };
  for (int i = 0; i < 2; i++) {
    err = vkomp_buffer_init(
      ctx,
      N_THREADS * sizeof(uint32_t),
      VKOMP_BUFFER_TYPE_HOST,
      host_bufs[i]
    );
    if (err) {
      eprintf("error initializing host buffers\n");
      goto cleanup;
    }
  }
  printf("created host visible buffers of size %lu\n", host_buf1.size);

  // Write input data to the buffer.
  uint32_t* mapped_words = NULL;
  err = vkomp_buffer_map(ctx, host_buf1, (void**) &mapped_words);
  if (err) {
    eprintf("error mapping VkompBuffer\n");
    goto cleanup;
  }
  for (uint32_t i = 0; i < N_THREADS; i++) {
    mapped_words[i] = i + 1;
  }
  vkomp_buffer_unmap(ctx, host_buf1);
  mapped_words = NULL;
  printf("wrote input data to first host-visible buffer\n");

  VkompBufferCopyOp copy_ops[] = {
    {
      .src = &host_buf1,
      .dest = &device_buf,
      .before_shader = true,
    },
    {
      .src = &device_buf,
      .dest = &host_buf2,
      .before_shader = false,
    },
  };

  const uint32_t push_constants[] = { N_THREADS };

  uint32_t work_group_size = WORK_GROUP_SIZE;
  const void* const specialization_constants[] = { &work_group_size };
  const size_t specialization_constants_sizes[] = { sizeof(uint32_t) };

  VkompFlowStage stages[] = {
    {
      .compute_buffers = &device_buf,
      .compute_buffers_len = 1,
      .shader_spv = many_copies_spv,
      .shader_spv_len = many_copies_spv_len,
      .work_group_count = (N_THREADS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
      .push_constants = (const void*) push_constants,
      .push_constants_size = sizeof(push_constants),
      .copy_ops = copy_ops,
      .copy_ops_len = sizeof(copy_ops) / sizeof(VkompBufferCopyOp),
      .specialization_constants = specialization_constants,
      .specialization_constants_sizes = specialization_constants_sizes,
      .specialization_constants_len = sizeof(specialization_constants) / sizeof(void*),
    }
  };
  uint32_t stages_len = sizeof(stages) / sizeof(VkompFlowStage);

  err = vkomp_flow_init(ctx, stages, stages_len, &flow);
  if (err) {
    eprintf("error initializing VkompFlow\n");
    goto cleanup;
  }
  printf("initialized the compute flow\n");

  err = vkomp_flow_run(ctx, flow);
  if (err) {
    eprintf("error running VkompFlow\n");
    goto cleanup;
  }
  printf("executed shader\n");

  err = vkomp_buffer_map(ctx, host_buf2, (void**) &mapped_words);
  if (err) {
    eprintf("error mapping output from VkompBuffer\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < N_THREADS; i++) {
    if (mapped_words[i] != (i + 1) * (i + 1)) {
      err = ERR_INVALID_OUTPUT;
      eprintf("found invalid square shader output: %u^2 != %u\n", i + 1, mapped_words[i]);
      goto cleanup;
    }
  }
  printf("output is correct\n");

  vkomp_buffer_unmap(ctx, host_buf2);
  mapped_words = NULL;


cleanup:
  vkomp_flow_free(ctx, flow);
  vkomp_buffer_free(ctx, host_buf1);
  vkomp_buffer_free(ctx, host_buf2);
  vkomp_buffer_free(ctx, device_buf);
  vkomp_context_free(ctx);
  vkDestroyInstance(instance, NULL);
  return err;
}
