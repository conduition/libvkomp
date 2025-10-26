#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "vkomp.h"
#include "utils.h"

int vkomp_devices_count(VkInstance instance, uint32_t* device_count) {
  return vkEnumeratePhysicalDevices(instance, device_count, NULL);
}


int vkomp_devices_enumerate(
  VkInstance instance,
  uint32_t* device_count,
  VkompDeviceInfo* devices
) {
  VkPhysicalDevice* physical_devices = malloc(*device_count * sizeof(VkPhysicalDevice));

  int err = vkEnumeratePhysicalDevices(instance, device_count, physical_devices);
  if (err) goto cleanup;

  uint32_t devices_written = 0;
  for (uint32_t i = 0; i < *device_count; i++) {
    int compute_queue_family = _vkomp_intern_find_compute_queue_family(physical_devices[i]);
    if (compute_queue_family < 0) continue;

    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(physical_devices[i], &device_props);

    devices[devices_written++] = (VkompDeviceInfo) {
      .properties = device_props,
      .dev_phy = physical_devices[i],
      .compute_queue_family = (uint32_t) compute_queue_family,
    };
  }

  *device_count = devices_written;

cleanup:
  free(physical_devices);
  return err;
}


int vkomp_find_best_device(VkompDeviceInfo* devices, uint32_t devices_len) {
  uint32_t best_max_memory = 0;
  int best_device_idx = -1;
  for (int i = 0; i < (int) devices_len; i++) {
    if (devices[i].properties.limits.maxComputeSharedMemorySize > best_max_memory) {
      best_max_memory = devices[i].properties.limits.maxComputeSharedMemorySize;
      best_device_idx = i;
    }
  }
  return best_device_idx;
}


int vkomp_get_best_gpu(VkInstance instance, VkompDeviceInfo* device) {
  uint32_t device_count;
  int err = vkomp_devices_count(instance, &device_count);
  if (err) return err;

  VkompDeviceInfo* devices = malloc(device_count * sizeof(VkompDeviceInfo));
  err = vkomp_devices_enumerate(instance, &device_count, devices);
  if (err) goto cleanup;

  // Sort all the GPUs to the front of the `devices` array.
  uint32_t gpu_count = 0;
  for (uint32_t i = 0; i < device_count; i++) {
    switch (devices[i].properties.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        devices[gpu_count++] = devices[i];
      default:
        continue;
    }
  }

  int best_gpu = vkomp_find_best_device(devices, gpu_count);
  if (best_gpu < 0) {
    err = VKOMP_ERROR_DEVICE_NOT_FOUND;
    goto cleanup;
  }

  *device = devices[best_gpu];

cleanup:
  free(devices);
  return err;
}
