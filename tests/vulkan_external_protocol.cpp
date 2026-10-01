#include "ffg/ffg_vk.h"

#include <cstdint>

namespace {
FgdsHdrMetadata hdr() {
  FgdsHdrMetadata value{};
  value.structSize = sizeof(FgdsHdrMetadata);
  value.version = FGDS_HDR_VERSION_1;
  value.colorSpace = FGDS_HDR_COLOR_SPACE_HDR10_PQ;
  value.transferFunction = FGDS_HDR_TRANSFER_PQ;
  value.flags = FGDS_HDR_FLAG_STATIC_METADATA;
  value.primaries[0] = 0.68f;
  value.primaries[1] = 0.32f;
  value.primaries[2] = 0.265f;
  value.primaries[3] = 0.69f;
  value.primaries[4] = 0.15f;
  value.primaries[5] = 0.06f;
  value.whitePoint[0] = 0.3127f;
  value.whitePoint[1] = 0.3290f;
  value.maxMasteringLuminanceNits = 1000.0f;
  value.minMasteringLuminanceNits = 0.05f;
  value.nominalPeakLuminanceNits = 1000.0f;
  return value;
}

FgdsVkExternalImage image() {
  FgdsVkExternalImage value{};
  value.structSize = sizeof(FgdsVkExternalImage);
  value.version = FGDS_VK_EXTERNAL_VERSION_1;
  value.image.structSize = sizeof(FgdsSharedImage);
  value.image.version = FGDS_SHARED_VERSION_1;
  value.image.backend = FGDS_SHARED_BACKEND_VULKAN;
  value.image.format = FGDS_VK_FORMAT_RGBA32_FLOAT;
  value.image.width = 1280;
  value.image.height = 720;
  value.image.arrayLayers = 1;
  value.image.mipLevels = 1;
  value.image.sampleCount = 1;
  value.image.flags = FGDS_SHARED_IMAGE_SHADER_RESOURCE;
  value.image.handleType = FGDS_SHARED_HANDLE_WIN32;
  value.image.adapterLuid = 0x0000002100000042ull;
  value.image.resourceHandle = 0x1234;
  value.imageLayout = FGDS_VK_EXTERNAL_LAYOUT_GENERAL;
  value.queueFamily = 2;
  return value;
}

FgdsVkExternalSync sync() {
  FgdsVkExternalSync value{};
  value.structSize = sizeof(FgdsVkExternalSync);
  value.version = FGDS_VK_EXTERNAL_VERSION_1;
  value.sync.structSize = sizeof(FgdsSharedSync);
  value.sync.version = FGDS_SHARED_VERSION_1;
  value.sync.backend = FGDS_SHARED_BACKEND_VULKAN;
  value.sync.type = FGDS_SHARED_SYNC_VULKAN_TIMELINE;
  value.sync.handleType = FGDS_SHARED_HANDLE_WIN32;
  value.sync.adapterLuid = 0x0000002100000042ull;
  value.sync.fenceHandle = 0x5678;
  value.sync.value = 9;
  value.semaphoreType = FGDS_VK_EXTERNAL_SEMAPHORE_TIMELINE;
  return value;
}
}  // namespace

int main() {
  FgdsHdrMetadata metadata = hdr();
  if (ffgVkValidateHdrMetadata(&metadata) != VK_SUCCESS)
    return 1;
  FgdsVkExternalImage externalImage = image();
  if (ffgVkValidateExternalImageV1(&externalImage) != VK_SUCCESS)
    return 2;
  FgdsVkExternalSync externalSync = sync();
  if (ffgVkValidateExternalSyncV1(&externalSync) != VK_SUCCESS)
    return 3;
  externalImage.imageLayout = 0;
  if (ffgVkValidateExternalImageV1(&externalImage) != VK_ERROR_VALIDATION_FAILED_EXT)
    return 4;
  return 0;
}
