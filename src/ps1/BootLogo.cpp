#include "BootLogo.h"
#include <algorithm>
namespace MipsyncEngine::Ps1 {
bool BuildPs1SystemArea(std::span<const uint8_t> original,
                       std::vector<uint8_t>& output, std::string& error) {
    output.clear(); error.clear();
    constexpr size_t sector = 2336;
    if (original.size() == sector * 12 || original.size() == sector * 16) {
        output.assign(original.begin(), original.end());
        return true;
    }
    if (original.size() == 2352 * 16) {
        output.resize(sector * 16);
        for (size_t i=0;i<16;++i)
            std::copy_n(original.begin()+i*2352+16,sector,output.begin()+i*sector);
        return true;
    }
    error = "PS1 system area requires 12/16 raw 2336-byte sectors or 16 raw 2352-byte sectors";
    return false;
}
}