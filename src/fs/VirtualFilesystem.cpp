#include "VirtualFilesystem.hpp"
#include "Logging.hpp"

void fs::VirtualFilesystem::unmount(Path dir, Error *err) {
    auto path = dir.makeAbsolute("").string();
    if(!mMount.contains(path)) {
        auto msg = fmt::format("Cannot unmount filesystem at \"{}\": doesent exist!", path);
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }
        return;
    }
    mMount.erase(path);
}
void fs::VirtualFilesystem::mount(IFilesystem *filesystem, Path dir, Error *err) {
    auto path = dir.makeAbsolute("").string();

    std::string msg;
    if(mMount.contains(path)) {
        msg = fmt::format("Cannot mount filesystem at \"{}\": already mounted!", path);
    }
    if(dir.string().empty()) {
        msg = fmt::format("\"\" cannot be a mount point!");
    }
    if(!msg.empty()) {
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }
        return;
    }

    mMount[path] = filesystem;
}

fs::VirtualFilesystem::Mount fs::VirtualFilesystem::getMount(Path path, Error *err) const {
    path = path.makeAbsolute("");
    // Find the mount that matches the most components

    uint biggestMatch = 0;
    std::string dir;

    for(auto &[mountPath, mountFs] : mMount) {
        if (mountPath.size() <= path.string().size() && 
            mountPath.size() > biggestMatch && 
            (path.string().compare(0, mountPath.size(), mountPath) == 0)) 
        {
            if(!dir.empty()) {
                LOG_WARN("Mount conflict at \"{}\"!", path.string());
            }
            dir = mountPath;
            biggestMatch = mountPath.size();
        }
    }

    if(dir.empty()) {
        auto msg = fmt::format("No filesystem mounted for path \"{}\"", path.string());
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }
        return {};
    }

    Mount res{
        .fs = mMount.at(dir),
        .dir = dir,
        .relativePath = Path(path).makeRelative(dir),
        .absolutePath = Path(path).makeAbsolute(""),
    };
    if(res.relativePath.empty())
        res.relativePath = "/";

    return res;
}

void fs::VirtualFilesystem::copy(Path const &src, Path const &dst, Error *err) {
    auto srcMount = getMount(src, err);
    if(err && err->failed)
        return;
    auto dstMount = getMount(dst, err);
    if(err && err->failed)
        return;

    assert(srcMount.fs && dstMount.fs);

    if(srcMount.fs->isDirectory(src) && !dstMount.fs->isDirectory(dst)) {
        auto msg = fmt::format("Cannot copy directory {} to {} (file)", src.string(), dst.string());
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }
        return;
    }

    if(srcMount.fs != dstMount.fs) {
        std::vector<Path> paths;
        if(srcMount.fs->isDirectory(srcMount.relativePath))
            paths = srcMount.fs->getContents(srcMount.relativePath, true);
        else 
            paths = {src};
        for(auto const &path : paths) {
            auto file = srcMount.fs->open(srcMount.relativePath, 0, err);
            if(err && err->failed)
                return;
            std::vector<std::byte> buff(file->size());
            file->read(buff.data(), buff.size());
            file->flush();
            file.close();

            file = dstMount.fs->open(dstMount.relativePath, FileOpenMode::Truncate, err);
            if(err && err->failed)
                return;
            file->write(buff.data(), buff.size());
            file->flush();
            file.close();
        }
    } else {
        srcMount.fs->copy(srcMount.relativePath, dstMount.relativePath, err);
    }
}
void fs::VirtualFilesystem::move(Path const &src, Path const &dst, Error *err) {
    // error: it seems like you've fallen asleep and hit your keyboard
    // zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz ...
    // ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ you fell asleep here
    // = help: try improving your sleep schedule or reducing your hours
    auto srcMount = getMount(src, err);
    if(err && err->failed)
        return;
    auto dstMount = getMount(dst, err);
    if(err && err->failed)
        return;

    assert(srcMount.fs && dstMount.fs);

    if(srcMount.fs->isDirectory(src) && !dstMount.fs->isDirectory(dst)) {
        auto msg = fmt::format("Cannot copy directory {} to {} (file)", src.string(), dst.string());
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }
        return;
    }

    if(srcMount.fs != dstMount.fs) {
        for(auto const &path : srcMount.fs->isDirectory(srcMount.relativePath) ? srcMount.fs->getContents(srcMount.relativePath, true) : std::vector<Path>{srcMount.relativePath}) {
            auto file = srcMount.fs->open(srcMount.relativePath, 0, err);
            if(err && err->failed)
                return;
            std::vector<std::byte> buff(file->size());
            file->read(buff.data(), buff.size());
            file.close();

            srcMount.fs->remove(srcMount.relativePath, err);
            if(err && err->failed)
                return;

            file = dstMount.fs->open(dstMount.relativePath, FileOpenMode::Truncate, err);
            if(err && err->failed)
                return;
            file->write(buff.data(), buff.size());
            file.close();
        }
    } else {
        srcMount.fs->move(srcMount.relativePath, dstMount.relativePath, err);
    }
}
void fs::VirtualFilesystem::createDirectory(Path const &path, Error *err) {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return;
    mount.fs->createDirectory(mount.relativePath, err);
}
void fs::VirtualFilesystem::createDirectories(Path const &path, Error *err) {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return;
    mount.fs->createDirectories(mount.relativePath, err);
}
bool fs::VirtualFilesystem::exists(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return false;
    return mount.fs->exists(mount.relativePath, err);
}
uintmax_t fs::VirtualFilesystem::fileSize(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return 0;
    return mount.fs->fileSize(mount.relativePath, err);
}
void fs::VirtualFilesystem::remove(Path const &path, Error *err) {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return;

    if(mount.relativePath.empty()) {
        auto msg = fmt::format("Cannot delete \"{}\", because it is a mount point!", path.string());
        if(err) {
            err->failed = true;
            err->message = msg;
        } else {
            LOG_ERROR(msg);
        }

        return;
    }
    mount.fs->remove(mount.relativePath, err);
}
std::vector<fs::Path> fs::VirtualFilesystem::getContents(Path const &path, bool recursive, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return {};
    auto contents = mount.fs->getContents(mount.relativePath, recursive, err);
    for(auto &entry : contents) {
        entry = entry.makeAbsolute(mount.dir);
    }
    return contents;
}
bool fs::VirtualFilesystem::isDirectory(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return false;
    return mount.fs->isDirectory(mount.relativePath, err);
}
bool fs::VirtualFilesystem::isRegularFile(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return false;
    return mount.fs->isRegularFile(mount.relativePath, err);
}
bool fs::VirtualFilesystem::isEmpty(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return false;
    return mount.fs->isEmpty(mount.relativePath, err);
}
fs::FileHandle fs::VirtualFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *err) {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return {};
    return mount.fs->open(mount.relativePath, mode, err);
}
std::chrono::file_clock::time_point fs::VirtualFilesystem::lastTimeWrite(Path const &path, Error *err) const {
    auto mount = getMount(path, err);
    if(err && err->failed || !mount.fs)
        return std::chrono::file_clock::now();
    return mount.fs->lastTimeWrite(mount.relativePath, err);
}



fs::SubFilesystem::SubFilesystem(IFilesystem *parent, Path prefix) : mParent(parent), mPrefix(prefix) {}

void fs::SubFilesystem::copy(Path const &src, Path const &dst, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->copy(mPrefix/src, mPrefix/dst, err);
}
void fs::SubFilesystem::move(Path const &src, Path const &dst, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->move(mPrefix/src, mPrefix/dst, err);
}
void fs::SubFilesystem::createDirectory(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->createDirectory(mPrefix/path, err);
}
void fs::SubFilesystem::createDirectories(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->createDirectories(mPrefix/path, err);
}
bool fs::SubFilesystem::exists(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->exists(mPrefix/path, err);
}
uintmax_t fs::SubFilesystem::fileSize(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->fileSize(mPrefix/path, err);
}
void fs::SubFilesystem::remove(Path const &path, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    mParent->remove(mPrefix/path, err);
}
std::vector<fs::Path> fs::SubFilesystem::getContents(Path const &path, bool recursive, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->getContents(mPrefix/path, err);
}
bool fs::SubFilesystem::isDirectory(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isDirectory(mPrefix/path, err);
}
bool fs::SubFilesystem::isRegularFile(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isRegularFile(mPrefix/path, err);
}
bool fs::SubFilesystem::isEmpty(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->isEmpty(mPrefix/path, err);
}
std::chrono::file_clock::time_point fs::SubFilesystem::lastTimeWrite(Path const &path, Error *err) const {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->lastTimeWrite(mPrefix/path, err);
}
fs::FileHandle fs::SubFilesystem::open(Path const &path, FileOpenMode::Flags mode, Error *err) {
    assert(mParent && "Invalid SubFilesystem!");
    return mParent->open(mPrefix/path, mode, err);
}