#include "NativeFilesystem.hpp"
#include "Logging.hpp"

#include <filesystem>

fs::NativeFile::NativeFile(Path path, FileOpenMode::Flags mode, uint id) {
    mId = id;
    std::ios::openmode iosMode = std::ios::in | std::ios::out | std::ios::binary;

    if(mode & FileOpenMode::Append)
        iosMode |= std::ios::app;
    if(mode & FileOpenMode::Truncate)
        iosMode |= std::ios::trunc;

    iosMode |= std::ios::ate;

    mFile.open(path.string(), iosMode);
}
void fs::NativeFile::setParent(NativeFilesystem *parent) {
    mParent = parent;
}
void fs::NativeFile::seekg(intmax_t position) {
    assert(isOpen());
    mFile.seekg(position);
}
void fs::NativeFile::seekg(intmax_t offset, SeekDir dir) {
    assert(isOpen());
    std::ios::seekdir direction;
    switch(dir) {
    case SeekDir::Beg: direction = std::ios::beg; break;
    case SeekDir::End: direction = std::ios::end; break;
    case SeekDir::Cur: direction = std::ios::cur; break;
    default:           direction = std::ios::beg; break;
    }

    mFile.seekg(offset, direction);
}
uintmax_t fs::NativeFile::tellg() {
    assert(isOpen());
    return mFile.tellg();
}
void fs::NativeFile::seekp(intmax_t position) {
    assert(isOpen());
    mFile.seekp(position);
}
void fs::NativeFile::seekp(intmax_t offset, SeekDir dir) {
    assert(isOpen());
    std::ios::seekdir direction;
    switch(dir) {
    case SeekDir::Beg: direction = std::ios::beg; break;
    case SeekDir::End: direction = std::ios::end; break;
    case SeekDir::Cur: direction = std::ios::cur; break;
    default:           direction = std::ios::beg; break;
    }

    mFile.seekp(offset, direction);
}
uintmax_t fs::NativeFile::tellp() {
    assert(isOpen());
    return mFile.tellp();
}
bool fs::NativeFile::isOpen() const {
    return mFile.is_open();
}
void fs::NativeFile::close() {
    assert(isOpen());
    mFile.close();
    if(mParent)
        mParent->close(mId);
}
uintmax_t fs::NativeFile::size() {
    assert(isOpen());
    auto pos = mFile.tellg();
    mFile.seekg(0, std::ios_base::end);
    auto size = mFile.tellg();
    mFile.seekg(pos);
    return size;
}
void fs::NativeFile::read(void *dst, uintmax_t size) {
    assert(isOpen());
    mFile.read(static_cast<char *>(dst), size);
}
void fs::NativeFile::sync() {
    assert(isOpen());
    mFile.sync();
}
void fs::NativeFile::write(void const *src, uintmax_t size) {
    assert(isOpen());
    mFile.write(static_cast<char const *>(src), size);
}
void fs::NativeFile::flush() {
    assert(isOpen());
    mFile.flush();
}
std::fstream &fs::NativeFile::getFileHandle() {
    return mFile;
}

void fs::NativeFilesystem::checkErr(std::error_code err, Error *outErr) const {
    if(err) {
        auto msg = fmt::format("Native filesystem {} error: {}", err.category().name(), err.message());
        if(outErr) {
            *outErr = {
                .failed = true,
                .message = msg
            };
        } else {
            LOG_ERROR(msg);
            assert(false && "Filesystem error");
        }
    }

}
void fs::NativeFilesystem::close(uint id) {
    assert(mFiles.contains(id));
    auto path = mFiles.at(id).path;
    auto &file = mFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    mFiles.erase(id);
}
fs::NativeFilesystem::NativeFilesystem(Path basePath) {
    setBasePath(basePath);
}
fs::NativeFilesystem::NativeFilesystem() = default;
fs::NativeFilesystem::~NativeFilesystem() {
    for(auto id : mFiles.sparse()) {
        close(id);
    }
}
fs::Path fs::NativeFilesystem::getFullPath(Path const &path) const {
    return mBasePath / path;
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
void fs::NativeFilesystem::copy(Path const &from, Path const &to, Error *outErr) {
    std::filesystem::copy_options copyOptions = std::filesystem::copy_options::none;
    if(isDirectory(from))
        copyOptions = std::filesystem::copy_options::recursive;

    std::error_code err;
    std::filesystem::copy(getFullPath(from).string(), getFullPath(to).string(), copyOptions, err);
    checkErr(err, outErr);
}
void fs::NativeFilesystem::move(Path const &from, Path const &to, Error *outErr) {
    std::error_code err;
    std::filesystem::rename(getFullPath(from).string(), getFullPath(to).string(), err);
    checkErr(err, outErr);
}

void fs::NativeFilesystem::createDirectory(Path const &path, Error *outErr) {
    std::error_code err;
    std::filesystem::create_directory(getFullPath(path).string(), err);
    checkErr(err, outErr);
}
void fs::NativeFilesystem::createDirectories(Path const &path, Error *outErr) {
    std::error_code err;
    std::filesystem::create_directories(getFullPath(path).string(), err);
    checkErr(err, outErr);
}
bool fs::NativeFilesystem::exists(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::exists(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;
}
uintmax_t fs::NativeFilesystem::fileSize(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::file_size(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;
}
void fs::NativeFilesystem::remove(Path const &path, Error *outErr) {
    std::error_code err;
    if(isDirectory(path, outErr)) {
        std::filesystem::remove_all(getFullPath(path).string(), err);
    } else {
        std::filesystem::remove(getFullPath(path).string(), err);
    }
    checkErr(err, outErr);
}
std::vector<fs::Path> fs::NativeFilesystem::getContents(Path const &path, bool recursive, Error *outErr) const {
    std::vector<Path> res;

    std::error_code err;
    if(recursive) {
        for(auto entry : std::filesystem::recursive_directory_iterator(getFullPath(path).string(), err)) {
            res.emplace_back(fs::Path(entry.path().string()).makeRelative(mBasePath));
        }
    } else {
        for(auto entry : std::filesystem::directory_iterator(getFullPath(path).string(), err)) {
            res.emplace_back(fs::Path(entry.path().string()).makeRelative(mBasePath));
        }
    }

    checkErr(err, outErr);

    return res;
}
bool fs::NativeFilesystem::isDirectory(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_directory(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;

}
bool fs::NativeFilesystem::isRegularFile(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_regular_file(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;
}
bool fs::NativeFilesystem::isEmpty(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_empty(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;
}
std::chrono::file_clock::time_point fs::NativeFilesystem::lastTimeWrite(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::last_write_time(getFullPath(path).string(), err);
    checkErr(err, outErr);
    return res;
};
fs::FileHandle fs::NativeFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *outErr) {
    auto fullPath = getFullPath(path);

    uint id = mNextId++;
    mFiles.emplace(id, Handle{
        .path = fullPath,
        .file = std::unique_ptr<NativeFile>(new NativeFile(fullPath, mode, id)),
        .id = id
    });
    auto &handle = mFiles.at(id);
    handle.file->setParent(this);

    if(outErr && !handle.file->isOpen()) {
        outErr->failed = true;
        outErr->message = fmt::format("Failed to open a native file {}!", fullPath.string());
    }

    return FileHandle(handle.file.get());
}
void fs::NativeFilesystem::setBasePath(Path const &path) {
    std::filesystem::create_directories(path.string());
    mBasePath = path;
}
fs::Path fs::NativeFilesystem::getBasePath() const {
    return mBasePath;
}

