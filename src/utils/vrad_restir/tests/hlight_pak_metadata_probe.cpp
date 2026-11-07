#include "hlight_bsp.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <cstdlib>

struct Input
{
    std::vector<unsigned char> bytes;
    unsigned calls = 0;
    unsigned long long transferred = 0;
};
static bool Read(void *context, uint32 offset, uint32 count, void *out)
{
    Input &input = *static_cast<Input *>(context);
    ++input.calls;
    if (uint64(offset) + count > input.bytes.size()) return false;
    memcpy(out, input.bytes.data() + offset, count);
    input.transferred += count;
    return true;
}
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 5) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 3;
    Input input;
    input.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (input.bytes.size() < 64 || input.bytes.size() > 0xffffffffu) return 4;
    ShadowMapBspLumpInfo pak = {64, uint32(input.bytes.size() - 64), 0, 0};
    if (argc > 2) pak.fileofs = uint32(std::strtoul(argv[2], nullptr, 0));
    if (argc > 3) pak.filelen = uint32(std::strtoul(argv[3], nullptr, 0));
    if (argc > 4) pak.uncompressedSize = uint32(std::strtoul(argv[4], nullptr, 0));
    bool found = false;
    const bool valid = hlight::FindPakAsset(Read, &input, pak, found);
    std::cout << "valid=" << valid << " found=" << found << " reads=" << input.calls
              << " bytes=" << input.transferred << '\n';
    return 0;
}
