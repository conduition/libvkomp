#include <stdio.h>
#include <stdlib.h>
#include <vkomp.h>

#include "shader.h"

#define N_THREADS 100

int main() {
  // Resources to be freed at cleanup. Initialized to zero to avoid UB.
  VkInstance       instance = NULL;
  VkompContext     ctx      = {0};
  VkompFlow        flow     = {0};
  VkompBuffer      compbuf  = {0};

  // First we must create a vulkan instance to initialize the global application state.
  // This instance should be kept around and not destroyed until all VkompContexts are
  // also freed.
  VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  int err = vkCreateInstance(&create_info, NULL, &instance);
  if (err) {
    fprintf(stderr, "error creating vulkan instance: %s\n", vkomp_stringify_error_code(err));
    goto cleanup;
  }

  // A `VkompDeviceInfo` represents a physical vulkan compute device. To get a device, you
  // can use a shortcut like `vkomp_get_best_device` as a quick-and-dirty getter, or fully
  // enumerate all devices and search for your preferred device with `vkomp_devices_enumerate`.
  // There are various metadata fields available inside the `VkompDeviceInfo` struct to inspect devices.
  VkompDeviceInfo device;
  err = vkomp_get_best_device(instance, &device);
  if (err) {
    fprintf(stderr, "error getting vulkan device: %s\n", vkomp_stringify_error_code(err));
    goto cleanup;
  }

  // A `VkompContext` represents can be created to initialize a pool of resources
  // for running compute shaders on a given vulkan device.
  err = vkomp_context_init(device, &ctx);
  if (err) {
    fprintf(stderr, "error initializing context: %s\n", vkomp_stringify_error_code(err));
    goto cleanup;
  }

  // Create a host-visible `VkompBuffer`. Host visible buffers are slow when accessed from
  // the device, but convenient as their memory can be easily accessed from the host code (CPU).
  // `VKOMP_BUFFER_TYPE_HOST` tells Vkomp to allocate host-visible memory.
  // For best performance, use `VKOMP_BUFFER_TYPE_DEVICE` for heavy I/O, with copy operations
  // to move data around between host and device as needed.
  err = vkomp_buffer_init(
    ctx,
    N_THREADS * sizeof(uint32_t),
    VKOMP_BUFFER_TYPE_HOST,
    &compbuf
  );
  if (err) {
    fprintf(stderr, "error initializing compute buffer: %s\n", vkomp_stringify_error_code(err));
    goto cleanup;
  }

  // "Map" the vulkan buffer, returning a pointer to host-visible memory.
  // Reads/writes on this pointer are slow because the data must be
  // sent to/from the compute device.
  uint32_t* mapped = NULL;
  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped);
  if (err) {
    fprintf(stderr, "error mapping compute buffer: %s\n", vkomp_stringify_error_code(err));
    goto cleanup;
  }

  // Write input data which will be squared by the shader.
  for (uint32_t i = 0; i < N_THREADS; i++)
    mapped[i] = i;
  vkomp_buffer_unmap(ctx, compbuf); // remember to unmap!
  mapped = NULL;

  // SPIR-V push constants can be used to send small amounts of read-only data to the
  // compute shader. Good for global runtime parameters like a thread-count ceiling.
  const uint32_t push_constants[] = { N_THREADS };

  // In vulkan, a 'stage' typically refers to a step in the graphics pipeline.
  // In Vkomp we use the term 'stage' a bit differently: to refer to a linear
  // dependent sequence of compute shaders and copy operations. This declarative type
  // is how the caller defines what shaders to run and how to move their I/O data around.
  VkompFlowStage stages[] = {
    {
      // Buffers are bound to SPIR-V binding indexes in the order they are presented.
      // Only one binding set (set = 0) is ever used.
      .compute_buffers = &compbuf,
      .compute_buffers_len = 1,

      // The source code for the compute shader to run in this stage. From shader.h.
      .shader_spv = shader_spv,
      .shader_spv_len = shader_spv_len,

      // Control data to tell vulkan how many work groups to launch.
      // WORK_GROUP_COUNT is also defined when compiling the shader,
      // but we could have used a specialization constant to set the
      // work group size at runtime instead.
      .work_group_count = (N_THREADS + WORK_GROUP_COUNT - 1) / WORK_GROUP_COUNT,
      .push_constants = (const void*) push_constants,
      .push_constants_size = sizeof(push_constants),
    }
  };
  uint32_t stages_len = sizeof(stages) / sizeof(VkompFlowStage);

  // A `VkompFlow` can be thought of as a pipeline of compute stages, with
  // associated reusable runtime resources.
  //
  // Each stage runs one after another in sequence. Each stage
  // waits for the prior stage to complete before it is executed.
  //
  // A `VkompFlow` only needs to be initialized once, and it can then
  // be run as many times as you would like. The input can be changed by
  // simply mapping and modifying `VkompBuffers`, which are already
  // bound to the flow's stages.
  err = vkomp_flow_init(ctx, stages, stages_len, &flow);
  if (err) {
    fprintf(stderr, "error initializing VkompFlow\n");
    goto cleanup;
  }

  // Synchronously execute the flow and await the completion of its last stage.
  err = vkomp_flow_run(ctx, flow);
  if (err) {
    fprintf(stderr, "error running VkompFlow\n");
    goto cleanup;
  }

  // Map the `VkompBuffer` so we can read its output on the host side.
  err = vkomp_buffer_map(ctx, compbuf, (void**) &mapped);
  if (err) {
    fprintf(stderr, "error mapping output from VkompBuffer\n");
    goto cleanup;
  }
  printf("shader output:\n");
  printf("[");
  for (int i = 0; i < N_THREADS - 1; i++)
    printf("%u, ", mapped[i]);
  printf("%u]\n", mapped[N_THREADS - 1]);

  vkomp_buffer_unmap(ctx, compbuf);
  mapped = NULL;


// We must free up resources at the end of the program to avoid memory leaks.
cleanup:
  vkomp_flow_free(ctx, flow);
  vkomp_buffer_free(ctx, compbuf);
  vkomp_context_free(ctx);
  vkDestroyInstance(instance, NULL);
  return err;
}
