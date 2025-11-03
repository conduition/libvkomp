#include <stdlib.h>
#include <vkomp.h>

#include "shaders/double.h"
#include "shaders/square_simple.h"
#include "utils.h"

#define ERR_INVALID_OUTPUT 51
#define N_THREADS 100
#define WORK_GROUP_SIZE 16

int double_flow_init(VkompContext ctx, VkompBuffer compbuf, VkompFlow* flow) {
  const uint32_t push_constants[] = { N_THREADS };

  uint32_t work_group_size = WORK_GROUP_SIZE;
  const void* const specialization_constants[] = { &work_group_size };
  const size_t specialization_constants_sizes[] = { sizeof(uint32_t) };

  VkompFlowStage stage = {
    .compute_buffers = &compbuf,
    .compute_buffers_len = 1,
    .shader_spv = double_spv,
    .shader_spv_len = double_spv_len,
    .work_group_count = (N_THREADS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
    .push_constants = (const void*) push_constants,
    .push_constants_size = sizeof(push_constants),
    .specialization_constants = specialization_constants,
    .specialization_constants_sizes = specialization_constants_sizes,
    .specialization_constants_len = sizeof(specialization_constants) / sizeof(void*),
  };

  return vkomp_flow_init(ctx, &stage, 1, flow);
}

int square_flow_init(VkompContext ctx, VkompBuffer compbuf, VkompFlow* flow) {
  const uint32_t push_constants[] = { N_THREADS };

  uint32_t work_group_size = WORK_GROUP_SIZE;
  const void* const specialization_constants[] = { &work_group_size };
  const size_t specialization_constants_sizes[] = { sizeof(uint32_t) };

  VkompFlowStage stage = {
    .compute_buffers = &compbuf,
    .compute_buffers_len = 1,
    .shader_spv = square_simple_spv,
    .shader_spv_len = square_simple_spv_len,
    .work_group_count = (N_THREADS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
    .push_constants = (const void*) push_constants,
    .push_constants_size = sizeof(push_constants),
    .specialization_constants = specialization_constants,
    .specialization_constants_sizes = specialization_constants_sizes,
    .specialization_constants_len = sizeof(specialization_constants) / sizeof(void*),
  };

  return vkomp_flow_init(ctx, &stage, 1, flow);
}

int main() {
  init_test();

  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkInstance       instance    = NULL;
  VkompContext     ctx         = {0};
  VkompFlow        double_flow = {0};
  VkompFlow        square_flow = {0};
  VkompBuffer      compbuf     = {0};

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

  // Create a host-visible compute buffer
  err = vkomp_buffer_init(
    ctx,
    N_THREADS * sizeof(uint32_t),
    VKOMP_BUFFER_TYPE_HOST,
    &compbuf
  );
  if (err) {
    eprintf("error initializing VkompBuffer\n");
    goto cleanup;
  }
  printf("created buffer of size %lu\n", compbuf.size);

  // Write input data to the buffer.
  uint32_t* mapped_words = NULL;
  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped_words);
  if (err) {
    eprintf("error mapping VkompBuffer\n");
    goto cleanup;
  }
  for (uint32_t i = 0; i < N_THREADS; i++) {
    mapped_words[i] = i;
  }
  vkomp_buffer_unmap(ctx, compbuf);
  mapped_words = NULL;
  printf("wrote input data to buffer\n");

  err = double_flow_init(ctx, compbuf, &double_flow);
  if (err) {
    eprintf("failed to set up double flow\n");
    goto cleanup;
  }

  err = square_flow_init(ctx, compbuf, &square_flow);
  if (err) {
    eprintf("failed to set up double flow\n");
    goto cleanup;
  }

  // y = (x * 2 * 2) ** 2
  VkompFlow* flows[] = { &double_flow, &double_flow, &square_flow };
  uint32_t flows_len = sizeof(flows) / sizeof(VkompFlow*);

  err = vkomp_flows_run_sequential(ctx, flows, flows_len);
  if (err) {
    eprintf("error running VkompFlow\n");
    goto cleanup;
  }
  printf("executed three shader flows in sequence\n");

  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped_words);
  if (err) {
    eprintf("error mapping output from VkompBuffer\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < N_THREADS; i++) {
    uint32_t doubled = i * 2;
    uint32_t quadrupled = doubled * 2;
    uint32_t squared = quadrupled * quadrupled;
    if (mapped_words[i] != squared) {
      err = ERR_INVALID_OUTPUT;
      eprintf("found invalid shader output: (%u * 4) ** 2 != %u\n", i, mapped_words[i]);
      goto cleanup;
    }
  }
  printf("output is correct\n");

  vkomp_buffer_unmap(ctx, compbuf);
  mapped_words = NULL;


cleanup:
  vkomp_flow_free(ctx, double_flow);
  vkomp_flow_free(ctx, square_flow);
  vkomp_buffer_free(ctx, compbuf);
  vkomp_context_free(ctx);
  vkDestroyInstance(instance, NULL);
  return err;
}
