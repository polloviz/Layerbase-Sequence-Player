// The few CUDA driver API types the OptiX and Optical Flow headers refer to. The program never
// links CUDA: nvcuda.dll (part of the NVIDIA driver) is loaded at run time by src/DenoiseOptix.cpp,
// so the CUDA Toolkit is not needed to build.
#pragma once

#include <stddef.h>

#define CUDAAPI __stdcall

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;   // as in optix_types.h (64-bit)
typedef struct CUctx_st* CUcontext;
typedef struct CUstream_st* CUstream;
typedef struct CUarray_st* CUarray;

typedef enum CUmemorytype_enum {
    CU_MEMORYTYPE_HOST = 1,
    CU_MEMORYTYPE_DEVICE = 2,
    CU_MEMORYTYPE_ARRAY = 3,
    CU_MEMORYTYPE_UNIFIED = 4
} CUmemorytype;

// cuMemcpy2D_v2
typedef struct CUDA_MEMCPY2D_st {
    size_t srcXInBytes;
    size_t srcY;
    CUmemorytype srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    size_t srcPitch;
    size_t dstXInBytes;
    size_t dstY;
    CUmemorytype dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    size_t dstPitch;
    size_t WidthInBytes;
    size_t Height;
} CUDA_MEMCPY2D;
