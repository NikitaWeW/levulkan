#include "ArchiveFilesystem.hpp"
#include "Logging.hpp"

fs::ArchiveFile::ArchiveFile(Path path, FileOpenMode::Flags mode, uint id) {
    mId = id;
}
void fs::ArchiveFile::setParent(ArchiveFilesystem *parent) {
    mParent = parent;
}
bool fs::ArchiveFile::isOpen() const {
    return mOpen;
}
void fs::ArchiveFile::close() {
    flush();
}
fs::FileOpenMode::Flags fs::ArchiveFile::getMode() const {
    return mMode;
}
uintmax_t fs::ArchiveFile::size() {
}
void fs::ArchiveFile::read(void *dst, uintmax_t size) {
}
void fs::ArchiveFile::sync() {
}
void fs::ArchiveFile::write(void const *src, uintmax_t size) {
}
void fs::ArchiveFile::flush() {
}

void fs::ArchiveFilesystem::checkErr(std::error_code err) const {
    if(err) {
        LOG_ERROR("Archive filesystem {} error: {}", err.category().name(), err.message());
    }
    assert(!err);
}
void fs::ArchiveFilesystem::close(uint id) {
    assert(mOpenedFiles.contains(id));
    auto path = mOpenedFiles.at(id).path;
    auto &file = mOpenedFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    mOpenedFiles.erase(id);
}
fs::ArchiveFilesystem::~ArchiveFilesystem() {
    for(auto id : mOpenedFiles.sparse()) {
        close(id);
    }
}
fs::Path fs::ArchiveFilesystem::getFullPath(Path const &path) {
    return mBasePath + '/' + path;
}
fs::ArchiveFilesystem &fs::ArchiveFilesystem::operator=(ArchiveFilesystem &&rhs) {
    if(this == &rhs)
        return *this;

    mOpenedFiles = std::move(rhs.mOpenedFiles);
    mNextId = rhs.mNextId;
    
    for(auto &handle : mOpenedFiles.dense()) {
        handle.file->setParent(this);
    }

    return *this;
}
fs::ArchiveFilesystem::ArchiveFilesystem(ArchiveFilesystem &&rhs) {
    *this = std::move(rhs);
}
void fs::ArchiveFilesystem::setBasePath(Path const &path) {
    mBasePath = path;
}
void fs::ArchiveFilesystem::copy(Path const &from, Path const &to, CopyOptions options) {
}
void fs::ArchiveFilesystem::createDirectory(Path const &path) {
}
void fs::ArchiveFilesystem::createDirectories(Path const &path) {
}
bool fs::ArchiveFilesystem::exists(Path const &path) {
}
uintmax_t fs::ArchiveFilesystem::fileSize(Path const &path) {
}
void fs::ArchiveFilesystem::remove(Path const &path) {
}
void fs::ArchiveFilesystem::removeAll(Path const &path) {
}
std::vector<fs::Path> fs::ArchiveFilesystem::getContents(Path const &path, bool recursive) {
}
bool fs::ArchiveFilesystem::isDirectory(Path const &path) {
}
bool fs::ArchiveFilesystem::isRegularFile(Path const &path) {
}
bool fs::ArchiveFilesystem::isEmpty(Path const &path) {
}
fs::IFile *fs::ArchiveFilesystem::open(Path const &path, FileOpenMode::Flags mode) {
}

