#include <stdlib.h>
#include <stdio.h>
#include <vkomp.h>

#include "utils.h"

#define ERR_NO_USABLE_DEVICE 10002;

int main() {
  init_test();
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
    eprintf("failed to count vulkan devices\n");
    goto cleanup;
  }

  devices = malloc(devices_count * sizeof(VkompDeviceInfo));
  err = vkomp_devices_enumerate(instance, devices_count, devices);
  if (err) {
    eprintf("failed to enumerate vulkan devices\n");
    goto cleanup;
  }

  int best_device_index = vkomp_find_best_device(devices, devices_count);
  if (best_device_index < 0) {
    err = ERR_NO_USABLE_DEVICE;
    eprintf("no vulkan devices available\n");
    goto cleanup;
  }

  for (int i = 0; i < (int) devices_count; i++) {
    printf(
      "found device %u: %s%s\n",
      i,
      devices[i].properties.deviceName,
      (i == best_device_index ? " (best)" : "")
    );
  }


cleanup:
  free(devices);
  vkDestroyInstance(instance, NULL);
  return err;
}
