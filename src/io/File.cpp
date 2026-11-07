#include "engine/io/fs/IFilesystem.hpp"
#include <cassert>

egn::fs::FileHandle::FileHandle(IFile *file) {
    mFile = file;
    if(mFile)
        mFile->setFileHandle(this);
}
egn::fs::FileHandle::~FileHandle() {
    close();
}
egn::fs::FileHandle::FileHandle(FileHandle &&rhs) {
    *this = std::move(rhs);
}
egn::fs::FileHandle &egn::fs::FileHandle::operator=(FileHandle &&rhs) {
    std::swap(mFile, rhs.mFile);
    if(mFile)
        mFile->setFileHandle(this);
    return *this;
}

egn::fs::IFile *egn::fs::FileHandle::release() {
    auto file = mFile;
    mFile = nullptr;
    return file;
}
egn::IStream const *egn::fs::FileHandle::get() const {
    return mFile;
}
egn::IStream *egn::fs::FileHandle::get() {
    return mFile;
}
void egn::fs::FileHandle::close() {
    if(isOpen()) {
        mFile->close();
    }
    mFile = nullptr;
}
bool egn::fs::FileHandle::isOpen() const {
    return mFile && mFile->isOpen();
}

egn::IStream const *egn::fs::FileHandle::operator->() const {
    assert(isOpen());
    return mFile;
}
egn::IStream *egn::fs::FileHandle::operator->() {
    assert(isOpen());
    return mFile;
}