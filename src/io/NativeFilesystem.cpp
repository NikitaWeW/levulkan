#include "engine/io/fs/NativeFilesystem.hpp"
#include "engine/io/Logging.hpp"

#include <filesystem>
#include <unordered_set>

egn::fs::NativeFile::NativeFile(Path path, FileOpenMode::Flags mode, uint id) {
    mId = id;
    mMode = mode;
    std::ios::openmode iosMode = std::ios::in | std::ios::out | std::ios::binary;

    if(mode & FileOpenMode::Append)
        iosMode |= std::ios::app;
    if(mode & FileOpenMode::Truncate)
        iosMode |= std::ios::trunc;

    if(!std::filesystem::exists(path.string())) {
        std::ofstream createFileIfDoesentExistBecauseCppStdLibIsFuckingUnusable(path.string());
        createFileIfDoesentExistBecauseCppStdLibIsFuckingUnusable.close();
    }

    mFile.open(path.string(), iosMode);
}
egn::fs::NativeFile::~NativeFile() {
}
void egn::fs::NativeFile::setFileHandle(FileHandle *handle) {
    mHandle = handle;
}
void egn::fs::NativeFile::setParentFilesystem(NativeFilesystem *parent) {
    mParent = parent;
}
void egn::fs::NativeFile::seekg(intmax_t position) {
    assert(isOpen());
    mFile.seekg(position);
}
void egn::fs::NativeFile::seekg(intmax_t offset, SeekDir dir) {
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
uintmax_t egn::fs::NativeFile::tellg() {
    assert(isOpen());
    return mFile.tellg();
}
void egn::fs::NativeFile::seekp(intmax_t position) {
    assert(isOpen());
    mFile.seekp(position);
}
void egn::fs::NativeFile::seekp(intmax_t offset, SeekDir dir) {
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
uintmax_t egn::fs::NativeFile::tellp() {
    assert(isOpen());
    return mFile.tellp();
}
bool egn::fs::NativeFile::isOpen() const {
    return mFile.is_open();
}
void egn::fs::NativeFile::close() {
    assert(isOpen());
    mFile.close();
    if(mHandle)
        mHandle->close();
    if(mParent)
        mParent->close(mId);
}
uintmax_t egn::fs::NativeFile::size() {
    assert(isOpen());
    auto pos = mFile.tellg();
    mFile.seekg(0, std::ios_base::end);
    auto size = mFile.tellg();
    mFile.seekg(pos);
    return size;
}
void egn::fs::NativeFile::read(void *dst, uintmax_t size) {
    assert(isOpen());
    mFile.read(static_cast<char *>(dst), size);
}
void egn::fs::NativeFile::sync() {
    assert(isOpen());
    mFile.sync();
}
void egn::fs::NativeFile::write(void const *src, uintmax_t size) {
    assert(isOpen());
    mFile.write(static_cast<char const *>(src), size);
}
void egn::fs::NativeFile::flush() {
    assert(isOpen());
    mFile.flush();
}
std::fstream &egn::fs::NativeFile::getFileHandle() {
    return mFile;
}

void egn::fs::NativeFilesystem::checkErr(std::error_code err, Error *outErr, std::string path) const {
    if(err) {
        auto msg = fmt::format("Native filesystem {} error: {}", err.category().name(), err.message());
        if(outErr) {
            *outErr = {
                .failed = true,
                .message = msg,
                .path = getFullPath(path).string()
            };
        } else {
            LOG_ERROR(msg);
            LOG_WARN("Error path: \"{}\"", getFullPath(path).string());
            assert(false && "Filesystem error");
        }
    }

}
void egn::fs::NativeFilesystem::close(uint id) {
    assert(mFiles.contains(id));
    auto &file = mFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    // Because this piece of junk is so bad,
    // The egn::fs::NativeFilesystem::close is called twice from egn::fs::NativeFile::close
    if(mFiles.contains(id))
        mFiles.erase(id);
}
egn::fs::NativeFilesystem::NativeFilesystem(Path basePath) {
    setBasePath(basePath);
}
egn::fs::NativeFilesystem::NativeFilesystem() = default;
egn::fs::NativeFilesystem::~NativeFilesystem() {
    for(auto id : mFiles.sparse()) {
        close(id);
    }
}
egn::fs::Path egn::fs::NativeFilesystem::getFullPath(Path const &path) const {
    return mBasePath / path.makeAbsolute("");
}
void egn::fs::NativeFilesystem::closeIfInUse(egn::fs::Path const &path) {
    if(!exists(path)) 
        return;


    std::unordered_set<std::string> paths;
    if(isDirectory(path)) {
        for(auto const &path : getContents(path, true)) {
            if(isRegularFile(path)) {
                paths.emplace(path.string());
            }
        }
    } else {
        paths.emplace(path.makeAbsolute("").removeDirSeparator().string());
    }
    std::vector<uint> toClose;
    for(auto const &file : mFiles.dense()) {
        if(paths.contains(file.path.string())) {
            toClose.emplace_back(file.openId);
        }
    }

    for(auto const &id : toClose) {
        close(id);
    }
}
egn::fs::NativeFilesystem &egn::fs::NativeFilesystem::operator=(NativeFilesystem &&rhs) {
    if(this == &rhs)
        return *this;

    mFiles = std::move(rhs.mFiles);
    mNextId = rhs.mNextId;
    
    for(auto &handle : mFiles.dense()) {
        handle.file->setParentFilesystem(this);
    }

    return *this;
}
egn::fs::NativeFilesystem::NativeFilesystem(NativeFilesystem &&rhs) {
    *this = std::move(rhs);
}
void egn::fs::NativeFilesystem::copy(Path const &from, Path const &to, Error *outErr) {
    if(exists(to, outErr) && isRegularFile(to, outErr))
        remove(to, outErr);
    if(outErr && outErr->failed)
        return;

    closeIfInUse(to);

    std::error_code err;
    std::filesystem::copy(getFullPath(from).string(), getFullPath(to).string(), std::filesystem::copy_options::recursive, err);
    checkErr(err, outErr, fmt::format("(src: {}, dst: {})", from.string(), to.string()));
}
void egn::fs::NativeFilesystem::move(Path const &from, Path const &to, Error *outErr) {
    closeIfInUse(from);
    closeIfInUse(to);

    std::error_code err;
    std::filesystem::rename(getFullPath(from).string(), exists(to) && isDirectory(to) ? getFullPath(to/from.filename()).string() : getFullPath(to).string(), err);
    checkErr(err, outErr, fmt::format("(src: {}, dst: {})", from.string(), to.string()));
}

void egn::fs::NativeFilesystem::createDirectory(Path const &path, Error *outErr) {
    std::error_code err;
    std::filesystem::create_directory(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
}
void egn::fs::NativeFilesystem::createDirectories(Path const &path, Error *outErr) {
    std::error_code err;
    std::filesystem::create_directories(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
}
bool egn::fs::NativeFilesystem::exists(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::exists(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;
}
uintmax_t egn::fs::NativeFilesystem::fileSize(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::file_size(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;
}
void egn::fs::NativeFilesystem::remove(Path const &path, Error *outErr) {
    bool isDir = isDirectory(path, outErr);
    if(outErr && outErr->failed)
        return;

    closeIfInUse(path);

    std::error_code err;
    if(isDir) {
        std::filesystem::remove_all(getFullPath(path).string(), err);
    } else {
        std::filesystem::remove(getFullPath(path).string(), err);
    }
    checkErr(err, outErr, path.string());
}
std::vector<egn::fs::Path> egn::fs::NativeFilesystem::getContents(Path const &path, bool recursive, Error *outErr) const {
    std::vector<Path> res;

    std::error_code err;
    if(recursive) {
        for(auto entry : std::filesystem::recursive_directory_iterator(getFullPath(path).string(), err)) {
            res.emplace_back(egn::fs::Path(entry.path().string()).makeRelative(mBasePath).makeAbsolute("").removeDirSeparator());
        }
    } else {
        for(auto entry : std::filesystem::directory_iterator(getFullPath(path).string(), err)) {
            res.emplace_back(egn::fs::Path(entry.path().string()).makeRelative(mBasePath).makeAbsolute("").removeDirSeparator());
        }
    }

    checkErr(err, outErr, path.string());

    return res;
}
bool egn::fs::NativeFilesystem::isDirectory(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_directory(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;

}
bool egn::fs::NativeFilesystem::isRegularFile(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_regular_file(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;
}
bool egn::fs::NativeFilesystem::isEmpty(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::is_empty(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;
}
std::chrono::file_clock::time_point egn::fs::NativeFilesystem::lastTimeWrite(Path const &path, Error *outErr) const {
    std::error_code err;
    auto res = std::filesystem::last_write_time(getFullPath(path).string(), err);
    checkErr(err, outErr, path.string());
    return res;
};
egn::fs::FileHandle egn::fs::NativeFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *outErr) {
    auto fullPath = getFullPath(path);

    uint id = mNextId++;
    mFiles.emplace(id, Handle{
        .path = path.makeAbsolute(""),
        .file = std::unique_ptr<NativeFile>(new NativeFile(fullPath, mode, id)),
        .openId = id,
    });
    auto &handle = mFiles.at(id);
    handle.file->setParentFilesystem(this);

    if(outErr && !handle.file->isOpen()) {
        outErr->failed = true;
        outErr->message = fmt::format("Failed to open a native file {}!", fullPath.string());
        outErr->path = path.string();
    }

    return FileHandle(handle.file.get());
}
void egn::fs::NativeFilesystem::setBasePath(Path const &path) {
    std::filesystem::create_directories(path.string());
    mBasePath = path;
}
egn::fs::Path egn::fs::NativeFilesystem::getBasePath() const {
    return mBasePath;
}

