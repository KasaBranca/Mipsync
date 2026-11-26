#include "BuiltinAssets.h"
#include "AssetManager.h"
#include "../core/RuntimePaths.h"
#include <algorithm>

namespace MipsyncEngine::BuiltinAssets {
namespace fs = std::filesystem;

namespace {
std::string Normalized(std::string_view path) {
    std::string out(path);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}
} // namespace

bool IsBuiltinPath(std::string_view projectRelPath) {
    const std::string path = Normalized(projectRelPath);
    const std::string root = kRoot;
    return path == root || path.rfind(root + "/", 0) == 0;
}

fs::path Directory() {
    return GetBundledResourcesDirectory() / kRoot;
}

fs::path Resolve(std::string_view projectRelPath) {
    if (!IsBuiltinPath(projectRelPath))
        return {};
    const fs::path relative =
        PathUtf8::FromString(Normalized(projectRelPath)).lexically_normal();
    fs::path inside;
    bool first = true;
    for (const auto& part : relative) {
        if (first) { first = false; continue; } // the "builtin" root itself
        if (part == ".." || part.has_root_name() || part.has_root_directory())
            return {};
        inside /= part;
    }
    return inside.empty() ? Directory() : Directory() / inside;
}

fs::path ResolveAssetPath(const fs::path& projectRoot, std::string_view projectRelPath) {
    if (IsBuiltinPath(projectRelPath))
        return Resolve(projectRelPath);
    return projectRoot / PathUtf8::FromString(std::string(projectRelPath));
}

std::string ToBuiltinPath(const fs::path& absolute) {
    std::error_code ec;
    const fs::path dir = fs::weakly_canonical(Directory(), ec);
    if (ec) return {};
    const fs::path file = fs::weakly_canonical(absolute, ec);
    if (ec) return {};
    const fs::path rel = file.lexically_relative(dir);
    if (rel.empty() || *rel.begin() == "..")
        return {};
    if (rel == ".")
        return kRoot;
    return std::string(kRoot) + "/" + Normalized(PathUtf8::ToString(rel));
}

std::vector<std::string> List() {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(Directory(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = PathUtf8::ToString(it->path().filename());
        if (!name.empty() && name.front() != '.')
            files.push_back(std::string(kRoot) + "/" + name);
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace MipsyncEngine::BuiltinAssets
