#include "IFilesystem.hpp"
#include <cassert>

fs::FileHandle::FileHandle(IFile *file) {
    mFile = file;
}
fs::FileHandle::~FileHandle() {
    close();
}

fs::IFile *fs::FileHandle::release() {
    auto file = mFile;
    mFile = nullptr;
    return file;
}
fs::IFile const *fs::FileHandle::get() const {
    return mFile;
}
fs::IFile *fs::FileHandle::get() {
    return mFile;
}
void fs::FileHandle::close() {
    if(mFile) {
        mFile->close();
        mFile = nullptr;
    }
}
bool fs::FileHandle::isOpen() const {
    return mFile && mFile->isOpen();
}

IStream const *fs::FileHandle::operator->() const {
    assert(isOpen());
    return mFile;
}
IStream *fs::FileHandle::operator->() {
    assert(isOpen());
    return mFile;
}