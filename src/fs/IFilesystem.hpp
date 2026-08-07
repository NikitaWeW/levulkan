#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace fs {

namespace FileOpenMode {
    enum FileOpenModeEnum : uint8_t
    {
        None      = 0,
        Append    = (1 << 0),
        Truncate  = (1 << 1),
    };
    using Flags = std::underlying_type_t<FileOpenModeEnum>;
};
enum class SeekDir : uint8_t {
    Beg,
    End,
    Cur
};

class IFilesystem;

/// POSIX stype paths
/// Absolute paths only
/// /path/to/file.extension
/// /path/to/dir/
class Path {
private:
    std::string mPath;
public:
    Path();
    Path(Path const &);
    Path(Path &&);
    Path(std::string const &path);
    Path(std::string &&path);
    Path(std::string_view path);
    Path(char const *path);
    
    Path &operator=(Path const &) = default;
    Path &operator=(Path &&) = default;
    Path &operator=(std::string const &path);
    Path &operator=(std::string &&path);
    Path &operator=(std::string_view path);
    Path &operator=(char const *path);

    bool operator==(Path const &rhs) const;
    bool operator!=(Path const &rhs) const;

    std::string const &string() const;
    
    // TODO: Doc
    Path filename() const;
    Path extension() const;
    Path stem() const;
    Path parentPath() const;
    bool valid(IFilesystem *filesystem = nullptr) const;
    bool empty() const;
    std::vector<Path> split() const;
    bool isAbsolute() const;

    Path relativeTo(Path) const;
    void makeAbsolute();
    
    Path &append(Path const &rhs);
    Path &concat(Path const &rhs);
    void clear();

    Path &operator/=(Path const &rhs);
    Path &operator+=(Path const &rhs);
};
Path operator/(Path const &lhs, Path const &rhs);
Path operator+(Path const &lhs, Path const &rhs);

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

    /// @brief Sets read position indicator.
    virtual void seekg(intmax_t position) = 0;
    /// @brief Sets read position indicator relative to @p dir.
    virtual void seekg(intmax_t offset, SeekDir dir) = 0;
    /// @brief Returns read position indicator.
    virtual uintmax_t tellg() = 0;
    /// @brief Returns the size of the file in bytes.
    virtual uintmax_t size() = 0;
    /// @brief Extracts block of data.
    virtual void read(void *dst, uintmax_t size) = 0;
    /// @brief Synchronizes with the underlying storage device.
    virtual void sync() = 0;

    // Write

    /// @brief Sets write position indicator.
    virtual void seekp(intmax_t position) = 0;
    /// @brief Sets write position indicator relative to @p dir.
    virtual void seekp(intmax_t offset, SeekDir dir) = 0;
    /// @brief Returns write position indicator.
    virtual uintmax_t tellp() = 0;
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
    virtual Path getBasePath() const = 0;

    /// @brief Copies files or directories.
    virtual void copy(Path const &from, Path const &to) = 0;
    /// @brief Copies files or directories.
    virtual void move(Path const &from, Path const &to) = 0;
    /// @brief Creates new directory.
    virtual void createDirectory(Path const &path) = 0;
    /// @brief Creates new directory recursively.
    virtual void createDirectories(Path const &path) = 0;
    /// @brief Checks whether path refers to existing file system object.
    virtual bool exists(Path const &path) const = 0;
    /// @brief Returns the size of a file in bytes.
    virtual uintmax_t fileSize(Path const &path) const = 0;
    /// @brief Removes a file or empty directory.
    virtual void remove(Path const &path) = 0;
    /// @brief Removes a file or directory and all its contents, recursively.
    virtual void removeAll(Path const &path) = 0;
    /// @brief Iterates over all elements in a directory, recursively or not.
    virtual std::vector<Path> getContents(Path const &path, bool recursive = false) const = 0;
    /// @brief Checks whether the given path refers to a directory.
    virtual bool isDirectory(Path const &path) const = 0;
    /// @brief Checks whether the argument refers to a regular file.
    virtual bool isRegularFile(Path const &path) const = 0;
    /// @brief Checks whether the given path refers to an empty file or directory.
    virtual bool isEmpty(Path const &path) const = 0;
    /// @brief Returns the last time of data modification
    virtual std::chrono::file_clock::time_point lastTimeWrite(Path const &path) const = 0;

    /// @brief Opens a file. Creates a new file if it doesent exist.
    virtual IFile *open(Path const &path, FileOpenMode::Flags mode = 0) = 0;
};

}; // namespace fs
