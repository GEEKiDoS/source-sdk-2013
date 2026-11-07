#ifndef SHADER_DLL_VERIFY_DX12_H
#define SHADER_DLL_VERIFY_DX12_H

#include "tier1/checksum_crc.h"

// Private handshake used by materialsystem's non-mod shader-DLL loader.
static constexpr int kShaderVerifyDataLengthDX12 = 4101;
static constexpr int kShaderVerifyPointerOffsetDX12 = 43;

class IShaderDLLVerificationDX12
{
public:
    virtual CRC32_t Function1(unsigned char *data) = 0;
    virtual void Function2(int a, int b, int c) = 0;
    virtual void Function3(int a, int b, int c) = 0;
    virtual void Function4(int a, int b, int c) = 0;
    virtual CRC32_t Function5() = 0;
};

extern "C" __declspec(dllexport) void _ftol3(char *data);

#endif
