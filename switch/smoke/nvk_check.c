/*
 * Copyright © 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * vulkaninfo --summary equivalent for Switch/NVK on Maxwell GM20B.
 * Logs to sdmc:/nvk_check.log and sdmc:/nvk_mesa.log (Mesa diagnostics).
 *
 * Build via: make nro APP=nvk_check  (inside Docker image)
 * Output: switch/smoke/out/nvk_check.nro
 */

#include "nvk_harness.h"

static void log_instance_extensions(void)
{
   PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties =
      (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
         NULL, "vkEnumerateInstanceExtensionProperties");
   PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties =
      (PFN_vkEnumerateInstanceLayerProperties)vk_icdGetInstanceProcAddr(
         NULL, "vkEnumerateInstanceLayerProperties");

   if (EnumerateInstanceExtensionProperties) {
      uint32_t count = 0;
      VkResult r = EnumerateInstanceExtensionProperties(NULL, &count, NULL);
      LOG("instance extensions: vkEnumerateInstanceExtensionProperties -> %d count=%u", r, count);
      if (r == VK_SUCCESS && count > 0) {
         VkExtensionProperties *props = malloc(count * sizeof(*props));
         if (props) {
            r = EnumerateInstanceExtensionProperties(NULL, &count, props);
            for (uint32_t i = 0; i < count; i++) {
               LOG("  [inst ext %2u] %s spec %u", i, props[i].extensionName, props[i].specVersion);
            }
            free(props);
         }
      }
   } else {
      LOG("instance extensions: vkEnumerateInstanceExtensionProperties not found");
   }

   if (EnumerateInstanceLayerProperties) {
      uint32_t count = 0;
      VkResult r = EnumerateInstanceLayerProperties(&count, NULL);
      LOG("instance layers: vkEnumerateInstanceLayerProperties -> %d count=%u", r, count);
      if (r == VK_SUCCESS && count > 0) {
         VkLayerProperties *props = malloc(count * sizeof(*props));
         if (props) {
            r = EnumerateInstanceLayerProperties(&count, props);
            for (uint32_t i = 0; i < count; i++) {
               LOG("  [layer %2u] %s spec %u impl %u : %s", i,
                   props[i].layerName, props[i].specVersion,
                   props[i].implementationVersion, props[i].description);
            }
            free(props);
         }
      }
   }
}

static void log_device_extensions(struct nvk_ctx *c)
{
   LOAD_INST(c, EnumerateDeviceExtensionProperties);
   if (!EnumerateDeviceExtensionProperties) {
      LOG("device extensions: vkEnumerateDeviceExtensionProperties not found");
      return;
   }
   uint32_t count = 0;
   VkResult r = EnumerateDeviceExtensionProperties(c->phys, NULL, &count, NULL);
   LOG("device extensions: vkEnumerateDeviceExtensionProperties -> %d count=%u", r, count);
   if (r != VK_SUCCESS || count == 0)
      return;

   VkExtensionProperties *props = malloc(count * sizeof(*props));
   if (!props) {
      LOG("  OOM for %u device extensions", count);
      return;
   }
   r = EnumerateDeviceExtensionProperties(c->phys, NULL, &count, props);
   if (r != VK_SUCCESS) {
      LOG("  second call failed: %d", r);
      free(props);
      return;
   }

   /* Sort alphabetically for stable diffs, like vulkaninfo */
   for (uint32_t i = 0; i < count; i++) {
      for (uint32_t j = i + 1; j < count; j++) {
         if (strcmp(props[i].extensionName, props[j].extensionName) > 0) {
            VkExtensionProperties tmp = props[i];
            props[i] = props[j];
            props[j] = tmp;
         }
      }
   }

   for (uint32_t i = 0; i < count; i++) {
      LOG("  %s (spec %u)", props[i].extensionName, props[i].specVersion);
   }

   /* Quick grep equivalent the user asked for */
   LOG("--- VK_EXT_* ---");
   for (uint32_t i = 0; i < count; i++) {
      if (strncmp(props[i].extensionName, "VK_EXT_", 7) == 0)
         LOG("  %s", props[i].extensionName);
   }
   LOG("--- VK_KHR_* ---");
   for (uint32_t i = 0; i < count; i++) {
      if (strncmp(props[i].extensionName, "VK_KHR_", 7) == 0)
         LOG("  %s", props[i].extensionName);
   }

   free(props);
}

static void log_queue_families(struct nvk_ctx *c)
{
   LOAD_INST(c, GetPhysicalDeviceQueueFamilyProperties);
   if (!GetPhysicalDeviceQueueFamilyProperties) {
      LOG("queue families: GetPhysicalDeviceQueueFamilyProperties not found");
      return;
   }
   uint32_t count = 0;
   GetPhysicalDeviceQueueFamilyProperties(c->phys, &count, NULL);
   LOG("queue families: count=%u", count);
   if (count == 0)
      return;
   VkQueueFamilyProperties *props = malloc(count * sizeof(*props));
   if (!props)
      return;
   GetPhysicalDeviceQueueFamilyProperties(c->phys, &count, props);
   for (uint32_t i = 0; i < count; i++) {
      LOG("  family %u: flags=0x%x count=%u timestampValidBits=%u minImageTransferGranularity=%ux%ux%u",
          i, props[i].queueFlags, props[i].queueCount,
          props[i].timestampValidBits,
          props[i].minImageTransferGranularity.width,
          props[i].minImageTransferGranularity.height,
          props[i].minImageTransferGranularity.depth);
   }
   free(props);
}

static void log_memory(struct nvk_ctx *c)
{
   LOG("memory heaps: %u", c->memp.memoryHeapCount);
   for (uint32_t i = 0; i < c->memp.memoryHeapCount; i++) {
      LOG("  heap %u: size %llu MB flags 0x%x",
          i, (unsigned long long)(c->memp.memoryHeaps[i].size >> 20),
          c->memp.memoryHeaps[i].flags);
   }
   LOG("memory types: %u", c->memp.memoryTypeCount);
   for (uint32_t i = 0; i < c->memp.memoryTypeCount; i++) {
      LOG("  type %u: heap %u flags 0x%x (HOST_VISIBLE=%d HOST_COHERENT=%d DEVICE_LOCAL=%d)",
          i, c->memp.memoryTypes[i].heapIndex,
          c->memp.memoryTypes[i].propertyFlags,
          !!(c->memp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
          !!(c->memp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
          !!(c->memp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
   }
}

static void log_device_props2(struct nvk_ctx *c)
{
   LOAD_INST(c, GetPhysicalDeviceProperties2);
   if (!GetPhysicalDeviceProperties2) {
      LOG("VkPhysicalDeviceProperties2: not available (need Vulkan 1.1+)");
      return;
   }

   VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   /* Chain driver properties if available */
   VkPhysicalDeviceDriverProperties driverProps = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
   };
   props2.pNext = &driverProps;

   GetPhysicalDeviceProperties2(c->phys, &props2);
   LOG("VkPhysicalDeviceProperties2:");
   LOG("  apiVersion=%u.%u.%u (%u) driverVersion=%u (0x%x)",
       VK_VERSION_MAJOR(props2.properties.apiVersion),
       VK_VERSION_MINOR(props2.properties.apiVersion),
       VK_VERSION_PATCH(props2.properties.apiVersion),
       props2.properties.apiVersion,
       props2.properties.driverVersion, props2.properties.driverVersion);
   LOG("  vendorID=0x%x deviceID=0x%x deviceType=%u deviceName='%s'",
       props2.properties.vendorID, props2.properties.deviceID,
       props2.properties.deviceType, props2.properties.deviceName);
   LOG("  limits: maxImageDimension2D=%u maxBoundDescriptorSets=%u maxMemoryAllocationCount=%u",
       props2.properties.limits.maxImageDimension2D,
       props2.properties.limits.maxBoundDescriptorSets,
       props2.properties.limits.maxMemoryAllocationCount);

   /* Driver ID is useful to confirm NVK */
   if (driverProps.driverID != 0) {
      LOG("  driverID=%u driverName='%s' driverInfo='%s' conformanceVersion=%u.%u.%u.%u",
          driverProps.driverID, driverProps.driverName, driverProps.driverInfo,
          driverProps.conformanceVersion.major, driverProps.conformanceVersion.minor,
          driverProps.conformanceVersion.subminor, driverProps.conformanceVersion.patch);
   }
}

static void log_device_features2(struct nvk_ctx *c)
{
   LOAD_INST(c, GetPhysicalDeviceFeatures2);
   if (!GetPhysicalDeviceFeatures2) {
      LOG("VkPhysicalDeviceFeatures2: not available");
      return;
   }
   VkPhysicalDeviceFeatures2 feats2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   GetPhysicalDeviceFeatures2(c->phys, &feats2);
   LOG("VkPhysicalDeviceFeatures2:");
   LOG("  robustBufferAccess=%d fullDrawIndexUint32=%d imageCubeArray=%d independentBlend=%d",
       feats2.features.robustBufferAccess, feats2.features.fullDrawIndexUint32,
       feats2.features.imageCubeArray, feats2.features.independentBlend);
   LOG("  geometryShader=%d tessellationShader=%d sampleRateShading=%d dualSrcBlend=%d",
       feats2.features.geometryShader, feats2.features.tessellationShader,
       feats2.features.sampleRateShading, feats2.features.dualSrcBlend);
   LOG("  fillModeNonSolid=%d wideLines=%d largePoints=%d alphaToOne=%d multiViewport=%d",
       feats2.features.fillModeNonSolid, feats2.features.wideLines,
       feats2.features.largePoints, feats2.features.alphaToOne,
       feats2.features.multiViewport);
}

int main(void)
{
   nvk_log_open("sdmc:/nvk_check.log");
   LOG("=== nvk_check ===");
   LOG("Maxwell GM20B (Tegra X1) capability probe");
   LOG("Harness: NVK_I_WANT_A_BROKEN_VULKAN_DRIVER=1, VK 1.3 instance, debug messenger on");

   /* Instance-level checks before device bringup */
   log_instance_extensions();

   struct nvk_ctx c;
   VkResult r = nvk_bringup(&c, NULL, 0);
   if (r != VK_SUCCESS) {
      LOG("FAIL: nvk_bringup -> %d", r);
      LOG("=== nvk_check FAILED at bringup ===");
      goto done;
   }

   /* Summary like vulkaninfo --summary */
   LOG("--- summary ---");
   LOG("deviceName='%s'", c.props.deviceName);
   LOG("deviceType=%u vendorID=0x%04x deviceID=0x%04x",
       c.props.deviceType, c.props.vendorID, c.props.deviceID);
   LOG("apiVersion=%u.%u.%u (%u) driverVersion=%u (0x%x)",
       VK_VERSION_MAJOR(c.props.apiVersion), VK_VERSION_MINOR(c.props.apiVersion),
       VK_VERSION_PATCH(c.props.apiVersion), c.props.apiVersion,
       c.props.driverVersion, c.props.driverVersion);
   LOG("graphics queue family=%u", c.qfi);

   log_device_props2(&c);
   log_device_features2(&c);
   log_queue_families(&c);
   log_memory(&c);
   log_device_extensions(&c);

   LOG("=== nvk_check PASSED ===");
   nvk_teardown(&c);

done:
   if (g_nvk_log) fclose(g_nvk_log);
   return 0;
}
