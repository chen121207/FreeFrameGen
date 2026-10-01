#pragma once
#include <d3d12.h>
#include "fgds/fgds.h"
#ifdef FFG_BUILD
#define FFG_API __declspec(dllexport)
#else
#define FFG_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct FfgContext FfgContext;
    FFG_API HRESULT __cdecl ffgCreate(ID3D12Device *device, FfgContext **output);
    FFG_API void __cdecl ffgDestroy(FfgContext *context);
    // Returns the native input contract supported by this context.  The
    // caller must set capabilities->structSize before calling; fields beyond
    // the current struct are left untouched for forward compatibility.
    FFG_API HRESULT __cdecl ffgGetCapabilities(FfgContext *context,
                                                FgdsCapabilities *capabilities);
    // Caller owns resources/list, transitions inputs to NON_PIXEL_SHADER_RESOURCE,
    // output to UNORDERED_ACCESS, and waits for GPU completion before reusing or
    // destroying this single-flight context. Output remains UAV; heap/root state
    // is clobbered. Does not submit, wait, allocate per frame or Present.
    FFG_API HRESULT __cdecl ffgRecord(FfgContext *, ID3D12GraphicsCommandList *, const FgdsPair *,
                                      ID3D12Resource *output);
    // v0.2 append-only contract.  The v0.1 ffgRecord ABI and validation are
    // intentionally unchanged.  Core resources are required; optional masks
    // are parsed but return E_NOTIMPL until a mask-aware reconstruction kernel
    // is enabled.  This makes unsupported features explicit to the game.
    FFG_API HRESULT __cdecl ffgRecordV2(FfgContext *, ID3D12GraphicsCommandList *,
                                        const FgdsPairV2 *, ID3D12Resource *output);

    // v0.3 multi-flight API. The legacy context above has one descriptor heap
    // and remains single-flight. A V3 context allocates one independent heap
    // per slot so records on different command lists cannot overwrite each
    // other's descriptors. slotCount must be in [1, FGDS_V3_MAX_SLOTS].
    typedef struct FfgContextV3 FfgContextV3;
    FFG_API HRESULT __cdecl ffgCreateV3(ID3D12Device *device, uint32_t slotCount,
                                        FfgContextV3 **output);
    FFG_API void __cdecl ffgDestroyV3(FfgContextV3 *context);
    FFG_API HRESULT __cdecl ffgGetCapabilitiesV3(FfgContextV3 *context,
                                                  FgdsCapabilitiesV3 *capabilities);
    // completionFence/completionValue name the fence value that will be
    // signalled after this command list executes. Both are required on every
    // call, including first use. The API does not signal, submit or wait.
    // Before reusing a slot, its previous fence value must be complete;
    // otherwise DXGI_ERROR_WAS_STILL_DRAWING is returned and no descriptors
    // or commands are written. completionValue must also be greater than the
    // fence's current completed value, or E_INVALIDARG is returned.
    FFG_API HRESULT __cdecl ffgRecordV3(FfgContextV3 *, ID3D12GraphicsCommandList *,
                                        const FgdsPairV2 *, ID3D12Resource *output,
                                        uint32_t slotIndex, ID3D12Fence *completionFence,
                                        uint64_t completionValue);

    // v0.4 transport helpers.  These entries are additive; the frozen v0.1
    // through v0.3 ABI and entry points above are unchanged.  Validation is
    // side-effect free and can be used before opening any OS handles.
    FFG_API HRESULT __cdecl ffgValidateHdrMetadata(const FgdsHdrMetadata *metadata);
    FFG_API HRESULT __cdecl ffgValidateSharedPairV1(const FgdsSharedPair *pair);

    // Imports D3D12 shared resources/fences, schedules queue waits for both
    // producer ready values, and records the same compute kernel as V3.  It
    // does not submit or signal the command list.  The host must signal
    // pair->retire.value on pair->retire.fenceHandle after ExecuteCommandLists.
    // Imported COM objects are retained by the selected slot until its retire
    // fence reaches the supplied value.  A matching adapter LUID and Win32/NT
    // shared-handle type are required.
    FFG_API HRESULT __cdecl ffgRecordSharedV1(FfgContextV3 *context,
                                               ID3D12CommandQueue *queue,
                                               ID3D12GraphicsCommandList *list,
                                               const FgdsSharedPair *pair,
                                               uint32_t slotIndex);
#ifdef __cplusplus
}
#endif
