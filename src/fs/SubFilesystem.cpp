#include "SubFilesystem.hpp"
#include "Logging.hpp"
#include <cassert>

fs::SubFilesystem::SubFilesystem(IFilesystem *parent, Path prefix) : mParent(parent), mPrefix(prefix) {
    assert(mParent);
    Error err;
    mParent->createDirectories(mPrefix, &err);
    if(err.failed) {
        LOG_ERROR("{}\nat {}", err.message, err.path);
    }
    assert(!err.failed);
}

void fs::SubFilesystem::copy(Path const &src, Path const &dst, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->copy(mPrefix/src, mPrefix/dst, err);
}
void fs::SubFilesystem::move(Path const &src, Path const &dst, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->move(mPrefix/src, mPrefix/dst, err);
}
void fs::SubFilesystem::createDirectory(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->createDirectory(mPrefix/path, err);
}
void fs::SubFilesystem::createDirectories(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->createDirectories(mPrefix/path, err);
}
bool fs::SubFilesystem::exists(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->exists(mPrefix/path, err);
}
uintmax_t fs::SubFilesystem::fileSize(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->fileSize(mPrefix/path, err);
}
void fs::SubFilesystem::remove(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->remove(mPrefix/path, err);
}
std::vector<fs::Path> fs::SubFilesystem::getContents(Path const &path, bool recursive, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    auto contents = mParent->getContents(mPrefix/path, recursive, err);

    for(auto &entry : contents) {
        entry = entry.makeRelative(mPrefix);
    }

    return contents;
}
bool fs::SubFilesystem::isDirectory(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isDirectory(mPrefix/path, err);
}
bool fs::SubFilesystem::isRegularFile(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isRegularFile(mPrefix/path, err);
}
bool fs::SubFilesystem::isEmpty(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isEmpty(mPrefix/path, err);
}
std::chrono::file_clock::time_point fs::SubFilesystem::lastTimeWrite(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->lastTimeWrite(mPrefix/path, err);
}
fs::FileHandle fs::SubFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->open(mPrefix/path, mode, err);
}