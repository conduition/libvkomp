#pragma once
#include <stdbool.h>
#include <vulkan/vulkan.h>

// Represents a vulkan device that supports compute shaders.
typedef struct {
  VkPhysicalDeviceProperties properties;
  VkPhysicalDevice           dev_phy;
  uint32_t                   compute_queue_family;
  // set to zero unless the VkInstance was created with `apiVersion >= VK_API_VERSION_1_1`.
  VkPhysicalDeviceVulkan11Properties properties_vk11;
} VkompDeviceInfo;

// Count the total number of vulkan devices.
int vkomp_devices_count(VkInstance instance, uint32_t* device_count);
// Enumerate all vulkan devices. The `devices` array must have enough space for
// at least `devices_count` device info structures. On success, `device_count` will
// be overwritten with the actual number of written devices, which may be less than
// the value returned by `vkomp_devices_count` as some devices might not support
// compute shaders.
int vkomp_devices_enumerate(
  VkInstance instance,
  uint32_t* device_count,
  VkompDeviceInfo* devices
);
// Find the "best" device of a set, using max compute memory
// as a rough proxy for performance. Vulkan doesn't give us a way to
// get a count of processing cores or clock speed etc, so this is
// only a rough guess. Returns -1 if devices_len is zero.
int vkomp_find_best_device(VkompDeviceInfo* devices, uint32_t devices_len);

// Enumerate all devices and find the best usable compute devices (using compute
// memory as a metric). A useful shortcut for those in a hurry to get any device or
// find the best available GPU.
int vkomp_get_best_device(VkInstance instance, VkompDeviceInfo* device);
int vkomp_get_best_gpu(VkInstance instance, VkompDeviceInfo* device);

// A context which encapsulates the runtime state of libvkomp.
typedef struct {
  VkDevice          device;
  VkompDeviceInfo*  device_info;
  VkCommandPool     cmd_pool;
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
// and device (GPU) local memory regions. The copy will try to copy as much of
// `src` into `dest` as it can without overflowing the length of `dest`.
// If `before_shader` is false, the copy will be executed after the stage's
// shader has run, but before the next compute flow stage begins.
// If `before_shader` is true, the copy will be executed before the stages's
// shader has run.
typedef struct {
  VkompBuffer* src;
  VkompBuffer* dest;
  bool         before_shader;
  size_t       size;
  size_t       src_offset;
  size_t       dest_offset;
} VkompBufferCopyOp;

// A user-supplied structure describing a compute shader, its buffers for I/O, and
// extra options such as buffer-to-buffer copy operations or runtime constants.
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

// Print convert a vulkan or libvkomp error code into a string.
// Always returns a non-null pointer.
const char* vkomp_stringify_error_code(int code);

// This error type extends the `VkResult` type. The error codes do not conflict with
// Vulkan's, and are also small enough to fit in an 8-bit unix process exit code.
typedef enum VkompError {
  // Indicates the caller passed an unknown value for `VkompBufferType`, which
  // is not an enum member.
  VKOMP_ERROR_INVALID_BUFFER_TYPE = 32,

  // Indicates we cannot find the requested memory type (host-visible, device local, etc).
  VKOMP_ERROR_MEMORY_TYPE_NOT_FOUND = 33,

  // Indicates Vkomp could not fetch a requested device.
  VKOMP_ERROR_DEVICE_NOT_FOUND = 34,

  // The user tried to map a buffer which is not host-visible.
  VKOMP_ERROR_BUFFER_NOT_HOST_VISIBLE = 35,

  // The user tried to copy between Vulkan buffers outside of their size bounds.
  VKOMP_ERROR_COPY_OP_OUT_OF_BOUNDS = 36,
} VkompError;
