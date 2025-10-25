#include <stdlib.h>
#include <vulkan/vulkan.h>

#include "vkomp.h"
#include "utils.h"

int vkomp_devices_count(VkInstance instance, uint32_t* device_count) {
  return vkEnumeratePhysicalDevices(instance, device_count, NULL);
}


int vkomp_devices_enumerate(
  VkInstance instance,
  uint32_t device_count,
  VkompDeviceInfo* devices
) {
  VkPhysicalDevice* physical_devices = malloc(device_count * sizeof(VkPhysicalDevice));

  int err = vkEnumeratePhysicalDevices(instance, &device_count, physical_devices);
  if (err) goto cleanup;

  for (uint32_t i = 0; i < device_count; i++) {
    VkPhysicalDeviceProperties device_props;
    vkGetPhysicalDeviceProperties(physical_devices[i], &device_props);

    devices[i] = (VkompDeviceInfo) {
      .properties = device_props,
      .dev_phy = physical_devices[i],
      .compute_queue_family = vulkan_find_compute_queue_family(physical_devices[i]),
    };
  }

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
