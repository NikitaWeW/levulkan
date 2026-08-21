#pragma once
#include "IFilesystem.hpp"

namespace fs {

class SubFilesystem : public IFilesystem {
protected:
    IFilesystem *mParent = nullptr;
    Path mPrefix;
public:
    SubFilesystem() = default;
    SubFilesystem(IFilesystem *parent, Path prefix);

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

} // namespace fs