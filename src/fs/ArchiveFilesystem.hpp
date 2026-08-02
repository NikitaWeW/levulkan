#pragma once
#include "ECS.hpp"
#include "IFilesystem.hpp"
#include "zip.h"

namespace fs {

class ArchiveFilesystem;

class ArchiveFile : public IFile {
protected:
    FileOpenMode::Flags mMode;
    uint mId = 0;
    ArchiveFilesystem *mParent = nullptr;
    std::vector<std::byte> mData;
    bool mOpen = false;

    friend ArchiveFilesystem;

    ArchiveFile(Path path, FileOpenMode::Flags mode, uint id);
    void setParent(ArchiveFilesystem *parent);
public:
    bool isOpen() const override;
    void close() override;
    FileOpenMode::Flags getMode() const override;

    uintmax_t size() override;
    void read(void *dst, uintmax_t size) override;
    void sync() override;

    void write(void const *src, uintmax_t size) override;
    void flush() override;
};

class ArchiveFilesystem : public IFilesystem {
protected:
    struct FileHandle {
        Path path;
        std::unique_ptr<ArchiveFile> file;
        uint id = 0;
    };
    zip_t *mZip = nullptr;
    SparseSet<FileHandle> mOpenedFiles;
    uint mNextId = 1;
    Path mBasePath = ".";

    // FIXME: No way storing password in plain text is a good idea.
    // Maybe this will help: https://en.wikipedia.org/wiki/Key_derivation_function
    std::string mPassword;

    friend ArchiveFile;

    void checkErr(std::error_code err) const;
    void close(uint id);
    Path getFullPath(Path const &path);
public:
    ~ArchiveFilesystem();
    ArchiveFilesystem &operator=(ArchiveFilesystem &&rhs);
    ArchiveFilesystem(ArchiveFilesystem &&rhs);

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
    IFile *open(Path const &path, FileOpenMode::Flags mode = 0) override;
    std::chrono::file_clock::time_point lastTimeWrite(Path const &path) override;

    /// @brief Closes the previous archive.
    void setBasePath(Path const &path) override;
    void setPassword(std::string_view password);
};

};