#pragma once
#include <stdbool.h>
#include <vulkan/vulkan.h>

// Represents a vulkan device. Note that not all devices support compute shaders.
// Check that `VkompDeviceInfo::compute_queue_family >= 0` before attempting to use
// a device. Creating a `VkompContext` using a device which doesn't support compute
// shaders will return an error.
typedef struct {
  VkPhysicalDeviceProperties properties;
  VkPhysicalDevice           dev_phy;
  int                        compute_queue_family;
} VkompDeviceInfo;

// Count the total number of vulkan devices.
int vkomp_devices_count(VkInstance instance, uint32_t* device_count);
// Enumerate all vulkan devices. The `devices` array must have enough space for
// at least `devices_count` device info structures.
int vkomp_devices_enumerate(
  VkInstance instance,
  uint32_t device_count,
  VkompDeviceInfo* devices
);
// Find the "best" device of a set, using max compute memory
// as a rough proxy for performance. Vulkan doesn't give us a way to
// get a count of processing cores or clock speed etc, so this is
// only a rough guess. Returns -1 if devices_len is zero.
int vkomp_find_best_device(VkompDeviceInfo* devices, uint32_t devices_len);


// A context which encapsulates the runtime state of libvkomp.
typedef struct {
  VkPhysicalDevice dev_phy;
  VkDevice         device;
  uint32_t         queue_family_index;
  VkCommandPool    cmd_pool;
} VkompContext;

void vkomp_context_free(VkompContext ctx);
int vkomp_context_init(VkompDeviceInfo device_info, VkompContext* ctx);

// Describes what type of memory is to be used for this buffer.
typedef enum VkompBufferType {
  // Host accessible memory. Writing data to this will be much slower, but the CPU
  // can easily access it, so this is suitable for small output buffers.
  VKOMP_BUFFER_TYPE_HOST = 1,

  // Device local memory. This will be MUCH faster when writing to, so it should
  // be the default for intermediate buffers on the GPU, in cases when the CPU
  // doesn't need to read from a buffer.
  VKOMP_BUFFER_TYPE_DEVICE = 2,
} VkompBufferType;

// A buffer with memory backing.
typedef struct {
  VkDeviceMemory memory;
  VkBuffer buffer;
  size_t size;
  VkompBufferType buf_type;
  bool is_host_visible;
  bool is_device_local;
} VkompBuffer;

// Free a buffer and its associated device memory.
void vkomp_buffer_free(VkompContext ctx, VkompBuffer compbuf);
// Initialize and allocate vulkan device memory for a buffer of a given size.
int vkomp_buffer_init(
  VkompContext ctx,
  size_t size,
  VkompBufferType buf_type,
  VkompBuffer* compbuf
);
// Map or unmap the buffer memory, so that we can read from it on the CPU.
// These funcs returns an error if the compute buffer is not host-visible.
int vkomp_buffer_map(VkompContext ctx, VkompBuffer compbuf, void** mapped);
void vkomp_buffer_unmap(VkompContext ctx, VkompBuffer compbuf);

// A buffer-to-buffer copy operation. Used to move data between host (CPU)
// and device (GPU) local memory regions.
typedef struct {
  VkompBuffer* src;
  VkompBuffer* dest;
} VkompBufferCopyOp;

// A user-supplied structure describing a compute shader.
typedef struct {
  VkompBuffer*       compute_buffers;
  uint32_t           compute_buffers_len;
  VkompBufferCopyOp* copy_ops;
  uint32_t           copy_ops_len;
  const void*        push_constants;
  size_t             push_constants_size;
  const void* const* specialization_constants;
  const size_t*      specialization_constants_sizes;
  uint32_t           specialization_constants_len;
  const uint8_t*     shader_spv;
  size_t             shader_spv_len;
  uint32_t           work_group_count;
} VkompFlowStage;

// The compiled static resources of a compute shader. This can be reused across multiple
// dispatches of the same shader.
typedef struct {
  VkPipelineLayout pipeline_layout;
  VkShaderModule shader;
  VkDescriptorSetLayout descriptor_set_layout;
  VkPipeline pipeline;
} VkompFlowStageCompiled;

// The execution-time resources of a compute shader. Can be reused.
typedef struct {
  VkCommandBuffer   cmd_buf;
  VkDescriptorSet   descriptor_set;
  VkEvent           done_event;
} VkompFlowStageExecutionResources;

// A structure which holds runtime state for a pipeline of one or more
// vulkan compute shaders. Each 'stage' in the pipeline is represented
// by a `VkompFlowStage`, and its shader executes only after the prior
// stage has completed.
typedef struct {
  uint32_t                          stages_len;
  VkompFlowStage*                   stages;
  VkompFlowStageCompiled*           stages_compiled;
  VkompFlowStageExecutionResources* stages_resources;
  VkDescriptorPool                  descriptor_pool;
} VkompFlow;

// Free the resources for this flow. No more shader executions can be
// run after the flow is freed.
void vkomp_flow_free(VkompContext ctx, VkompFlow flow);
// Initialize and compile shaders for a given set of stages, preparing
// everything in a `VkompFlow`, ready to run.
int vkomp_flow_init(
  VkompContext ctx,
  VkompFlowStage* stages,
  uint32_t stages_len,
  VkompFlow* flow
);
// Execute the shader pipeline, completing each shader stage in turn.
// Synchronous. Waits until the final shader stage completes before returning.
int vkomp_flow_run(VkompContext ctx, VkompFlow flow);
