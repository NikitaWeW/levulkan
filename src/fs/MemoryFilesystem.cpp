#include "MemoryFilesystem.hpp"
#include "Logging.hpp"
#include "nlohmann/json.hpp"

fs::MemoryFile::MemoryFile(FileOpenMode::Flags mode, uint fileId) {
    mFileId = fileId;
    mMode = mode;
}
void fs::MemoryFile::open(uint openId) {
    mOpenId = openId;
    assert(mParent);
    assert(mParent->mFileContents.contains(mFileId));
    mBuffer.reserve(mParent->mFileContents.at(mFileId).size() * 1.5);

    if(!(mMode | FileOpenMode::Truncate)) {
        sync();
    }

    mOpen = true;
}
fs::MemoryFile::~MemoryFile() {
}
void fs::MemoryFile::setParent(MemoryFilesystem *parent) {
    mParent = parent;
}
void fs::MemoryFile::seekg(intmax_t position) {
    mReadHead = position;
}
void fs::MemoryFile::seekg(intmax_t offset, SeekDir dir) {
    switch(dir) {
    case SeekDir::Beg: 
        mReadHead = offset;
        break;
    case SeekDir::End: 
        mReadHead = size() - offset; 
        break;
    case SeekDir::Cur: 
        mReadHead += offset;
        break;
    default:           
        mReadHead = offset;
        break;
    }
    assert(mReadHead <= size());
}
uintmax_t fs::MemoryFile::tellg() {
    return mReadHead;
}
void fs::MemoryFile::seekp(intmax_t position) {
    mWriteHead = position;
}
void fs::MemoryFile::seekp(intmax_t offset, SeekDir dir) {
    switch(dir) {
    case SeekDir::Beg: mWriteHead = offset;          break;
    case SeekDir::End: mWriteHead = size() - offset; break;
    case SeekDir::Cur: mWriteHead += offset;         break;
    default:           mWriteHead = offset;          break;
    }
}
uintmax_t fs::MemoryFile::tellp() {
    return mWriteHead;
}
bool fs::MemoryFile::isOpen() const {
    return mOpen;
}
void fs::MemoryFile::close() {
    flush();
    mOpen = false;
    if(mParent)
        mParent->close(mOpenId);
}
uintmax_t fs::MemoryFile::size() {
    return mBuffer.size();
}
void fs::MemoryFile::read(void *dst, uintmax_t size) {
    std::memcpy(dst, mBuffer.data() + mReadHead, std::clamp<uintmax_t>(mReadHead + size, 0, this->size()) - mReadHead);
}
void fs::MemoryFile::sync() {
    mBuffer = mParent->mFileContents.at(mFileId);
}
void fs::MemoryFile::write(void const *src, uintmax_t size) {
    if(mMode | FileOpenMode::Append) {
        seekp(0, SeekDir::End);
    }

    if(mWriteHead + size >= this->size()) {
        mBuffer.resize(mWriteHead + size);
    }

    std::memcpy(mBuffer.data() + mWriteHead, src, std::clamp<uintmax_t>(mWriteHead + size, 0, this->size()) - mWriteHead);
}
void fs::MemoryFile::flush() {
    mParent->mFileContents.at(mFileId) = mBuffer;
    mParent->mDescriptors.at(mFileId).modificationTime = std::chrono::file_clock::now();
}

void fs::MemoryFilesystem::setErr(Error *err, std::string msg, std::string path) const {
    if(err) {
        auto message = fmt::format("Memory filesystem error: {}", msg);
        if(err) {
            *err = {
                .failed = true,
                .message = message,
                .path = path
            };
        } else {
            LOG_ERROR(msg);
            LOG_WARN("Error path: \"{}\"", path);
            assert(false && "Filesystem error");
        }
    }
}
uint fs::MemoryFilesystem::getFileIndex(fs::Path path) const {
    path = path.makeAbsolute("");
    if(mPathToDescIndex.contains(path.string())) {
        return mPathToDescIndex.at(path.string());
    } else {
        return 0;
    }
}
fs::MemoryFilesystem::Descriptor &fs::MemoryFilesystem::getDesc(fs::Path path, DescriptorType defaultType) {
    path = path.makeAbsolute("");
    uint index = getFileIndex(path);
    assert(exists(path.parentPath()));
    if(index == 0) {
        index = mPathToDescIndex[path.string()] = mNextId++;
        mDescriptors[index] = {
            .type = defaultType,
            .path = path,
            .id = index,
            .modificationTime = std::chrono::file_clock::now()
        };
    }

    auto &desc = mDescriptors.at(index);
    if(desc.type == DescriptorType::File && !mFileContents.contains(desc.id)) {
        mFileContents.emplace(desc.id);
    }

    return desc;
}

void fs::MemoryFilesystem::close(uint id) {
    assert(mFiles.contains(id));
    auto path = mFiles.at(id).path;
    auto &file = mFiles.at(id).file;
    
    if(file->isOpen())
        file->close();

    mFiles.erase(id);
}
fs::MemoryFilesystem::MemoryFilesystem() = default;
fs::MemoryFilesystem::~MemoryFilesystem() {
    for(auto id : mFiles.sparse()) {
        close(id);
    }
}
fs::MemoryFilesystem &fs::MemoryFilesystem::operator=(MemoryFilesystem &&rhs) {
    if(this == &rhs)
        return *this;

    mFiles = std::move(rhs.mFiles);
    mNextId = rhs.mNextId;
    
    for(auto &handle : mFiles.dense()) {
        handle.file->setParent(this);
    }

    return *this;
}
fs::MemoryFilesystem::MemoryFilesystem(MemoryFilesystem &&rhs) {
    *this = std::move(rhs);
}
bool fs::MemoryFilesystem::checkBeforeTransfer(fs::Path src, fs::Path dst, Error *err, std::string_view op) const {
    if(!exists(src)) {
        setErr(err, fmt::format("Cannot {} \"{}\" to \"{}\": no such file or directory!", op, src.string(), dst.string()), fmt::format("src: {}, dst: {}", src.string(), dst.string()));
        return false;
    }
    if(isDirectory(src) && exists(dst) && !isDirectory(dst)) {
        setErr(err, fmt::format("{}: Cannot overwrite non-directory \"{}\" with directory \"{}\"!", op, src.string(), dst.string()), fmt::format("src: {}, dst: {}", src.string(), dst.string()));
        return false;
    }
    if(!exists(dst.parentPath())) {
        setErr(err, fmt::format("Cannot {} \"{}\" to \"{}\": no such file or directory!", op, src.string(), dst.string()), fmt::format("src: {}, dst: {}", src.string(), dst.string()));
        return false;
    }
    if(!isDirectory(dst.parentPath())) {
        setErr(err, fmt::format("{}: Cannot overwrite non-directory \"{}\"!", op, dst.parentPath().string()), fmt::format("src: {}, dst: {}", src.string(), dst.string()));
        return false;
    }
    if(isDirectory(src) && exists(dst/src.filename()) && !isDirectory(dst/src.filename())) {
        setErr(err, fmt::format("{}: Cannot overwrite non-directory \"{}\" with directory \"{}\"!", op, src.string(), dst.string()), fmt::format("src: {}, dst: {}", src.string(), (dst/src.filename()).string()));
        return false;
    }
    return true;
}
void fs::MemoryFilesystem::copy(Path const &src, Path const &dst, Error *err) {
    if(!checkBeforeTransfer(src, dst, err, "copy"))
        return;

    auto const &srcDesc = mDescriptors.at(getFileIndex(src));
    
    auto dstDesc = getDesc(dst, srcDesc.type);
    if(srcDesc.type == DescriptorType::File && dstDesc.type == DescriptorType::Directory) {
        dstDesc = getDesc(dst/src.filename(), DescriptorType::File);
    }

    if(srcDesc.type == DescriptorType::File) {
        mFileContents.at(dstDesc.id) = mFileContents.at(srcDesc.id);
    } else if(srcDesc.type == DescriptorType::Directory) {
        auto contents = getContents(src, true, err);
        if(err && err->failed)
            return;
        for(auto path : contents) {
            if(isDirectory(path)) 
                continue;
            
            auto dstPath = dst/path.makeRelative(src);
            createDirectories(dstPath, err);
            if(err && err->failed)
                continue;
            auto const &desc = getDesc(dstPath, DescriptorType::File);
            mFileContents.at(desc.id) = mFileContents.at(getFileIndex(path));
        }
    }
}
void fs::MemoryFilesystem::move(Path const &src, Path const &dst, Error *err) {
    if(!checkBeforeTransfer(src, dst, err, "move"))
        return;

    auto &srcDesc = mDescriptors.at(getFileIndex(src));

    uint dstId = getFileIndex(dst);
    bool dstIsDirectory = dstId != 0 && mDescriptors.at(dstId).type == DescriptorType::Directory;

    if(srcDesc.type == DescriptorType::File) {
        srcDesc.path = dstIsDirectory ? dst : dst/src.filename();
    } else if(srcDesc.type == DescriptorType::Directory) {
        auto contents = getContents(src, true, err);
        if(err && err->failed)
            return;
        for(auto path : contents) {
            if(isDirectory(path)) 
                continue;

            auto &desc = mDescriptors.at(getFileIndex(path));
            auto dstPath = dst/path.makeRelative(src);
            createDirectories(dstPath.parentPath());
            desc.path = dstPath;
        }
    }
}

void fs::MemoryFilesystem::createDirectory(Path const &path, Error *err) {
    if(exists(path)) {
        setErr(err, fmt::format("Cannot create a directory: already exists!"), path.string());
        return;
    }
    if(!exists(path.parentPath())) {
        setErr(err, fmt::format("Cannot create a directory: no such file directory!"), path.string());
        return;
    }
    if(!isRegularFile(path.parentPath())) {
        setErr(err, fmt::format("Cannot create a directory: parent path is a file!"), path.string());
        return;
    }

    auto &desc = getDesc(path, DescriptorType::Directory);
}
void fs::MemoryFilesystem::createDirectories(Path const &path, Error *err) {
    auto parentPath = path;

    std::vector<fs::Path> toCreate;

    while(!exists(parentPath) && !parentPath.empty()) {
        toCreate.emplace_back(parentPath);
        parentPath = parentPath.parentPath();
    }

    std::reverse(toCreate.begin(), toCreate.end());

    for(auto const &path : toCreate) {
        createDirectory(path);
    }
}
bool fs::MemoryFilesystem::exists(Path const &path, Error *err) const {
    return path.empty() || getFileIndex(path) != 0;
}
uintmax_t fs::MemoryFilesystem::fileSize(Path const &path, Error *err) const {
    if(!exists(path)) {
        setErr(err, fmt::format("Cannot get file size: no such file or directory!"), path.string());
        return 0;
    }
    if(!isRegularFile(path)) {
        setErr(err, fmt::format("Cannot get file size of a directory!"), path.string());
        return 0;
    }
    return mFileContents.at(getFileIndex(path)).size();
}
void fs::MemoryFilesystem::remove(Path const &path, Error *err) {
    if(!exists(path))
        setErr(err, fmt::format("Cannot remove path: no such file or directory!"), path.string());

    auto index = getFileIndex(path);

    if(isRegularFile(path)) {
        mFileContents.erase(index);
    } else if(isDirectory(path)) {
        for(auto const &path : getContents(path, false, err)) {
            remove(path, err);
            if(err && err->failed)
                return;
        }
    }

    mDescriptors.erase(index);
}
std::vector<fs::Path> fs::MemoryFilesystem::getContents(Path const &path, bool recursive, Error *err) const {
    if(!exists(path))
        setErr(err, fmt::format("Cannot get directory contents: no such file or directory!"), path.string());
    if(!isDirectory(path))
        setErr(err, fmt::format("Cannot get contents of a non-directory!"), path.string());

    std::vector<Path> res;
    auto prefix = path.makeAbsolute("").string();
    for(auto const &desc : mDescriptors.dense()) {
        if(prefix.empty()) {
            res.emplace_back(desc.path);
            continue;
        }
            
        if(recursive) {
            if(desc.path.makeAbsolute("").string().compare(0, prefix.size(), prefix) == 0)
                res.emplace_back(desc.path);
        } else {
            if(desc.path.parentPath() == path)
                res.emplace_back(desc.path);
        }
    }

    return res;
}
bool fs::MemoryFilesystem::isDirectory(Path const &path, Error *err) const {
    uint index = getFileIndex(path);
    return path.empty() || index != 0 && mDescriptors.at(index).type == DescriptorType::Directory;

}
bool fs::MemoryFilesystem::isRegularFile(Path const &path, Error *err) const {
    uint index = getFileIndex(path);
    return index != 0 && mDescriptors.at(index).type == DescriptorType::File;
}
bool fs::MemoryFilesystem::isEmpty(Path const &path, Error *err) const {
    if(!exists(path)) {
        setErr(err, fmt::format("isEmpty: No such file or directory!"), path.string());
        return false;
    }

    uint index = getFileIndex(path);
    auto const &desc = mDescriptors.at(index);
    return isRegularFile(path) ? fileSize(path) == 0 : getContents(path, false).size() == 0;
}
std::chrono::file_clock::time_point fs::MemoryFilesystem::lastTimeWrite(Path const &path, Error *err) const {
    if(!exists(path)) {
        setErr(err, fmt::format("Cannot get last modification time: no such file or directory!"), path.string());
        return std::chrono::file_clock::now();
    }

    return mDescriptors.at(getFileIndex(path)).modificationTime;
};
fs::FileHandle fs::MemoryFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *err) {
    if(isDirectory(path)) {
        setErr(err, fmt::format("Cannot open file: is a directory!"), path.string());
        return {};
    }
    if(!exists(path.parentPath())) {
        setErr(err, fmt::format("Cannot open file: no such file or directory!"), path.string());
        return {};
    }

    uint openId = mNextId++;
    uint fileId = getDesc(path, DescriptorType::File).id;
    mFiles.emplace(openId, Handle{
        .path = path,
        .file = std::unique_ptr<MemoryFile>(new MemoryFile(mode, fileId)),
        .openId = openId,
        .fileId = fileId
    });
    auto &handle = mFiles.at(openId);
    handle.file->setParent(this);
    handle.file->open(handle.openId);

    if(err && !handle.file->isOpen()) {
        err->failed = true;
        err->message = fmt::format("Failed to open memory file {}!", path.string());
        err->path = path.string();
    }

    return FileHandle(handle.file.get());
}
