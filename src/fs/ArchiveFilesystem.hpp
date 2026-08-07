#pragma once
#include "ECS.hpp"
#include "IFilesystem.hpp"
#include "zip.h"

namespace fs {

class ArchiveFilesystem;

class ArchiveFile : public IFile {
protected:
    ArchiveFilesystem *mParent = nullptr;
    zip_t *mZip = nullptr;
    zip_file_t *mFile;

    uint mHandleId = 0;
    uint mFileId = 0;

    FileOpenMode::Flags mMode;
    Path mPath;
    std::string mPassword;

    uintmax_t mWriteHead = 0;
    std::vector<std::byte> mData;

    friend ArchiveFilesystem;

    void open(Path path, FileOpenMode::Flags mode, uint id, zip_t *zip, bool compression);
    void _close();

    void setParent(ArchiveFilesystem *parent);
    void setPassword(std::string_view password);
public:
    bool isOpen() const override;
    void close() override;
    FileOpenMode::Flags getMode() const override;

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

    // IMPORTANT: Doesent flush the archive
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
    bool mBasePathChanged = true;
    Path mBasePath = ".";
    bool mCompress = true;
    uintmax_t mBasePathLastWrite = 0;

    // FIXME: No way storing password in plain text is a good idea.
    // Maybe this will help: https://en.wikipedia.org/wiki/Key_derivation_function
    std::string mPassword;

    friend ArchiveFile;

    void close(uint id);
    Path getFullPath(Path const &path);
public:
    ~ArchiveFilesystem();
    ArchiveFilesystem();
    ArchiveFilesystem &operator=(ArchiveFilesystem &&rhs);
    ArchiveFilesystem(ArchiveFilesystem &&rhs);

    void sync();
    void close();
    void discard();

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
    IFile *open(Path const &path, FileOpenMode::Flags mode = 0) override;
    std::chrono::file_clock::time_point lastTimeWrite(Path const &path) const override;

    /// @brief Closes the previous archive.
    void setBasePath(Path const &path) override;
    Path getBasePath() const override;
    void setPassword(std::string_view password);
    void setCompression(bool compress);
};

};