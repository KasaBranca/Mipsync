#include "ps1/BootLogo.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#define CHECK(expr) do { if (!(expr)) { std::cerr << "Failed: " #expr << '\n'; return 1; } } while (0)
int main() {
    using MipsyncEngine::Ps1::BuildPs1SystemArea;
    std::ifstream input(MIPSYNC_BOOT_LICENSE,std::ios::binary);
    std::vector<uint8_t> original(std::istreambuf_iterator<char>(input),{}),output;
    std::string error;
    CHECK(original.size()==12*2336);
    CHECK(BuildPs1SystemArea(original,output,error));
    CHECK(output==original); // No logo, license text, subheader or parity edits.
    const size_t license=4*2336+8;
    CHECK(std::string(output.begin()+license,output.begin()+license+65)==
        "          Licensed  by          Sony Computer Entertainment Inc.\n");
    CHECK(output[5*2336+8]==0x41); // Stock BIOS TMD remains present.
    std::vector<uint8_t> raw(16*2352,0x77),expected(16*2336,0x77);
    for(size_t s=0;s<12;++s) {
        std::copy_n(original.begin()+s*2336,2336,raw.begin()+s*2352+16);
        std::copy_n(original.begin()+s*2336,2336,expected.begin()+s*2336);
    }
    CHECK(BuildPs1SystemArea(raw,output,error));
    CHECK(output==expected); // Raw sector conversion keeps all 16 sectors.
    CHECK(BuildPs1SystemArea(expected,output,error) && output==expected);
    // Any explicit regional file stays intact, including EU/US metadata.
    expected[license]='E';
    CHECK(BuildPs1SystemArea(expected,output,error) && output==expected);
    CHECK(!BuildPs1SystemArea({},output,error) && output.empty() && !error.empty());
    CHECK(!BuildPs1SystemArea(std::vector<uint8_t>(15),output,error));
    std::cout<<"PS1 standard system-area bytes and NTSC-J default preserved.\n";
}