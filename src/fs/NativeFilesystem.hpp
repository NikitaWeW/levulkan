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
    FileOpenMode::Flags getMode() const override;

    uintmax_t size() override;
    void read(void *dst, uintmax_t size) override;
    void sync() override;

    void write(void const *src, uintmax_t size) override;
    void flush() override;

    std::fstream &getFileHandle();
};

class NativeFilesystem : public IFilesystem {
protected:
    struct FileHandle {
        Path path;
        std::unique_ptr<NativeFile> file;
        uint id = 0;
    };
    SparseSet<FileHandle> mFiles;
    uint mNextId = 1;
    Path mBasePath = ".";

    friend NativeFile;

    void checkErr(std::error_code err) const;
    void close(uint id);
    Path getFullPath(Path const &path);
public:
    ~NativeFilesystem();
    NativeFilesystem &operator=(NativeFilesystem &&rhs);
    NativeFilesystem(NativeFilesystem &&rhs);
    void setBasePath(Path const &path) override;

    void copy(Path const &from, Path const &to, CopyOptions options = CopyOptions::None) override;
    void createDirectory(Path const &path) override;
    void createDirectories(Path const &path) override;
    bool exists(Path const &path) override;
    uintmax_t fileSize(Path const &path) override;
    void remove(Path const &path) override;
    void removeAll(Path const &path) override;
    std::vector<Path> getContents(Path const &path, bool recursive = false) override;
    bool isDirectory(Path const &path) override;
    bool isRegularFile(Path const &path) override;
    bool isEmpty(Path const &path) override;
    std::chrono::file_clock::time_point lastTimeWrite(Path const &path) override;
    IFile *open(Path const &path, FileOpenMode::Flags mode = 0) override;
};

};