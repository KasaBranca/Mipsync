#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace MipsyncEngine::Ps1 {
// Keep standard system-area bytes intact. Only strip raw 2352-byte sector
// headers when converting to mkpsxiso's 2336-byte license-file representation.
bool BuildPs1SystemArea(std::span<const uint8_t> original,
                       std::vector<uint8_t>& output, std::string& error);
}
