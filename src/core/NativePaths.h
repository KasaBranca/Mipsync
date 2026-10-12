#pragma once
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MipsyncEngine::NativePaths {
namespace fs = std::filesystem;
inline fs::path OrdinaryPath(const fs::path& path) {
#ifdef _WIN32
    const auto& name = path.native();
    if (name.rfind(LR"(\\?\UNC\)", 0) == 0) return fs::path(LR"(\\)" + name.substr(8));
    if (name.rfind(LR"(\\?\)", 0) == 0) return fs::path(name.substr(4));
#endif
    return path;
}
// Win32 file copies need the extended-length prefix even when directory
// creation and file streams already accept the same long Unicode path.
inline fs::path FilePath(const fs::path& path) {
#ifdef _WIN32
    if (path.native().rfind(LR"(\\?\)", 0) == 0) return path;
    const auto absolute = fs::absolute(path).make_preferred();
    const auto native = absolute.native();
    if (native.rfind(LR"(\\?\)", 0) == 0) return absolute;
    if (native.rfind(LR"(\\)", 0) == 0)
        return fs::path(LR"(\\?\UNC\)" + native.substr(2));
    return fs::path(LR"(\\?\)" + native);
#else
    return path;
#endif
}
inline fs::path Canonical(const fs::path& path) {
#ifdef _WIN32
    HANDLE file = CreateFileW(FilePath(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw fs::filesystem_error("Cannot resolve path", path, std::error_code(GetLastError(), std::system_category()));
    wchar_t name[32768]{};
    const DWORD length = GetFinalPathNameByHandleW(file, name, 32768, FILE_NAME_NORMALIZED);
    const DWORD error = GetLastError(); CloseHandle(file);
    if (!length || length >= 32768)
        throw fs::filesystem_error("Cannot resolve path", path, std::error_code(error, std::system_category()));
    return OrdinaryPath(fs::path(name));
#else
    return fs::canonical(path);
#endif
}
inline void CopyRegularFile(const fs::path& source, const fs::path& destination) {
    fs::create_directories(FilePath(destination.parent_path()));
#ifdef _WIN32
    if (!CopyFileW(FilePath(source).c_str(), FilePath(destination).c_str(), FALSE))
        throw fs::filesystem_error("Cannot copy file", source, destination,
                                  std::error_code(GetLastError(), std::system_category()));
#else
    fs::copy_file(FilePath(source), FilePath(destination), fs::copy_options::overwrite_existing);
#endif
}
#ifdef _WIN32
// MinGW's directory iterators do not preserve extended-length paths reliably.
// Enumerate with Win32 so both cleanup and SDK copies work beyond MAX_PATH.
template<class Visitor>
inline void VisitDirectory(const fs::path& path, Visitor visitor) {
    WIN32_FIND_DATAW entry{};
    const auto pattern = FilePath(OrdinaryPath(path) / L"*");
    HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return;
        throw fs::filesystem_error("Cannot enumerate directory", path,
                                  std::error_code(error, std::system_category()));
    }
    struct SearchHandle { HANDLE value; ~SearchHandle() { FindClose(value); } } handle{search};
    do {
        if (wcscmp(entry.cFileName, L".") && wcscmp(entry.cFileName, L".."))
            visitor(entry);
    } while (FindNextFileW(search, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw fs::filesystem_error("Cannot enumerate directory", path,
                                  std::error_code(GetLastError(), std::system_category()));
}
#endif
inline void CopyTree(const fs::path& source, const fs::path& destination,
                     bool skipBuildDirectory = false) {
    const auto root = fs::absolute(OrdinaryPath(source)).make_preferred();
    fs::create_directories(FilePath(destination));
#ifdef _WIN32
    VisitDirectory(root, [&](const WIN32_FIND_DATAW& entry) {
        if (skipBuildDirectory && wcscmp(entry.cFileName, L"build") == 0) return;
        const auto input = root / entry.cFileName;
        const auto output = OrdinaryPath(destination) / entry.cFileName;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            CopyTree(input, output);
        else CopyRegularFile(input, output);
    });
#else
    for (auto it = fs::recursive_directory_iterator(FilePath(root));
         it != fs::recursive_directory_iterator(); ++it) {
        const auto relative = OrdinaryPath(it->path()).lexically_relative(root);
        if (skipBuildDirectory && relative.begin()->native() == fs::path("build").native()) {
            it.disable_recursion_pending(); continue;
        }
        if (it->is_directory()) fs::create_directories(FilePath(destination / relative));
        else if (it->is_regular_file()) CopyRegularFile(it->path(), destination / relative);
    }
#endif
}
inline void RemoveTree(const fs::path& path) {
#ifdef _WIN32
    const auto native = FilePath(path);
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
        throw fs::filesystem_error("Cannot inspect generated output", path, std::error_code(error, std::system_category()));
    }
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (directory && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        VisitDirectory(path, [&](const WIN32_FIND_DATAW& entry) {
            RemoveTree(OrdinaryPath(path) / entry.cFileName);
        });
    }
    if (!(directory ? RemoveDirectoryW(native.c_str()) : DeleteFileW(native.c_str())))
        throw fs::filesystem_error("Cannot remove generated output", path, std::error_code(GetLastError(), std::system_category()));
#else
    fs::remove_all(path);
#endif
}
}
