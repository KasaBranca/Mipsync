#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace MipsyncEngine::AssetFiles {
using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 32>;
using Entries = std::map<std::string, Bytes>;
// A mounted player package is read-only. Files in its namespace never fall
// back to loose files, and decrypted data is never extracted to disk.
bool IsPackedPath(const std::filesystem::path& path);
bool Exists(const std::filesystem::path& path);
Bytes Read(const std::filesystem::path& path);
std::string ReadText(const std::filesystem::path& path);
void PutMemoryFile(const std::filesystem::path& path, Bytes bytes);
bool Mount(const std::filesystem::path& pack, const Key& key, std::string& error);
void Unmount();
bool MountPlayer(const std::filesystem::path& dataDirectory, std::string& error);
bool WritePack(const std::filesystem::path& path, const Entries& entries, const Key& key, std::string& error);
Key RandomKey();
bool BindPlayerKey(const std::filesystem::path& executable, const Key& key, std::string& error);
}
