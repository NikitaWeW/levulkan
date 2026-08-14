#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "IStream.hpp"

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

class IFilesystem;

/// POSIX stype paths
/// Absolute paths only
/// /path/to/file.extension
/// /path/to/dir/
class Path {
private:
    std::string mPath;
    void removeStuff();
public:
    Path();
    Path(Path const &);
    Path(Path &&);
    Path(std::string const &path);
    Path(std::string &&path);
    Path(std::string_view path);
    Path(char const *path);
    template<std::ranges::range T>
    explicit Path(T const &components, bool absolute = true);
    
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
    std::string filename() const;
    std::string extension() const;
    std::string stem() const;
    Path parentPath() const;
    bool empty() const;
    std::vector<std::string> split() const;
    bool isAbsolute() const;

    Path makeRelative(Path const &to) const;
    Path makeAbsolute(Path const &to) const;

    std::string removeFilename();
    std::string removeExtension();
    Path        removeParentPath();
    
    Path &append(Path const &rhs);
    Path &concat(Path const &rhs);
    void clear();

    Path &operator/=(Path const &rhs);
    Path &operator+=(Path const &rhs);
};
Path operator/(Path const &lhs, Path const &rhs);
Path operator+(Path const &lhs, Path const &rhs);

class IFile : public IStream {
public:
    virtual ~IFile() = default;

    IFile() = default;

    // Pointers to IFIle must be persistent.
    IFile(IFile const &) = delete;
    IFile &operator=(IFile const &) = delete;
    IFile(IFile &&) = delete;
    IFile &operator=(IFile &&) = delete;

    /// @brief Checks if the file is opened successfully.
    virtual bool isOpen() const = 0;
    /// @brief Closes the file.
    /// After closing, the file handle (pointer) may become invalid.
    virtual void close() = 0;
};

/// @brief RAII IFile wrapper. 
/// Ensures the safe memory access to the file handle.
class FileHandle {
private:
    IFile *mFile = nullptr;
public:
    FileHandle() = default;
    explicit FileHandle(IFile *file);
    ~FileHandle();

    FileHandle(FileHandle const &) = delete;
    FileHandle &operator=(FileHandle const &) = delete;
    FileHandle(FileHandle &&) = default;
    FileHandle &operator=(FileHandle &&) = default;

    [[nodiscard]] IFile *release();
    IStream const *get() const;
    IStream *get();
    void close();
    bool isOpen() const;

    IStream const *operator->() const;
    IStream *operator->();
};

struct Error {
    bool failed = false;
    std::string message;
};

class IFilesystem {
public:
    virtual ~IFilesystem() = default;

    IFilesystem() = default;
    IFilesystem(IFilesystem const &) = delete;
    IFilesystem &operator=(IFilesystem const &) = delete;
    IFilesystem(IFilesystem &&) = default;
    IFilesystem &operator=(IFilesystem &&) = default;

    /// @brief Copies files or directories.
    virtual void copy(Path const &src, Path const &dst, Error *err = nullptr) = 0;
    /// @brief Copies files or directories.
    virtual void move(Path const &src, Path const &dst, Error *err = nullptr) = 0;
    /// @brief Creates new directory.
    virtual void createDirectory(Path const &path, Error *err = nullptr) = 0;
    /// @brief Creates new directory recursively.
    virtual void createDirectories(Path const &path, Error *err = nullptr) = 0;
    /// @brief Checks whether path refers to existing file system object.
    virtual bool exists(Path const &path, Error *err = nullptr) const = 0;
    /// @brief Returns the size of a file in bytes.
    virtual uintmax_t fileSize(Path const &path, Error *err = nullptr) const = 0;
    /// @brief Removes a file or a directory and all its contents, recursively.
    virtual void remove(Path const &path, Error *err = nullptr) = 0;
    /// @brief Iterates over all elements in a directory, recursively or not.
    virtual std::vector<Path> getContents(Path const &path, bool recursive = false, Error *err = nullptr) const = 0;
    /// @brief Checks whether the given path refers to a directory.
    virtual bool isDirectory(Path const &path, Error *err = nullptr) const = 0;
    /// @brief Checks whether the argument refers to a regular file.
    virtual bool isRegularFile(Path const &path, Error *err = nullptr) const = 0;
    /// @brief Checks whether the given path refers to an empty file or directory.
    virtual bool isEmpty(Path const &path, Error *err = nullptr) const = 0;
    /// @brief Returns the last time of data modification
    virtual std::chrono::file_clock::time_point lastTimeWrite(Path const &path, Error *err = nullptr) const = 0;

    /// @brief Opens a file. Creates a new file if it doesent exist.
    virtual FileHandle open(Path const &path, FileOpenMode::Flags mode = 0, Error *err = nullptr) = 0;
};

}; // namespace fs

template<std::ranges::range T>
inline fs::Path::Path(T const &components, bool absolute) {
    for(auto it = components.begin(); it != components.end(); ++it) {
        mPath.append("/").append(*it);
    }
    if(!absolute && !mPath.empty())
        mPath.erase(0, 1);
    removeStuff();
}
