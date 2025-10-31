#include <stdlib.h>
#include <stdio.h>
#include <vkomp.h>

#include "utils.h"

int main() {
  init_test();
  VkInstance       instance = NULL;
  VkompDeviceInfo* devices  = NULL;

  VkApplicationInfo app_info = {
    .apiVersion = VK_API_VERSION_1_1,
  };
  VkInstanceCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
  };
  int err = vkCreateInstance(&create_info, NULL, &instance);
  if (err) {
    eprintf("error creating vulkan instance\n");
    return err;
  }

  uint32_t devices_count;
  err = vkomp_devices_count(instance, &devices_count);
  if (err) {
    eprintf("failed to count vulkan devices\n");
    goto cleanup;
  }

  devices = malloc(devices_count * sizeof(VkompDeviceInfo));
  err = vkomp_devices_enumerate(instance, &devices_count, devices);
  if (err) {
    eprintf("failed to enumerate vulkan devices\n");
    goto cleanup;
  }

  int best_device_index = vkomp_find_best_device(devices, devices_count);
  if (best_device_index < 0) {
    err = VKOMP_ERROR_DEVICE_NOT_FOUND;
    eprintf("no vulkan devices available\n");
    goto cleanup;
  }

  for (int i = 0; i < (int) devices_count; i++) {
    printf(
      "found device %u: %s warp_size=%u%s\n",
      i,
      devices[i].properties.deviceName,
      devices[i].properties_vk11.subgroupSize,
      (i == best_device_index ? " (best)" : "")
    );
  }


cleanup:
  free(devices);
  vkDestroyInstance(instance, NULL);
  return err;
}
