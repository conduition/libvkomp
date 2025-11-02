#include <stdlib.h>
#include <vkomp.h>

#include "utils.h"

#define ERR_NO_USABLE_DEVICE 50
#define ERR_INVALID_OUTPUT 51

int test_device(VkompDeviceInfo device) {
  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkompContext ctx  = {0};
  VkompFlow    flow = {0};
  VkompBuffer  buf1 = {0};
  VkompBuffer  buf2 = {0};

  int err = vkomp_context_init(device, &ctx);
  if (err) {
    eprintf("error initializing VkompContext\n");
    goto cleanup;
  }

  err = vkomp_buffer_init(ctx, 32, VKOMP_BUFFER_TYPE_HOST, &buf1);
  if (err) {
    eprintf("error initializing VkompBuffer\n");
    goto cleanup;
  }
  err = vkomp_buffer_init(ctx, 32, VKOMP_BUFFER_TYPE_HOST, &buf2);
  if (err) {
    eprintf("error initializing VkompBuffer\n");
    goto cleanup;
  }
  printf("created buffers of size %lu\n", buf1.size);

  // Write input data to the buffer.
  uint8_t* mapped_bytes = NULL;
  err = vkomp_buffer_map(ctx, buf1, (void**) &mapped_bytes);
  if (err) {
    eprintf("error mapping VkompBuffer\n");
    goto cleanup;
  }
  for (uint32_t i = 0; i < 32; i++) {
    mapped_bytes[i] = i;
  }
  vkomp_buffer_unmap(ctx, buf1);
  mapped_bytes = NULL;
  printf("wrote input data to buf1\n");

  VkompBufferCopyOp copy_op = {
    .src = &buf1,
    .dest = &buf2,
  };

  VkompFlowStage stages[] = {
    {
      .copy_ops = &copy_op,
      .copy_ops_len = 1,
    },
  };
  uint32_t stages_len = 1;

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

  err = vkomp_buffer_map(ctx, buf2, (void**) &mapped_bytes);
  if (err) {
    eprintf("error mapping output from VkompBuffer\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < 32; i++) {
    if (mapped_bytes[i] != i) {
      err = ERR_INVALID_OUTPUT;
      eprintf("found invalid copy output: %u != %u\n", i, mapped_bytes[i]);
      goto cleanup;
    }
  }
  printf("output is correct\n");

  vkomp_buffer_unmap(ctx, buf2);
  mapped_bytes = NULL;

cleanup:
  vkomp_flow_free(ctx, flow);
  vkomp_buffer_free(ctx, buf1);
  vkomp_buffer_free(ctx, buf2);
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
  err = vkomp_devices_enumerate(instance, &devices_count, devices);
  if (err) {
    eprintf("error enumerating vulkan devices\n");
    goto cleanup;
  }

  for (uint32_t i = 0; i < devices_count; i++) {
    char* devname = devices[i].properties.deviceName;
    printf("running tests on device: %s\n", devname);
    err = test_device(devices[i]);
    if (err) {
      eprintf("failed on device: %s: %s\n", devname, vkomp_stringify_error_code(err));
      goto cleanup;
    }
  }

cleanup:
  free(devices);
  vkDestroyInstance(instance, NULL);
  return err;
}
