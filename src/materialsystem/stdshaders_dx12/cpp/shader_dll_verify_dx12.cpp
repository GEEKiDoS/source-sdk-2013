#include <windows.h>
#include <cstring>
#include "shader_dll_verify_dx12.h"
#include "tier1/checksum_md5.h"

namespace
{
unsigned char *s_lastInput = nullptr;
HMODULE s_module = nullptr;

class ShaderDLLVerificationDX12 final : public IShaderDLLVerificationDX12
{
public:
    CRC32_t Function1(unsigned char *data) override
    {
        s_lastInput = data + kShaderVerifyPointerOffsetDX12;
        IShaderDLLVerificationDX12 *self = this;
        CRC32_t crc;
        CRC32_Init(&crc);
        CRC32_ProcessBuffer(&crc, s_lastInput, kShaderVerifyDataLengthDX12);
        // The shipped loader hashes the LOW FOUR bytes of both pointer values on x64.
        CRC32_ProcessBuffer(&crc, &s_module, 4);
        CRC32_ProcessBuffer(&crc, &self, 4);
        CRC32_Final(&crc);
        return crc;
    }

    void Function2(int, int, int) override
    {
        MD5Context_t context;
        MD5Init(&context);
        MD5Update(&context, s_lastInput + kShaderVerifyPointerOffsetDX12,
                  kShaderVerifyDataLengthDX12 - kShaderVerifyPointerOffsetDX12);
        MD5Final(s_lastInput, &context);
    }
    void Function3(int, int, int) override {}
    void Function4(int, int, int) override {}
    CRC32_t Function5() override { return 32423; }
};
ShaderDLLVerificationDX12 s_verification;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD, LPVOID)
{
    s_module = module;
    return TRUE;
}

extern "C" __declspec(dllexport) void _ftol3(char *data)
{
    IShaderDLLVerificationDX12 *verification = &s_verification;
    // The loader passes an address inside its pointer variable, not a 4-byte
    // output buffer. Fill the entire pointer so virtual calls work on x64.
    std::memcpy(data + kShaderVerifyPointerOffsetDX12, &verification, sizeof(verification));
}
