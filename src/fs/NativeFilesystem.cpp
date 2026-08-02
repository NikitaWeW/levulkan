#include "NativeFilesystem.hpp"
#include <filesystem>
#include "Logging.hpp"

fs::NativeFile::NativeFile(Path path, FileOpenMode::Flags mode, uint id) {
    mId = id;
    std::ios::openmode iosMode;

    if(mode & FileOpenMode::Read)
        iosMode |= std::ios::in;
    if(mode & FileOpenMode::Write)
        iosMode |= std::ios::out;
    if(mode & FileOpenMode::Append)
        iosMode |= std::ios::app;
    if(mode & FileOpenMode::Truncate)
        iosMode |= std::ios::trunc;
    if(mode & FileOpenMode::Binary)
        iosMode |= std::ios::binary;

    iosMode |= std::ios::ate;

    mFile.open(path, iosMode);
}
void fs::NativeFile::setParent(NativeFilesystem *parent) {
    mParent = parent;
}
bool fs::NativeFile::isOpen() const {
    return mFile.is_open();
}
void fs::NativeFile::close() {
    mFile.close();
    if(mParent)
        mParent->close(mId);
}
fs::FileOpenMode::Flags fs::NativeFile::getMode() const {
    return mMode;
}
uintmax_t fs::NativeFile::size() {
    auto pos = mFile.tellg();
    mFile.seekg(0, std::ios_base::end);
    auto size = mFile.tellg();
    mFile.seekg(pos);
    return size;
}
void fs::NativeFile::read(void *dst, uintmax_t size) {
    mFile.read(static_cast<char *>(dst), size);
}
void fs::NativeFile::sync() {
    mFile.sync();
}
void fs::NativeFile::write(void const *src, uintmax_t size) {
    mFile.write(static_cast<char const *>(src), size);
}
void fs::NativeFile::flush() {
    mFile.flush();
}
std::fstream &fs::NativeFile::getFileHandle() {
    return mFile;
}

void fs::NativeFilesystem::checkErr(std::error_code err) const {
    if(err) {
        LOG_ERROR("Native filesystem {} error: {}", err.category().name(), err.message());
    }
    assert(!err);
}
void fs::NativeFilesystem::close(uint id) {
    assert(mFiles.contains(id));
    auto path = mFiles.at(id).path;
    auto &file = mFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    mFiles.erase(id);
}
fs::NativeFilesystem::~NativeFilesystem() {
    for(auto id : mFiles.sparse()) {
        close(id);
    }
}
fs::Path fs::NativeFilesystem::getFullPath(Path const &path) {
    return mBasePath + '/' + path;
}
fs::NativeFilesystem &fs::NativeFilesystem::operator=(NativeFilesystem &&rhs) {
    if(this == &rhs)
        return *this;

    mFiles = std::move(rhs.mFiles);
    mNextId = rhs.mNextId;
    
    for(auto &handle : mFiles.dense()) {
        handle.file->setParent(this);
    }

    return *this;
}
fs::NativeFilesystem::NativeFilesystem(NativeFilesystem &&rhs) {
    *this = std::move(rhs);
}
void fs::NativeFilesystem::copy(Path const &from, Path const &to, CopyOptions options) {
    std::filesystem::copy_options copyOptions = std::filesystem::copy_options::none;
    switch(options) {
    case CopyOptions::None:              copyOptions = std::filesystem::copy_options::none;               break;
    case CopyOptions::SkipExisting:      copyOptions = std::filesystem::copy_options::skip_existing;      break;
    case CopyOptions::OverwriteExisting: copyOptions = std::filesystem::copy_options::overwrite_existing; break;
    case CopyOptions::UpdateExisting:    copyOptions = std::filesystem::copy_options::update_existing;    break;
    case CopyOptions::Recursive:         copyOptions = std::filesystem::copy_options::recursive;          break;
    case CopyOptions::CopySymlinks:      copyOptions = std::filesystem::copy_options::copy_symlinks;      break;
    case CopyOptions::SkipSymlinks:      copyOptions = std::filesystem::copy_options::skip_symlinks;      break;
    case CopyOptions::DirectoriesOnly:   copyOptions = std::filesystem::copy_options::directories_only;   break;
    case CopyOptions::CreateSymlinks:    copyOptions = std::filesystem::copy_options::create_symlinks;    break;
    case CopyOptions::CreateHardLinks:   copyOptions = std::filesystem::copy_options::create_hard_links;  break;
    default: break;
    }

    std::error_code err;
    std::filesystem::copy(getFullPath(from), getFullPath(to), copyOptions, err);
    checkErr(err);
}
void fs::NativeFilesystem::createDirectory(Path const &path) {
    std::error_code err;
    std::filesystem::create_directory(getFullPath(path), err);
    checkErr(err);
}
void fs::NativeFilesystem::createDirectories(Path const &path) {
    std::error_code err;
    std::filesystem::create_directories(getFullPath(path), err);
    checkErr(err);
}
bool fs::NativeFilesystem::exists(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::exists(getFullPath(path), err);
    checkErr(err);
    return res;
}
uintmax_t fs::NativeFilesystem::fileSize(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::file_size(getFullPath(path), err);
    checkErr(err);
    return res;
}
void fs::NativeFilesystem::remove(Path const &path) {
    std::error_code err;
    std::filesystem::remove(getFullPath(path), err);
    checkErr(err);
}
void fs::NativeFilesystem::removeAll(Path const &path) {
    std::error_code err;
    std::filesystem::remove_all(getFullPath(path), err);
    checkErr(err);
}
std::vector<fs::Path> fs::NativeFilesystem::getContents(Path const &path, bool recursive) {
    std::vector<Path> res;

    if(recursive) {
        for(auto entry : std::filesystem::recursive_directory_iterator(getFullPath(path))) {
            res.emplace_back(entry.path().string());
        }
    } else {
        for(auto entry : std::filesystem::directory_iterator(getFullPath(path))) {
            res.emplace_back(entry.path().string());
        }
    }

    return res;
}
bool fs::NativeFilesystem::isDirectory(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::is_directory(getFullPath(path), err);
    checkErr(err);
    return res;

}
bool fs::NativeFilesystem::isRegularFile(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::is_regular_file(getFullPath(path), err);
    checkErr(err);
    return res;
}
bool fs::NativeFilesystem::isEmpty(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::is_empty(getFullPath(path), err);
    checkErr(err);
    return res;
}
std::chrono::file_clock::time_point fs::NativeFilesystem::lastTimeWrite(Path const &path) {
    std::error_code err;
    auto res = std::filesystem::last_write_time(getFullPath(path), err);
    checkErr(err);
    return res;
};
fs::IFile *fs::NativeFilesystem::open(Path const &path, FileOpenMode::Flags mode) {
    auto fullPath = getFullPath(path);
    if(!exists(fullPath)) {
        return nullptr;
    }

    uint id = mNextId++;
    mFiles.emplace(id, FileHandle{
        .path = fullPath,
        .file = std::unique_ptr<NativeFile>(new NativeFile(fullPath, mode, id)),
        .id = id
    });
    auto &handle = mFiles.at(id);
    handle.file->setParent(this);

    return handle.file.get();
}
void fs::NativeFilesystem::setBasePath(Path const &path) {
    mBasePath = path;
}

