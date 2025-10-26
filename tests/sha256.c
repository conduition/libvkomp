#include <stdlib.h>
#include <string.h>
#include <vkomp.h>

#include "shaders/sha256.h"
#include "utils.h"

#define ERR_NO_USABLE_DEVICE 50
#define ERR_INVALID_OUTPUT 51

#define N_THREADS (1 << 20)
#define WORK_GROUP_SIZE 32
#define OUTPUT_WORDS 8
#define OUTPUT_BYTES (OUTPUT_WORDS * sizeof(uint32_t))

int test_device(VkompDeviceInfo device) {
  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkompContext     ctx        = {0};
  VkompFlow        flow       = {0};
  VkompBuffer      device_buf = {0};
  VkompBuffer      host_buf   = {0};

  int err = vkomp_context_init(device, &ctx);
  if (err) {
    eprintf("error initializing VkompContext\n");
    goto cleanup;
  }

  // Create a device local compute buffer where output will be written to
  err = vkomp_buffer_init(
    ctx,
    N_THREADS * sizeof(uint32_t) * OUTPUT_WORDS,
    VKOMP_BUFFER_TYPE_DEVICE,
    &device_buf
  );
  if (err) {
    eprintf("error initializing device buffer\n");
    goto cleanup;
  }

  // If the device local buffer is not host visible, we need a separate
  // host-visible buffer where the output will be copied to once the
  // shader completes.
  VkompBuffer* output_buf_ptr = &device_buf;
  if (!device_buf.is_host_visible) {
    // Create a host visible buffer where output will be copied to once the shader completes.
    err = vkomp_buffer_init(
      ctx,
      device_buf.size,
      VKOMP_BUFFER_TYPE_HOST,
      &host_buf
    );
    if (err) {
      eprintf("error initializing host buffer\n");
      goto cleanup;
    }
    output_buf_ptr = &host_buf;
  }

  printf("created buffers of size %lu\n", device_buf.size);

  // Adding an explicit copy is much faster than writing large outputs directly to
  // host-visible memory.
  VkompBufferCopyOp copy_op = {
    .src = &device_buf,
    .dest = &host_buf,
  };

  // Only perform the copy if needed
  uint32_t copy_ops_len = device_buf.is_host_visible ? 0 : 1;

  const uint32_t push_constants[] = { N_THREADS };
  uint32_t work_group_size = WORK_GROUP_SIZE;
  const void* const specialization_constants[] = { &work_group_size };
  const size_t specialization_constants_sizes[] = { sizeof(uint32_t) };

  VkompFlowStage stages[] = {
    {
      .compute_buffers = &device_buf,
      .compute_buffers_len = 1,
      .copy_ops = &copy_op,
      .copy_ops_len = copy_ops_len,
      .shader_spv = sha256_spv,
      .shader_spv_len = sha256_spv_len,
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

  // Copy the shader output back to CPU memory
  uint8_t* mapped = NULL;
  err = vkomp_buffer_map(ctx, *output_buf_ptr, (void**) &mapped);
  if (err) {
    eprintf("error mapping output from VkompBuffer\n");
    goto cleanup;
  }
  uint8_t* output_data = malloc(output_buf_ptr->size);
  memcpy(output_data, mapped, output_buf_ptr->size);
  vkomp_buffer_unmap(ctx, *output_buf_ptr);
  mapped = NULL;

  printf("shader output:\n");
  for (uint32_t i = 0; i < 4; i++) {
    printf("  ");
    for (uint32_t j = 0; j < OUTPUT_BYTES; j++) {
      printf("%02x", output_data[i * OUTPUT_BYTES + j]);
    }
    printf("\n");
  }
  printf("  ...\n");

  for (uint32_t i = N_THREADS - 4; i < N_THREADS; i++) {
    printf("  ");
    for (uint32_t j = 0; j < OUTPUT_BYTES; j++) {
      printf("%02x", output_data[i * OUTPUT_BYTES + j]);
    }
    printf("\n");
  }

  free(output_data);

cleanup:
  vkomp_flow_free(ctx, flow);
  vkomp_buffer_free(ctx, device_buf);
  vkomp_buffer_free(ctx, host_buf);
  vkomp_context_free(ctx);
  return err;
}

int main() {
  init_test();

  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkInstance       instance = NULL;
  VkompDeviceInfo* devices  = NULL;

  VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  int err = vkCreateInstance(&create_info, NULL, &instance);
  if (err) {
    eprintf("error creating vulkan instance\n");
    return err;
  }

  uint32_t devices_count;
  err = vkomp_devices_count(instance, &devices_count);
  if (err) {
    eprintf("error counting vulkan devices\n");
    goto cleanup;
  }

  if (devices_count == 0) {
    eprintf("no vulkan devices found\n");
    err = ERR_NO_USABLE_DEVICE;
    goto cleanup;
  }

  devices = malloc(devices_count * sizeof(VkompDeviceInfo));
  err = vkomp_devices_enumerate(instance, devices_count, devices);
  if (err) {
    eprintf("error enumerating vulkan devices\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < devices_count; i++) {
    char* devname = devices[i].properties.deviceName;
    if (devices[i].compute_queue_family < 0) {
      eprintf("skipping device: %s (compute queue family not found)\n", devname);
      continue;
    }

    printf("running tests on device: %s\n", devname);
    err = test_device(devices[i]);
    if (err) {
      eprintf("failed on device: %s\n", devname);
      goto cleanup;
    }
  }

cleanup:
  free(devices);
  vkDestroyInstance(instance, NULL);
  return err;
}
