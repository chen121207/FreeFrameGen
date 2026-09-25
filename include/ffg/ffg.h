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
    // Caller owns resources/list, transitions inputs to NON_PIXEL_SHADER_RESOURCE,
    // output to UNORDERED_ACCESS, and waits for GPU completion before reusing or
    // destroying this single-flight context. Output remains UAV; heap/root state
    // is clobbered. Does not submit, wait, allocate per frame or Present.
    FFG_API HRESULT __cdecl ffgRecord(FfgContext *, ID3D12GraphicsCommandList *, const FgdsPair *,
                                      ID3D12Resource *output);
#ifdef __cplusplus
}
#endif
