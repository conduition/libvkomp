#include <stdlib.h>
#include <vkomp.h>

#include "shaders/square_with_consts.h"
#include "utils.h"

#define ERR_NO_USABLE_DEVICE 10002
#define ERR_INVALID_OUTPUT 10003
#define N_THREADS 100
#define WORK_GROUP_SIZE 16

int main() {
  init_test();

  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkInstance       instance = NULL;
  VkompDeviceInfo* devices  = NULL;
  VkompContext     ctx      = {0};
  VkompFlow        flow     = {0};
  VkompBuffer      compbuf  = {0};

  VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  int err = vkCreateInstance(&create_info, NULL, &instance);
  if (err) {
    fprintf(stderr, "error creating vulkan instance\n");
    return err;
  }

  uint32_t devices_count;
  err = vkomp_devices_count(instance, &devices_count);
  if (err) {
    fprintf(stderr, "error counting vulkan devices\n");
    goto cleanup;
  }

  if (devices_count == 0) {
    fprintf(stderr, "no vulkan devices found\n");
    goto cleanup;
  }

  devices = malloc(devices_count * sizeof(VkompDeviceInfo));
  err = vkomp_devices_enumerate(instance, devices_count, devices);
  if (err) {
    fprintf(stderr, "error enumerating vulkan devices\n");
    goto cleanup;
  }

  err = vkomp_context_init(devices[0], &ctx);
  if (err) {
    fprintf(stderr, "error initializing VkompContext\n");
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
    fprintf(stderr, "error initializing VkompBuffer\n");
    goto cleanup;
  }
  printf("created buffer of size %lu\n", compbuf.size);

  // Write input data to the buffer.
  uint32_t* mapped_words = NULL;
  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped_words);
  if (err) {
    fprintf(stderr, "error mapping VkompBuffer\n");
    goto cleanup;
  }
  for (uint32_t i = 0; i < N_THREADS; i++) {
    mapped_words[i] = i + 1;
  }
  vkomp_buffer_unmap(ctx, compbuf);
  mapped_words = NULL;
  printf("wrote input data to buffer\n");

  const uint32_t push_constants[] = { N_THREADS };

  uint32_t work_group_size = WORK_GROUP_SIZE;
  const void* const specialization_constants[] = { &work_group_size };
  const size_t specialization_constants_sizes[] = { sizeof(uint32_t) };

  VkompFlowStage stages[] = {
    {
      .compute_buffers = &compbuf,
      .compute_buffers_len = 1,
      .shader_spv = square_with_consts_spv,
      .shader_spv_len = square_with_consts_spv_len,
      .work_group_count = (N_THREADS + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
      .push_constants = (const void*) push_constants,
      .push_constants_size = sizeof(push_constants),
      .specialization_constants = specialization_constants,
      .specialization_constants_sizes = specialization_constants_sizes,
      .specialization_constants_len = sizeof(specialization_constants) / sizeof(void*),
    }
  };
  uint32_t stages_len = sizeof(stages) / sizeof(VkompFlowStage);


  err = vkomp_flow_init(ctx, stages, stages_len, &flow);
  if (err) {
    fprintf(stderr, "error initializing VkompFlow\n");
    goto cleanup;
  }
  printf("initialized the compute flow\n");

  err = vkomp_flow_run(ctx, flow);
  if (err) {
    fprintf(stderr, "error running VkompFlow\n");
    goto cleanup;
  }
  printf("executed shader\n");

  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped_words);
  if (err) {
    fprintf(stderr, "error mapping output from VkompBuffer\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < N_THREADS; i++) {
    if (mapped_words[i] != (i + 1) * (i + 1)) {
      err = ERR_INVALID_OUTPUT;
      fprintf(stderr, "found invalid square shader output: %u^2 != %u\n", i, mapped_words[i]);
      goto cleanup;
    }
  }
  printf("output is correct\n");

  vkomp_buffer_unmap(ctx, compbuf);
  mapped_words = NULL;


cleanup:
  vkomp_flow_free(ctx, flow);
  vkomp_buffer_free(ctx, compbuf);
  vkomp_context_free(ctx);
  free(devices);
  vkDestroyInstance(instance, NULL);
  return err;
}
