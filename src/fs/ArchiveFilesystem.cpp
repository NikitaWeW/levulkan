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

static zip_stat_t getStat(zip_t *zip, fs::Path path) {
    zip_stat_t stat;
    zip_stat_init(&stat);

    int res = zip_stat(zip, path.c_str(), 0, &stat);
    assert(res == 0);

    return stat;
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
    mZip = rhs.mZip;
    
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

    if(mZip) {
        zip_close(mZip);
    }

    int errorCode = 0;
    mZip = zip_open(mBasePath.c_str(), ZIP_CREATE, &errorCode);
    if(!mZip) {
        zip_error_t error;
        zip_error_init_with_code(&error, errorCode);
        LOG_ERROR("Failed to open zip archive {}: {}", mBasePath, zip_error_strerror(&error));
        zip_error_fini(&error);
    }
    assert(mZip);
}
void fs::ArchiveFilesystem::copy(Path const &from, Path const &to, CopyOptions options) {
    assert(mZip && "You need to call setBasePath first!");
    int64_t index = zip_name_locate(mZip, from.c_str(), 0);

    zip_error_t err;
    zip_source_t *source = zip_source_zip_file_create(mZip, index, 0, 0, -1, mPassword.c_str(), &err);
    if(!source) {
        LOG_ERROR("Failed to copy file from {} to {} from archive {}: {}", from, to, mBasePath, zip_error_strerror(&err));
        return;
    }

    zip_file_add(mZip, to.c_str(), source, 0);
    zip_source_free(source);
}
void fs::ArchiveFilesystem::createDirectory(Path const &path) {
    zip_dir_add(mZip, path.c_str(), 0);
}
void fs::ArchiveFilesystem::createDirectories(Path const &path) {
    std::string_view delimiter = "/";
    std::vector<std::string> dirs;
    {
        size_t pos = 0;
        std::string token;
        while((pos = path.find(delimiter)) != std::string::npos) {
            token = path.substr(0, pos);
            dirs.push_back(token);
        }
        dirs.push_back(path);
    }
    // FIXME: no way this is right
    for(auto const &dir : dirs) {
        if(!exists(dir))
            createDirectory(dir);
    }
}
bool fs::ArchiveFilesystem::exists(Path const &path) {
    return zip_name_locate(mZip, path.c_str(), 0) != -1;
}
uintmax_t fs::ArchiveFilesystem::fileSize(Path const &path) {
    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path);
        return 0;
    }

    return getStat(mZip, path).size;
}
void fs::ArchiveFilesystem::remove(Path const &path) {
    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path);
        return;
    }

    auto index = zip_name_locate(mZip, path.c_str(), 0);
    zip_delete(mZip, index);
}
void fs::ArchiveFilesystem::removeAll(Path const &path) {
    remove(path);
}
std::vector<fs::Path> fs::ArchiveFilesystem::getContents(Path const &path, bool recursive) {
    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path);
        return {};
    }

    auto numEntries = zip_get_num_entries(mZip, 0);
    auto prefix = path;
    if(prefix.back() != '/')
        prefix.insert(prefix.back(), "/");

    for(int64_t i = 0; i < numEntries; ++i) {
        auto name = zip_get_name(mZip, i, 0);
        if(!name)
            continue; // Deleted

        // Yeah please finish this
        // Also
        // FIXME: use getFullPath()
    }
}
bool fs::ArchiveFilesystem::isDirectory(Path const &path) {
}
bool fs::ArchiveFilesystem::isRegularFile(Path const &path) {
}
bool fs::ArchiveFilesystem::isEmpty(Path const &path) {
}
std::chrono::file_clock::time_point fs::ArchiveFilesystem::lastTimeWrite(Path const &path) {

}
fs::IFile *fs::ArchiveFilesystem::open(Path const &path, FileOpenMode::Flags mode) {
}
void fs::ArchiveFilesystem::setPassword(std::string_view password) {
    mPassword = password;
}

