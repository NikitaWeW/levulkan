#include "ArchiveFilesystem.hpp"
#include "Logging.hpp"
#include <filesystem>

static zip_stat_t getStat(zip_t *zip, fs::Path path) {
    zip_stat_t stat;
    zip_stat_init(&stat);

    int res = zip_stat(zip, path.string().c_str(), 0, &stat);
    assert(res == 0);

    return stat;
}
static zip_stat_t getStat(zip_t *zip, zip_int32_t id) {
    zip_stat_t stat;
    zip_stat_init(&stat);

    int res = zip_stat_index(zip, id, 0, &stat);
    assert(res == 0);

    return stat;
}

void fs::ArchiveFile::open(Path path, FileOpenMode::Flags mode, uint id, zip_t *zip, bool compression) {
    mHandleId = id;
    mZip = zip;
    mPath = path;
    mFileId = zip_name_locate(mZip, path.string().c_str(), 0);

    _close();

    if(zip_set_file_compression(mZip, mFileId, compression ? ZIP_CM_DEFAULT : ZIP_CM_STORE, 9) != 0) {
        zip_error *err = zip_get_error(mZip);
        LOG_ERROR("Failed to set compression for {}: {}", path.string(), zip_error_strerror(err));
    }

    if(mPassword.empty())
        mFile = zip_fopen_index(mZip, mFileId, 0);
    else
        mFile = zip_fopen_index_encrypted(mZip, mFileId, 0, mPassword.data());
    if(!mFile) {
        zip_error *err = zip_get_error(mZip);
        LOG_ERROR("Failed to open {} from {}: {}", path.string(), mParent->getBasePath().string(), zip_error_strerror(err));
    }


    // Not to override old file data with zeros
    mData.reserve(size() * 1.5);
    mData.resize(size());
    if(!(mode & FileOpenMode::Truncate)) {
        read(mData.data(), mData.size());
    }
}
void fs::ArchiveFile::setParent(ArchiveFilesystem *parent) {
    mParent = parent;
}
void fs::ArchiveFile::setPassword(std::string_view password) {
    mPassword = password;
}

bool fs::ArchiveFile::isOpen() const {
    return mFile;
}

void fs::ArchiveFile::_close() {
    if(mFile) {
        zip_fclose(mFile);
        mFile = nullptr;
    }
}
void fs::ArchiveFile::close() {
    _close();
    if(mParent) {
        mParent->close(mHandleId);
    }
}
fs::FileOpenMode::Flags fs::ArchiveFile::getMode() const {
    return mMode;
}
void fs::ArchiveFile::seekg(intmax_t position) {
    assert(isOpen());
    auto res = zip_fseek(mFile, position, 0);
    if(res != 0) {
        zip_error *err = zip_file_get_error(mFile);
        LOG_ERROR("Failed to seek file {} to {}: {}", mPath.string(), position, zip_error_strerror(err));
    }
}
void fs::ArchiveFile::seekg(intmax_t offset, SeekDir dir) {
    assert(isOpen());
    int direction;
    switch(dir) {
    case SeekDir::Beg: direction = SEEK_SET; break;
    case SeekDir::End: direction = SEEK_END; break;
    case SeekDir::Cur: direction = SEEK_CUR; break;
    default:           direction = SEEK_SET; break;
    }

    auto res = zip_fseek(mFile, offset, direction);
    if(res != 0) {
        std::string_view dirStr;
        switch(dir) {
        case SeekDir::Beg: dirStr = "SeekDir::Beg"; break;
        case SeekDir::End: dirStr = "SeekDir::End"; break;
        case SeekDir::Cur: dirStr = "SeekDir::Cur"; break;
        default:           dirStr = "SeekDir::Beg (default)"; break;
        }
        zip_error *err = zip_file_get_error(mFile);
        LOG_ERROR("Failed to seek file {} to {} from: {}", mPath.string(), offset, dirStr, zip_error_strerror(err));
    }
}
uintmax_t fs::ArchiveFile::tellg() {
    assert(isOpen());
    return zip_ftell(mFile);
}
void fs::ArchiveFile::seekp(intmax_t position) {
    assert(isOpen());
    mWriteHead = position;
}
void fs::ArchiveFile::seekp(intmax_t offset, SeekDir dir) {
    assert(isOpen());
    switch(dir) {
    case SeekDir::Beg: 
        mWriteHead = offset; 
        break;
    case SeekDir::End: 
        mWriteHead = std::max<intmax_t>(size() - offset, 0); 
        break;
    case SeekDir::Cur: 
        mWriteHead = std::max<intmax_t>(tellp() + offset, 0); 
        break;
    default:
        break;
    }
}
uintmax_t fs::ArchiveFile::tellp() {
    assert(isOpen());
    return mWriteHead;
}
uintmax_t fs::ArchiveFile::size() {
    assert(isOpen());
    return getStat(mZip, mFileId).size;
}
void fs::ArchiveFile::read(void *dst, uintmax_t size) {
    assert(isOpen());
    auto res = zip_fread(mFile, dst, size);
    if(res != 0) {
        zip_error *err = zip_file_get_error(mFile);
        LOG_ERROR("Failed to read {} bytes from file {}: {}", size, mPath.string(), zip_error_strerror(err));
    }
}
void fs::ArchiveFile::sync() {
    assert(isOpen());
    mParent->sync();
}
void fs::ArchiveFile::write(void const *src, uintmax_t size) {
    if(mMode & FileOpenMode::Append) {
        seekp(0, SeekDir::End);
    }
    assert(isOpen());
    auto newSize = tellp() + size;
    if(newSize < mData.size())
        mData.resize(newSize);

    std::memcpy(mData.data() + tellp(), src, size);
}
void fs::ArchiveFile::flush() {
    assert(isOpen());
    zip_source_t *source = zip_source_buffer(mZip, mData.data(), mData.size(), 0);
    zip_file_replace(mZip, mFileId, source, 0);
}

void fs::ArchiveFilesystem::sync() {
    uintmax_t basePathWriteTime = 0;
    bool existed = std::filesystem::exists(getBasePath().string());
    if(existed) {
        basePathWriteTime = std::filesystem::last_write_time(getBasePath().string()).time_since_epoch().count();
    }
    if(!mBasePathChanged && mBasePathLastWrite >= static_cast<uintmax_t>(basePathWriteTime)) {
        return;
    }
    
    close();

    int errorCode = 0;
    int flags = ZIP_CREATE;
    if(!existed) {
        flags |= ZIP_TRUNCATE;
        std::ofstream(getBasePath().string());
    }
    mZip = zip_open(getBasePath().string().c_str(), flags, &errorCode);
    if(!mZip) {
        zip_error_t error;
        zip_error_init_with_code(&error, errorCode);
        LOG_ERROR("Failed to open zip archive {}: {}", getBasePath().string(), zip_error_strerror(&error));
        if(errorCode == 0)
            LOG_ERROR("Errno: {}", strerror(errno));
        zip_error_fini(&error);
        assert(mZip);
        return;
    }

    for(auto &handle : mOpenedFiles.range()) {
        handle.file->open(handle.path, handle.file->getMode(), handle.id, mZip, mCompress);
    }

    mBasePathLastWrite = basePathWriteTime;
    mBasePathChanged = false;
}
void fs::ArchiveFilesystem::close() {
    for(auto &handle : mOpenedFiles.range()) {
        handle.file->close();
    }

    if(mZip) {
        zip_close(mZip);
        mZip = nullptr;
    }
}
void fs::ArchiveFilesystem::discard() {
    if(mZip) {
        zip_discard(mZip);
        mZip = nullptr;
    }
}
void fs::ArchiveFilesystem::close(uint id) {
    assert(mOpenedFiles.contains(id));
    auto &file = mOpenedFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    mOpenedFiles.erase(id);
}
fs::ArchiveFilesystem::ArchiveFilesystem() = default;
fs::ArchiveFilesystem::~ArchiveFilesystem() {
    close();
}
fs::Path fs::ArchiveFilesystem::getFullPath(Path const &path) const {
    return mBasePath / path;
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
    std::filesystem::create_directories(path.parentPath().string());

    mBasePath = path;
    mBasePathChanged = true;
    sync();
}
fs::Path fs::ArchiveFilesystem::getBasePath() const {
    return mBasePath;
}
void fs::ArchiveFilesystem::copy(Path const &from, Path const &to) {
    assert(mZip && "You need to call setBasePath first!");
    if(isDirectory(from) && !isDirectory(to)) {
        LOG_ERROR("Cannot copy directory {} to {} (file) in archive", from.string(), to.string(), getBasePath().string());
        return;
    }

    std::vector<Path> files;

    if(isDirectory(from))
        files = getContents(from, true);
    else
        files = { from };

    int64_t index = zip_name_locate(mZip, from.string().c_str(), 0);
    auto dstName = isRegularFile(to) ? to : to / from.filename();

    zip_error_t err;
    zip_source_t *source = zip_source_zip_file_create(mZip, index, 0, 0, -1, mPassword.c_str(), &err);
    if(!source) {
        LOG_ERROR("Failed to copy file from {} to {} from archive {}: {}", from.string(), to.string(), getBasePath().string(), zip_error_strerror(&err));
        return;
    }

    auto res = zip_file_add(mZip, dstName.string().c_str(), source, 0);
    if(res != 0) {
        auto *err = zip_get_error(mZip);
        LOG_ERROR("Failed to copy \"{}\" to \"{}\" in archive \"{}\": {}", from.string(), to.string(), getBasePath().string(), zip_error_strerror(err));
    }

    zip_source_free(source);
}
void fs::ArchiveFilesystem::move(Path const &from, Path const &to) {
    assert(mZip && "You need to call setBasePath first!");
    auto res = zip_file_rename(mZip, getStat(mZip, from).index, to.string().c_str(), 0);
    if(res != 0) {
        auto *err = zip_get_error(mZip);
        LOG_ERROR("Failed to move \"{}\" to \"{}\" in archive \"{}\": {}", from.string(), to.string(), getBasePath().string(), zip_error_strerror(err));
    }
}
void fs::ArchiveFilesystem::createDirectory(Path const &path) {
    assert(mZip && "You need to call setBasePath first!");
    auto res = zip_dir_add(mZip, getFullPath(path).string().c_str(), 0);
    if(res != 0) {
        auto *err = zip_get_error(mZip);
        LOG_ERROR("Failed to create directory \"{}\" in archive \"{}\": {}", path.string(), getBasePath().string(), zip_error_strerror(err));
    }
}
void fs::ArchiveFilesystem::createDirectories(Path const &path) {
    assert(mZip && "You need to call setBasePath first!");
    Path dirPath = isDirectory(path) ? path : path.parentPath();
    while(!(dirPath = dirPath.parentPath()).empty()) {
        createDirectory(getFullPath(dirPath));
    }
}
bool fs::ArchiveFilesystem::exists(Path const &path) const {
    assert(mZip && "You need to call setBasePath first!");
    return zip_name_locate(mZip, path.string().c_str(), 0) != -1;
}
uintmax_t fs::ArchiveFilesystem::fileSize(Path const &path) const {
    assert(mZip && "You need to call setBasePath first!");
    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path.string());
        return 0;
    }

    return getStat(mZip, path).size;
}
void fs::ArchiveFilesystem::remove(Path const &path) {
    assert(mZip && "You need to call setBasePath first!");
    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path.string());
        return;
    }

    auto index = zip_name_locate(mZip, path.string().c_str(), 0);
    auto res = zip_delete(mZip, index);

    if(res != 0) {
        auto *err = zip_get_error(mZip);
        LOG_ERROR("Failed to remove \"{}\" in archive \"{}\": {}", path.string(), getBasePath().string(), zip_error_strerror(err));
    }
}
void fs::ArchiveFilesystem::removeAll(Path const &path) {
    remove(path);
}
std::vector<fs::Path> fs::ArchiveFilesystem::getContents(Path const &path, bool recursive) const {
    auto fullPath = getFullPath(path);

    if(!exists(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" doesent exist!", path.string());
        return {};
    }
    if(!isDirectory(path)) {
        LOG_ERROR("fs::ArchiveFilesystem: path \"{}\" is not a directory!", path.string());
        return {};
    }

    auto numEntries = zip_get_num_entries(mZip, 0);
    auto prefix = path.string();

    std::vector<fs::Path> contents;
    contents.reserve(numEntries / 20u);

    LOG_VAR(prefix);

    for(int64_t i = 0; i < numEntries; ++i) {
        std::string_view name = zip_get_name(mZip, i, 0);
        LOG_TRACE("i: {}; name: {}", i, name.data() ? name : "0x0");
        if(!name.data()) { // Deleted
            LOG_TRACE("Deleted");
            continue; 
        }

        if(name.compare(0, prefix.size(), prefix)) {
            LOG_TRACE("Added");
            contents.emplace_back(name);
        }
    }

    return contents;
}
bool fs::ArchiveFilesystem::isDirectory(Path const &path) const {
    auto fullPath = getFullPath(path);
    if(!exists(path)) {
        return false;
    }
    
    zip_stat_t stat = getStat(mZip, fullPath);

    return std::string_view(stat.name).back() == '/';
}
bool fs::ArchiveFilesystem::isRegularFile(Path const &path) const {
    return exists(path) && !isDirectory(path);
}
bool fs::ArchiveFilesystem::isEmpty(Path const &path) const {
    return fileSize(path) == 0;
}
std::chrono::file_clock::time_point fs::ArchiveFilesystem::lastTimeWrite(Path const &path) const {
    auto fullPath = getFullPath(path);
    if(!exists(fullPath)) {
        LOG_ERROR("Path \"{}\" doesent exist in archive \"{}\"!", path.string(), getBasePath().string());
        return std::chrono::file_clock::now();
    }

    time_t time = getStat(mZip, fullPath).mtime;
    return std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(time));
}
fs::FileHandle fs::ArchiveFilesystem::open(Path const &path, FileOpenMode::Flags mode) {
    auto fullPath = getFullPath(path);

    uint id = mNextId++;
    mOpenedFiles.emplace(id, Handle{
        .path = fullPath,
        .file = std::unique_ptr<ArchiveFile>(new ArchiveFile),
        .id = id
    });
    auto &handle = mOpenedFiles.at(id);
    handle.file->setParent(this);
    handle.file->setPassword(mPassword);
    handle.file->open(handle.path.string(), mode, id, mZip, mCompress);

    return FileHandle(handle.file.get());
}
void fs::ArchiveFilesystem::setPassword(std::string_view password) {
    mPassword = password;
}
void fs::ArchiveFilesystem::setCompression(bool compress) {
    mCompress = compress;
}
