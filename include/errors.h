#pragma once
const char* vkomp_stringify_error_code(int code);

typedef enum VkompError {
  // Indicates the caller tried to initialize a `VkompContext` using a device
  // with no valid compute queue family.
  VKOMP_ERROR_DEVICE_CANNOT_COMPUTE = 32,

  // Indicates the caller passed an unknown value for `VkompBufferType`, which
  // is not an enum member.
  VKOMP_ERROR_INVALID_BUFFER_TYPE = 33,

  // Indicates we cannot find the requested memory type (host-visible, device local, etc).
  VKOMP_ERROR_MEMORY_TYPE_NOT_FOUND = 34,
} VkompError;
