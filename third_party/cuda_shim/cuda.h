// The few CUDA driver API types the OptiX headers refer to. The program never links CUDA:
// nvcuda.dll (part of the NVIDIA driver) is loaded at run time by src/DenoiseOptix.cpp, so
// the CUDA Toolkit is not needed to build.
#pragma once

#define CUDAAPI __stdcall

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;   // as in optix_types.h (64-bit)
typedef struct CUctx_st* CUcontext;
typedef struct CUstream_st* CUstream;
