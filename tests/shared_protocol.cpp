#include "ffg/ffg.h"

#include <cmath>
#include <cstdint>

namespace {
FgdsHdrMetadata hdr() {
  FgdsHdrMetadata value{};
  value.structSize = sizeof(FgdsHdrMetadata);
  value.version = FGDS_HDR_VERSION_1;
  value.colorSpace = FGDS_HDR_COLOR_SPACE_HDR10_PQ;
  value.transferFunction = FGDS_HDR_TRANSFER_PQ;
  value.flags = FGDS_HDR_FLAG_STATIC_METADATA | FGDS_HDR_FLAG_DISPLAY_TARGET;
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
  value.maxContentLightLevelNits = 1000.0f;
  value.maxFrameAverageLightLevelNits = 400.0f;
  value.nominalPeakLuminanceNits = 1000.0f;
  return value;
}

FgdsSharedImage image(uint32_t format, uint32_t flags, uint64_t handle) {
  FgdsSharedImage value{};
  value.structSize = sizeof(FgdsSharedImage);
  value.version = FGDS_SHARED_VERSION_1;
  value.backend = FGDS_SHARED_BACKEND_D3D12;
  value.format = format;
  value.width = 1920;
  value.height = 1080;
  value.arrayLayers = 1;
  value.mipLevels = 1;
  value.sampleCount = 1;
  value.flags = flags;
  value.handleType = FGDS_SHARED_HANDLE_WIN32;
  value.adapterLuid = 0x0000001200000034ull;
  value.resourceHandle = handle;
  return value;
}

FgdsSharedSync sync(uint64_t handle, uint64_t value) {
  FgdsSharedSync result{};
  result.structSize = sizeof(FgdsSharedSync);
  result.version = FGDS_SHARED_VERSION_1;
  result.backend = FGDS_SHARED_BACKEND_D3D12;
  result.type = FGDS_SHARED_SYNC_D3D12_FENCE;
  result.handleType = FGDS_SHARED_HANDLE_WIN32;
  result.adapterLuid = 0x0000001200000034ull;
  result.fenceHandle = handle;
  result.value = value;
  return result;
}

FgdsSharedPair validPair() {
  FgdsSharedPair pair{};
  pair.structSize = sizeof(FgdsSharedPair);
  pair.version = FGDS_SHARED_VERSION_1;
  pair.alpha = 0.5f;
  pair.outputHdr = hdr();
  for (uint32_t side = 0; side < 2; ++side) {
    auto& frame = pair.frames[side];
    frame.structSize = sizeof(FgdsSharedFrame);
    frame.version = FGDS_SHARED_VERSION_1;
    frame.width = 1920;
    frame.height = 1080;
    frame.frameId = 10 + side;
    frame.timestampNs = 1000 + side;
    frame.worldToClip[0] = frame.worldToClip[5] = frame.worldToClip[10] = frame.worldToClip[15] = 1.0f;
    frame.resourceFlags = FGDS_RESOURCE_CORE;
    frame.color = image(FGDS_FORMAT_RGBA32_FLOAT, FGDS_SHARED_IMAGE_SHADER_RESOURCE, 0x100 + side * 10);
    frame.depth = image(FGDS_FORMAT_R32_FLOAT, FGDS_SHARED_IMAGE_SHADER_RESOURCE, 0x101 + side * 10);
    frame.motionToOther = image(FGDS_FORMAT_RG32_FLOAT, FGDS_SHARED_IMAGE_SHADER_RESOURCE, 0x102 + side * 10);
    frame.objectId = image(FGDS_FORMAT_R32_UINT, FGDS_SHARED_IMAGE_SHADER_RESOURCE, 0x103 + side * 10);
    frame.ready = sync(0x200 + side, 20 + side);
    frame.hdr = pair.outputHdr;
  }
  pair.output = image(FGDS_FORMAT_RGBA32_FLOAT, FGDS_SHARED_IMAGE_UNORDERED_ACCESS, 0x300);
  pair.retire = sync(0x400, 30);
  return pair;
}
}  // namespace

int main() {
  if (ffgValidateHdrMetadata(nullptr) != E_POINTER)
    return 1;
  FgdsHdrMetadata metadata = hdr();
  if (ffgValidateHdrMetadata(&metadata) != S_OK)
    return 2;

  FgdsSharedPair pair = validPair();
  if (ffgValidateSharedPairV1(&pair) != S_OK)
    return 3;

  pair.frames[1].ready.value = 0;
  if (ffgValidateSharedPairV1(&pair) != E_INVALIDARG)
    return 4;
  pair = validPair();
  pair.frames[0].resourceFlags |= FGDS_RESOURCE_HUD_MASK;
  if (ffgValidateSharedPairV1(&pair) != E_NOTIMPL)
    return 5;
  pair = validPair();
  pair.outputHdr.nominalPeakLuminanceNits = 0.0f;
  if (ffgValidateSharedPairV1(&pair) != E_INVALIDARG)
    return 6;
  pair = validPair();
  pair.frames[1].color.adapterLuid ^= 1ull;
  if (ffgValidateSharedPairV1(&pair) != E_INVALIDARG)
    return 7;
  pair = validPair();
  pair.output.handleType = FGDS_SHARED_HANDLE_OPAQUE_FD;
  if (ffgValidateSharedPairV1(&pair) != E_INVALIDARG)
    return 8;
  return 0;
}
