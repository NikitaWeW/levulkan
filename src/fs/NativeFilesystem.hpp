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

    void checkErr(std::error_code err) const;
    void close(uint id);
    Path getFullPath(Path const &path) const;
public:
    NativeFilesystem();
    ~NativeFilesystem();
    NativeFilesystem &operator=(NativeFilesystem &&rhs);
    NativeFilesystem(NativeFilesystem &&rhs);
    void setBasePath(Path const &path) override;
    Path getBasePath() const override;

    void copy(Path const &from, Path const &to) override;
    void move(Path const &from, Path const &to) override;
    void createDirectory(Path const &path) override;
    void createDirectories(Path const &path) override;
    bool exists(Path const &path) const override;
    uintmax_t fileSize(Path const &path) const override;
    void remove(Path const &path) override;
    void removeAll(Path const &path) override;
    std::vector<Path> getContents(Path const &path, bool recursive = false) const override;
    bool isDirectory(Path const &path) const override;
    bool isRegularFile(Path const &path) const override;
    bool isEmpty(Path const &path) const override;
    FileHandle open(Path const &path, FileOpenMode::Flags mode = 0) override;
    std::chrono::file_clock::time_point lastTimeWrite(Path const &path) const override;
};

};