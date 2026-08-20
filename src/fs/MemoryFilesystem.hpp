#pragma once
#include "ECS.hpp"
#include "IFilesystem.hpp"
#include <unordered_set>

namespace fs {

class MemoryFilesystem;

class MemoryFile : public IFile {
protected:
    FileOpenMode::Flags mMode;
    uint mOpenId = 0;
    uint mFileId = 0;
    MemoryFilesystem *mParent = nullptr;
    FileHandle *mHandle = nullptr;
    std::vector<std::byte> mBuffer;
    bool mOpen = false;
    uintmax_t mWriteHead = 0;
    uintmax_t mReadHead = 0;

    friend MemoryFilesystem;

    MemoryFile(FileOpenMode::Flags mode, uint fileId);
    void open(uint openId);
public:
    ~MemoryFile();
    bool isOpen() const override;
    void close() override;
    void setFileHandle(FileHandle *handle) override;
    void setParentFilesystem(MemoryFilesystem *parent);

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
};

class MemoryFilesystem : public IFilesystem {
protected:
    struct Handle {
        Path path;
        std::unique_ptr<MemoryFile> file;
        uint openId = 0;
        uint fileId = 0;
    };
    enum class DescriptorType {
        File, Directory
    };
    struct Descriptor {
        DescriptorType type;
        Path path;
        // Index into #mFileContents
        uint id = 0;
        std::chrono::file_clock::time_point modificationTime;
    };

    uint mNextId = 1;
    SparseSet<Handle> mFiles;
    SparseSet<Descriptor> mDescriptors;
    SparseSet<std::vector<std::byte>> mFileContents;
    std::unordered_map<std::string, uint> mPathToDescIndex;

    friend MemoryFile;

    void setErr(Error *outErr, std::string msg, std::string path) const;
    bool checkBeforeTransfer(fs::Path src, fs::Path dst, Error *err, std::string_view op) const;
    Descriptor &getDesc(fs::Path path, DescriptorType defaultType); // Create a descriptor if doesent exist
    uint getFileIndex(fs::Path path) const;
    void close(uint id); // open id
    void closeAll(uint id); // file id
    void closeIfInUse(fs::Path const &path);
public:
    MemoryFilesystem();
    ~MemoryFilesystem();
    MemoryFilesystem &operator=(MemoryFilesystem &&rhs);
    MemoryFilesystem(MemoryFilesystem &&rhs);

    std::vector<std::byte> serialize() const;
    void deserialize(void const *data, uintmax_t size);

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