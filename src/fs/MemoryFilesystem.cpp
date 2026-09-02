#include "MemoryFilesystem.hpp"
#include "Logging.hpp"
#include "nlohmann/json.hpp"

using nlohmann::json;

fs::MemoryFile::MemoryFile(FileOpenMode::Flags mode, uint fileId) {
    mFileId = fileId;
    mMode = mode;
}
void fs::MemoryFile::open(uint openId) {
    mOpenId = openId;
    assert(mParent);
    // LOG_TRACE("{}: Open {} {}", mOpenId, mFileId, mParent->mFiles.at(mOpenId).path.string());
    assert(mParent->mFileContents.contains(mFileId));
    mBuffer.reserve(mParent->mFileContents.at(mFileId).size() * 1.5);
    
    if(!(mMode & FileOpenMode::Truncate)) {
        sync();
    }
    
    mOpen = true;
}
fs::MemoryFile::~MemoryFile() {
}
void fs::MemoryFile::setFileHandle(FileHandle *handle) {
    mHandle = handle;
}
void fs::MemoryFile::setParentFilesystem(MemoryFilesystem *parent) {
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
    // LOG_TRACE("{}: Close", mOpenId);
    mOpen = false;
    if(mHandle)
        mHandle->close();
    if(mParent)
        mParent->close(mOpenId);
}
uintmax_t fs::MemoryFile::size() {
    return mBuffer.size();
}
void fs::MemoryFile::read(void *dst, uintmax_t size) {
    // LOG_TRACE("{}: Read {} bytes offset {} buff size {}", mOpenId, size, mReadHead, this->size());
    std::memcpy(dst, mBuffer.data() + mReadHead, std::clamp<uintmax_t>(mReadHead + size, 0, this->size()) - mReadHead);
    mReadHead += size;
}
void fs::MemoryFile::sync() {
    mBuffer = mParent->mFileContents.at(mFileId);
    // LOG_TRACE("{}: Sync buff size {}", mOpenId, size());
}
void fs::MemoryFile::write(void const *src, uintmax_t size) {
    if(mMode & FileOpenMode::Append) {
        seekp(0, SeekDir::End);
    }

    if(mWriteHead + size >= this->size()) {
        if(mWriteHead + size >= mBuffer.capacity()) 
            mBuffer.reserve((mWriteHead + size) * 1.5);
        mBuffer.resize(mWriteHead + size);
    }

    // LOG_TRACE("{}: Write {} bytes offset {} buff size {} (append {})", mOpenId, size, mReadHead, this->size(), mMode & FileOpenMode::Append);
    std::memcpy(mBuffer.data() + mWriteHead, src, std::clamp<uintmax_t>(mWriteHead + size, 0, this->size()) - mWriteHead);

    mWriteHead += size;
}
void fs::MemoryFile::flush() {
    // LOG_TRACE("{}: Flush", mOpenId);
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
    path = path.makeAbsolute("").removeDirSeparator();
    if(mPathToDescIndex.contains(path.string())) {
        return mPathToDescIndex.at(path.string());
    } else {
        return 0;
    }
}
fs::MemoryFilesystem::Descriptor &fs::MemoryFilesystem::getDesc(fs::Path path, DescriptorType defaultType) {
    path = path.makeAbsolute("").removeDirSeparator();
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

    if(mFiles.contains(id))
        mFiles.erase(id);
}
void fs::MemoryFilesystem::closeAll(uint id) {
    std::vector<uint> toClose;
    toClose.reserve(4);
    for(auto const &handle : mFiles.dense()) {
        if(handle.fileId == id)
            toClose.emplace_back(handle.openId);
    }
    for(auto const &id : toClose) {
        close(id);
    }
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
        handle.file->setParentFilesystem(this);
    }

    return *this;
}
fs::MemoryFilesystem::MemoryFilesystem(MemoryFilesystem &&rhs) {
    *this = std::move(rhs);
}
void fs::MemoryFilesystem::closeIfInUse(fs::Path const &path) {
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

    closeIfInUse(dst);

    auto srcDesc = mDescriptors.at(getFileIndex(src));
    
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
            auto dstPath = (dst/path.makeRelative(src)).removeDirSeparator();
            if(isDirectory(path)) {
                if(!exists(dstPath))
                    createDirectories(dstPath);
            } else {
                createDirectories(dstPath.parentPath(), err);
                if(err && err->failed)
                    continue;
                auto const &desc = getDesc(dstPath, DescriptorType::File);
                mFileContents.at(desc.id) = mFileContents.at(getFileIndex(path));
            }
        }
    }
}
void fs::MemoryFilesystem::move(Path const &src, Path const &dst, Error *err) {
    if(!checkBeforeTransfer(src, dst, err, "move"))
        return;
    closeIfInUse(src);
    closeIfInUse(dst);

    auto &srcDesc = mDescriptors.at(getFileIndex(src));

    uint dstId = getFileIndex(dst);
    bool dstIsDirectory = dstId != 0 && mDescriptors.at(dstId).type == DescriptorType::Directory;

    if(srcDesc.type == DescriptorType::File) {
        srcDesc.path = (dstIsDirectory ? dst/src.filename() : dst).removeDirSeparator();
        if(exists(srcDesc.path))
            remove(srcDesc.path);
        mPathToDescIndex.erase(src.removeDirSeparator().string());
        mPathToDescIndex[srcDesc.path.string()] = srcDesc.id;
    } else if(srcDesc.type == DescriptorType::Directory) {
        auto contents = getContents(src, true, err);
        contents.emplace_back(src);
        if(err && err->failed)
            return;
        for(auto path : contents) {
            auto dstPath = (dst/path.makeRelative(src)).removeDirSeparator();
            if(exists(dstPath))
                remove(dstPath);
            uint index = getFileIndex(path);
            mDescriptors.at(index).path = dstPath;
            mPathToDescIndex.erase(path.string());
            mPathToDescIndex[dstPath.string()] = index;
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
    if(isRegularFile(path.parentPath())) {
        setErr(err, fmt::format("Cannot create a directory: parent path is a file!"), path.string());
        return;
    }

    getDesc(path, DescriptorType::Directory);
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
        createDirectory(path, err);
        if(err && err->failed)
            return;
    }
}
bool fs::MemoryFilesystem::exists(Path const &path, Error *) const {
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

    closeIfInUse(path);

    auto index = getFileIndex(path);

    if(isRegularFile(path)) {
        mFileContents.erase(index);
    } else if(isDirectory(path)) {
        for(auto const &dirPath : getContents(path, false, err)) {
            remove(dirPath, err);
            if(err && err->failed)
                return;
        }
    }

    mDescriptors.erase(index);
    mPathToDescIndex.erase(path.makeAbsolute("").removeDirSeparator().string());
}
std::vector<fs::Path> fs::MemoryFilesystem::getContents(Path const &path, bool recursive, Error *err) const {
    if(!exists(path))
        setErr(err, fmt::format("Cannot get directory contents: no such file or directory!"), path.string());
    if(!isDirectory(path))
        setErr(err, fmt::format("Cannot get contents of a non-directory!"), path.string());

    std::vector<Path> res;
    auto prefix = path.makeAbsolute("");
    for(auto const &desc : mDescriptors.dense()) {
        if(recursive) {
            if(prefix.empty() || (desc.path.string().compare(0, prefix.string().size(), prefix.string()) == 0 && desc.path != path))
                res.emplace_back(desc.path);
        } else {
            if(desc.path.parentPath() == prefix)
                res.emplace_back(desc.path);
        }
    }

    return res;
}
bool fs::MemoryFilesystem::isDirectory(Path const &path, Error *) const {
    uint index = getFileIndex(path);
    return path.empty() || (index != 0 && mDescriptors.at(index).type == DescriptorType::Directory);

}
bool fs::MemoryFilesystem::isRegularFile(Path const &path, Error *) const {
    uint index = getFileIndex(path);
    return index != 0 && mDescriptors.at(index).type == DescriptorType::File;
}
bool fs::MemoryFilesystem::isEmpty(Path const &path, Error *err) const {
    if(!exists(path)) {
        setErr(err, fmt::format("isEmpty: No such file or directory!"), path.string());
        return false;
    }

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
    handle.file->setParentFilesystem(this);
    handle.file->open(handle.openId);

    if(err && !handle.file->isOpen()) {
        err->failed = true;
        err->message = fmt::format("Failed to open memory file {}!", path.string());
        err->path = path.string();
    }

    return FileHandle(handle.file.get());
}

/*
uint32 <-- json descriptors size
[ <-- json file descriptors
{
    "type": <-- descriptor type ("directory" / "file")
    "path": <-- virtual path
    "mtime": <-- modification time (nanoseconds since epoch)
    "offset": <-- offset of the file data relative to the start of the binary blob ("file" only)
    "size": <-- size of the file data ("file" only)
},
]
01011101 01010010... <-- binary file data (the rest of the file)
*/

std::vector<std::byte> fs::MemoryFilesystem::serialize() const {
    struct ByteRange {
        void const *data;
        uintmax_t size;
    };
    std::vector<ByteRange> ranges;
    ranges.reserve(mDescriptors.size());
    uintmax_t binOffset = 0;

    json descriptors;
    for(auto const &desc : mDescriptors.dense()) {
        json &jsonDesc = descriptors.emplace_back();
        jsonDesc["type"] = desc.type == DescriptorType::Directory ? "directory" : "file";
        jsonDesc["path"] = desc.path.string();
        jsonDesc["mtime"] = std::chrono::duration_cast<std::chrono::nanoseconds>(desc.modificationTime.time_since_epoch()).count();
        if(desc.type == DescriptorType::File) {
            auto const &contents = mFileContents.at(desc.id);
            jsonDesc["offset"] = binOffset;
            jsonDesc["size"] = contents.size();
            binOffset += contents.size();
            ranges.emplace_back(contents.data(), contents.size());
        }
    }

    std::string descRes = descriptors.dump(-1);
    std::vector<std::byte> result(sizeof(uint32_t) + descRes.size() + binOffset);
    
    uint32_t jsonSize = descRes.size();
    std::memcpy(result.data(), &jsonSize, sizeof(uint32_t));
    std::memcpy(result.data() + sizeof(uint32_t), descRes.data(), descRes.size());

    uintmax_t offset = sizeof(uint32_t) + descRes.size();
    for(auto const &[data, size] : ranges) {
        std::memcpy(result.data() + offset, data, size);
        offset += size;
    }

    assert(offset == result.size());

    return result;
}
void fs::MemoryFilesystem::deserialize(void const *data, uintmax_t fileSize) {
    if(fileSize <= sizeof(uint32_t)) {
        LOG_ERROR("fs::MemoryFilesystem::deserialize: size {} is too small!", fileSize);
        return;
    }

    uint32_t jsonSize = *reinterpret_cast<uint32_t const *>(data);

    // uint32_t jsonSize = 0;
    // std::memcpy(&jsonSize, data, sizeof(uint32_t));

    if(jsonSize == 0) {
        LOG_ERROR("fs::MemoryFilesystem::deserialize: header size {} is 0!", jsonSize);
        return;
    }
    if(jsonSize + sizeof(uint32_t) > fileSize) {
        LOG_ERROR("fs::MemoryFilesystem::deserialize: header size {} is too big for size {}!", jsonSize, fileSize);
        return;
    }
    

    auto jsonRange = std::ranges::subrange(static_cast<std::byte const *>(data) + sizeof(uint32_t), static_cast<std::byte const *>(data) + sizeof(uint32_t) + jsonSize);
    // printf("%.*s", static_cast<int>(&jsonRange.back() - &jsonRange.front() + 1), reinterpret_cast<char const *>(&jsonRange.front()));
    json descriptors = json::parse(jsonRange);
    assert(descriptors.is_array());

    closeIfInUse("/");
    mNextId = 1;
    mDescriptors.clear();
    mFileContents.clear();
    mPathToDescIndex.clear();

    for(json const &jsonDesc : descriptors) {
        assert(jsonDesc.is_object());
        assert(jsonDesc.contains("path"));
        assert(jsonDesc.contains("type"));
        assert(jsonDesc.contains("mtime"));
        std::string path = jsonDesc["path"].get<std::string>();
        DescriptorType type = jsonDesc["type"].get<std::string>() == "file" ? DescriptorType::File : DescriptorType::Directory;
        intmax_t modificationTime = jsonDesc["mtime"].get<intmax_t>();
        uint index = mPathToDescIndex[path] = mNextId++;
        mDescriptors[index] = {
            .type = type,
            .path = path,
            .id = index,
            .modificationTime = std::chrono::file_clock::time_point(std::chrono::nanoseconds(modificationTime))
        };
        mPathToDescIndex[path] = index;
        if(type == DescriptorType::File) {
            assert(jsonDesc.contains("offset"));
            assert(jsonDesc.contains("size"));
            uintmax_t offset = jsonDesc["offset"].get<uintmax_t>();
            uintmax_t size = jsonDesc["size"].get<uintmax_t>();

            assert(offset + size < fileSize && "Invalid descriptor!");

            mFileContents.emplace(index, jsonRange.end() + offset, jsonRange.end() + offset + size);
        }
    }
}
