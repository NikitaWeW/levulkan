#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace fs {

namespace FileOpenMode {
    enum FileOpenModeEnum : uint8_t
    {
        Read      = (1 << 0),
        Write     = (1 << 1),
        Append    = (1 << 2),
        Truncate  = (1 << 3),
        Binary    = (1 << 4),
    };
    using Flags = std::underlying_type_t<FileOpenModeEnum>;
};
enum class CopyOptions : uint16_t {
    None              = 0,
    SkipExisting      = (1 << 0),
    OverwriteExisting = (1 << 1),
    UpdateExisting    = (1 << 2),
    Recursive         = (1 << 3),
    CopySymlinks      = (1 << 4),
    SkipSymlinks      = (1 << 5),
    DirectoriesOnly   = (1 << 6),
    CreateSymlinks    = (1 << 7),
    CreateHardLinks   = (1 << 8),
};

using Path = std::string;

class IFile {
public:
    virtual ~IFile() = default;

    IFile() = default;
    IFile(IFile const &) = delete;
    IFile &operator=(IFile const &) = delete;
    IFile(IFile &&) = default;
    IFile &operator=(IFile &&) = default;

    /// @brief Checks if the file is opened successfully.
    virtual bool isOpen() const = 0;
    /// @brief Closes the file.
    /// After closing, the file handle may become invalid at any point in time.
    virtual void close() = 0;
    virtual FileOpenMode::Flags getMode() const = 0;

    // Read

    /// @brief Returns the size of the file in bytes.
    virtual uintmax_t size() = 0;
    /// @brief Extracts block of data.
    virtual void read(void *dst, uintmax_t size) = 0;
    /// @brief Synchronizes with the underlying storage device.
    virtual void sync() = 0;

    // Write

    /// @brief Inserts block of data.
    virtual void write(void const *src, uintmax_t size) = 0;
    /// @brief Synchronizes with the underlying storage device.
    virtual void flush() = 0;
};

class IFilesystem {
public:
    virtual ~IFilesystem() = default;

    IFilesystem() = default;
    IFilesystem(IFilesystem const &) = delete;
    IFilesystem &operator=(IFilesystem const &) = delete;
    IFilesystem(IFilesystem &&) = default;
    IFilesystem &operator=(IFilesystem &&) = default;

    virtual void setBasePath(Path const &path) = 0;

    /// @brief Copies files or directories.
    virtual void copy(Path const &from, Path const &to, CopyOptions options = CopyOptions::None) = 0;
    /// @brief Creates new directory.
    virtual void createDirectory(Path const &path) = 0;
    /// @brief Creates new directory recursively.
    virtual void createDirectories(Path const &path) = 0;
    /// @brief Checks whether path refers to existing file system object.
    virtual bool exists(Path const &path) = 0;
    /// @brief Returns the size of a file in bytes.
    virtual uintmax_t fileSize(Path const &path) = 0;
    /// @brief Removes a file or empty directory.
    virtual void remove(Path const &path) = 0;
    /// @brief Removes a file or directory and all its contents, recursively.
    virtual void removeAll(Path const &path) = 0;
    /// @brief Iterates over all elements in a directory, recursively or not.
    virtual std::vector<Path> getContents(Path const &path, bool recursive = false) = 0;
    /// @brief Checks whether the given path refers to a directory.
    virtual bool isDirectory(Path const &path) = 0;
    /// @brief Checks whether the argument refers to a regular file.
    virtual bool isRegularFile(Path const &path) = 0;
    /// @brief Checks whether the given path refers to an empty file or directory.
    virtual bool isEmpty(Path const &path) = 0;
    /// @brief Returns the last time of data modification
    virtual std::chrono::file_clock::time_point lastTimeWrite(Path const &path) = 0;

    /// @brief Opens a file.
    virtual IFile *open(Path const &path, FileOpenMode::Flags mode = 0) = 0;
};

}; // namespace fs
