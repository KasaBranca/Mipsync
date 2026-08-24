#include "PS1Export.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MipsyncEngine::Mips {
namespace fs = std::filesystem;

namespace {

void Push16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void Push32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void PushI32(std::vector<uint8_t>& buf, int32_t v) {
    Push32(buf, static_cast<uint32_t>(v));
}

void PushString(std::vector<uint8_t>& buf, const std::string& s) {
    // Clamp; the runtime stores lengths as u16 so 64 KiB is the hard cap.
    const size_t len = std::min<size_t>(s.size(), 0xFFFFu);
    Push16(buf, static_cast<uint16_t>(len));
    buf.insert(buf.end(), s.begin(), s.begin() + static_cast<ptrdiff_t>(len));
}

std::string SanitizeSymbol(const std::string& className) {
    std::string out;
    out.reserve(className.size());
    for (char c : className) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), '_');
    return out;
}

} // namespace

int32_t ToFixed16(double v) {
    if (std::isnan(v)) return 0;
    const double scaled = v * 65536.0;
    if (scaled >= 2147483647.0)  return 0x7FFFFFFF;
    if (scaled <= -2147483648.0) return static_cast<int32_t>(0x80000000);
    return static_cast<int32_t>(std::lrint(scaled));
}

double FromFixed16(int32_t v) {
    return static_cast<double>(v) / 65536.0;
}

std::vector<uint8_t> EncodeMbc(const CompiledModule& module) {
    std::vector<uint8_t> buf;
    buf.reserve(256 + module.className.size());

    buf.push_back('M'); buf.push_back('B'); buf.push_back('C'); buf.push_back('1');
    Push16(buf, kMbcCurrentVersion);
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.className.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.numberConstants.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.stringConstants.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.nameConstants.size(),   0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.fields.size(),          0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.methods.size(),         0xFFFFu)));
    Push16(buf, 0);

    buf.insert(buf.end(), module.className.begin(), module.className.end());

    for (double n : module.numberConstants)
        PushI32(buf, ToFixed16(n));

    for (const auto& s : module.stringConstants)
        PushString(buf, s);

    for (const auto& s : module.nameConstants)
        PushString(buf, s);

    for (const auto& f : module.fields) {
        PushString(buf, f.name);
        Push16(buf, f.defaultConstIndex);
        buf.push_back(static_cast<uint8_t>(f.valueKind));
        const uint8_t flags = static_cast<uint8_t>((f.hasGetter ? 0x1 : 0) |
                                                   (f.hasSetter ? 0x2 : 0));
        buf.push_back(flags);
    }

    for (const auto& m : module.methods) {
        PushString(buf, m.name);
        Push32(buf, static_cast<uint32_t>(m.code.size()));
        buf.insert(buf.end(), m.code.begin(), m.code.end());
        Push16(buf, static_cast<uint16_t>(std::min<uint32_t>(m.localCount, 0xFFFFu)));
        Push16(buf, 0);
    }

    return buf;
}

bool WriteMbcFile(const CompiledModule& module, const std::string& path,
                  std::string& outError) {
    try {
        const auto bytes = EncodeMbc(module);
        const fs::path target = PathUtf8::FromString(path);
        if (target.has_parent_path())
            fs::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open .mbc for write: " + path;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool EmitScriptsDataC(const std::vector<CompiledModule>& modules,
                      const std::string& outCFile,
                      ScriptsDataEmit& outStats,
                      std::string& outError) {
    try {
        outStats = {};
        const fs::path cPath = PathUtf8::FromString(outCFile);
        if (cPath.has_parent_path())
            fs::create_directories(cPath.parent_path());
        std::ofstream out(cPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open scripts_data.c for write: " + outCFile;
            return false;
        }

        out << "/* Auto-generated by Mipsync (src/mips/PS1Export.cpp).\n"
               " * Do not edit by hand — regenerated by `Build PS1` every time.\n"
               " * Holds .mbc bytecode for every Mipsync script in the project,\n"
               " * embedded as `static const unsigned char` arrays. The PS1 mini-VM\n"
               " * (templates/ps1/starter/runtime/vm.c) iterates `g_mipsync_scripts`\n"
               " * and decodes each blob.\n"
               " */\n"
               "#include <stddef.h>\n"
               "#include \"runtime/scripts_data.h\"\n\n";

        for (size_t i = 0; i < modules.size(); ++i) {
            const auto bytes = EncodeMbc(modules[i]);
            outStats.totalBytes += static_cast<uint32_t>(bytes.size());
            const std::string sym = "k_mbc_" + SanitizeSymbol(modules[i].className);
            out << "static const unsigned char " << sym << "[" << bytes.size() << "] = {\n  ";
            for (size_t j = 0; j < bytes.size(); ++j) {
                out << "0x";
                out.width(2);
                out.fill('0');
                out << std::hex << static_cast<int>(bytes[j]) << std::dec;
                if (j + 1 < bytes.size()) out << ",";
                if (((j + 1) % 16) == 0) out << "\n  ";
                else if (j + 1 < bytes.size()) out << " ";
            }
            out << "\n};\n\n";
        }

        out << "const mipsync_script_blob g_mipsync_scripts[] = {\n";
        for (size_t i = 0; i < modules.size(); ++i) {
            const auto bytes = EncodeMbc(modules[i]);
            const std::string sym = "k_mbc_" + SanitizeSymbol(modules[i].className);
            out << "    { \"" << modules[i].className << "\", "
                << sym << ", " << bytes.size() << " },\n";
        }
        out << "};\n";
        out << "const unsigned int g_mipsync_script_count = "
            << modules.size() << "u;\n";

        outStats.scriptCount = static_cast<uint32_t>(modules.size());
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine::Mips
