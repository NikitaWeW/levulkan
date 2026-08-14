#pragma once
#include <fstream>
#include "ECS.hpp"
#include "IFilesystem.hpp"

namespace fs {

class NativeFilesystem;

class NativeFile : public IFile {
protected:
    std::fstream mFile;
    FileOpenMode::Flags mMode;
    uint mId = 0;
    NativeFilesystem *mParent = nullptr;

    friend NativeFilesystem;

    NativeFile(Path path, FileOpenMode::Flags mode, uint id);
    void setParent(NativeFilesystem *parent);
public:
    bool isOpen() const override;
    void close() override;

    void seekg(intmax_t position) override;
    void seekg(intmax_t offset, SeekDir dir) override;
    uintmax_t tellg() override;
    uintmax_t size() override;
    void read(void *dst, uintmax_t size) override;
    void sync() override;

    void seekp(intmax_t position) override;
    void seekp(intmax_t offset, SeekDir dir) override;
    uintmax_t tellp() override;
    void write(void const *src, uintmax_t size) override;
    void flush() override;

    std::fstream &getFileHandle();
};

class NativeFilesystem : public IFilesystem {
protected:
    struct Handle {
        Path path;
        std::unique_ptr<NativeFile> file;
        uint id = 0;
    };
    SparseSet<Handle> mFiles;
    uint mNextId = 1;
    Path mBasePath = ".";

    friend NativeFile;

    void checkErr(std::error_code err, Error *outErr) const;
    void close(uint id);
    Path getFullPath(Path const &path) const;
public:
    NativeFilesystem();
    NativeFilesystem(Path basePath);
    ~NativeFilesystem();
    NativeFilesystem &operator=(NativeFilesystem &&rhs);
    NativeFilesystem(NativeFilesystem &&rhs);

    /// @brief Sets the (physical) base / root path.
    /// Might flush the filesystem and load a new one.
    /// Creates directories if they dont exist.
    void setBasePath(Path const &path);
    Path getBasePath() const;

    void copy(Path const &src, Path const &dst, Error *err = nullptr) override;
    void move(Path const &src, Path const &dst, Error *err = nullptr) override;
    void createDirectory(Path const &path, Error *err = nullptr) override;
    void createDirectories(Path const &path, Error *err = nullptr) override;
    bool exists(Path const &path, Error *err = nullptr) const override;
    uintmax_t fileSize(Path const &path, Error *err = nullptr) const override;
    void remove(Path const &path, Error *err = nullptr) override;
    std::vector<Path> getContents(Path const &path, bool recursive = false, Error *err = nullptr) const override;
    bool isDirectory(Path const &path, Error *err = nullptr) const override;
    bool isRegularFile(Path const &path, Error *err = nullptr) const override;
    bool isEmpty(Path const &path, Error *err = nullptr) const override;
    std::chrono::file_clock::time_point lastTimeWrite(Path const &path, Error *err = nullptr) const override;
    FileHandle open(Path const &path, FileOpenMode::Flags mode = 0, Error *err = nullptr) override;
};

};