#include "VirtualFilesystem.hpp"
#include "Logging.hpp"

#define CHECK_ERR(e) if(static_cast<bool>(e) && (e)->failed) { return; }

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
    if(dir.string().empty() || path.compare(0, 2, "..") == 0) {
        msg = fmt::format("\"{}\" cannot be a mount point!", dir.string());
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
    auto pathComponents = path.split();
    // Find the mount that matches the most components

    uint biggestMatch = 0;
    std::string dir;

    for(auto &[mountPath, mountFs] : mMount) {
        if(fs::Path(mountPath).empty() && biggestMatch < 1) {
            biggestMatch = 1;
            dir = mountPath;
        } else {
            auto mountComponents = fs::Path(mountPath).split();
            if(mountComponents.size() > pathComponents.size() || mountComponents.size() <= biggestMatch) {
                continue;
            }
            
            bool matches = true;
            for(uint i = 0; i < mountComponents.size(); ++i) {
                if(mountComponents[i] != pathComponents[i])
                    matches = false;
            }
    
            if(matches) {
                dir = mountPath;
                biggestMatch = mountComponents.size();
            }
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

void fs::VirtualFilesystem::transfer(Mount src, Mount dst, Error *err, bool move) {
    assert(src.fs && dst.fs);

    if(!src.fs->exists(src.relativePath)) {
        auto msg = fmt::format("Cannot {} directory {} to {}: no such file or directory", move ? "move" : "copy", src.absolutePath.string(), dst.absolutePath.string());
        if(err) {
            err->failed = true;
            err->message = msg;
            err->path = fmt::format("src: {}, dst: {}", src.absolutePath.string(), dst.absolutePath.string());
        } else {
            LOG_ERROR(msg);
        }
        return;
    }
    if(!dst.relativePath.parentPath().empty() && !dst.fs->exists(dst.relativePath.parentPath())) {
        auto msg = fmt::format("Cannot {} directory {} to {}: no such file or directory", move ? "move" : "copy", src.absolutePath.string(), dst.absolutePath.string());
        if(err) {
            err->failed = true;
            err->message = msg;
            err->path = fmt::format("src: {}, dst: {}", src.absolutePath.string(), dst.absolutePath.string());
        } else {
            LOG_ERROR(msg);
        }
        return;
    }
    if(src.fs->isDirectory(src.relativePath) && dst.fs->exists(dst.relativePath) && !dst.fs->isDirectory(dst.relativePath)) {
        auto msg = fmt::format("Cannot {} directory {} to {} (an existing file)", move ? "move" : "copy", src.absolutePath.string(), dst.absolutePath.string());
        if(err) {
            err->failed = true;
            err->message = msg;
            err->path = fmt::format("src: {}, dst: {}", src.absolutePath.string(), dst.absolutePath.string());
        } else {
            LOG_ERROR(msg);
        }
        return;
    }

    if(src.fs != dst.fs) {
        std::vector<Path> paths;
        if(src.fs->isDirectory(src.relativePath, err)) {
            paths = src.fs->getContents(src.relativePath, true, err);
        } else {
            paths = {src.relativePath};
        }
        if(err && err->failed)
            return;
        for(auto const &path : paths) {
            auto srcPath = src.relativePath/path.makeRelative(src.relativePath);
            if(src.fs->isDirectory(srcPath))
                continue;

            auto file = src.fs->open(srcPath, 0, err);
            CHECK_ERR(err);

            std::vector<std::byte> buff(file->size());
            file->seekg(0);
            file->read(buff.data(), buff.size());
            file.close();

            if(move) {
                src.fs->remove(srcPath, err);
                CHECK_ERR(err);
            }
            
            auto dstPath = dst.relativePath/path.makeRelative(src.relativePath);
            if(!dstPath.parentPath().empty()) {
                dst.fs->createDirectories(dstPath.parentPath());
            }
            file = dst.fs->open(dstPath, FileOpenMode::Truncate, err);
            CHECK_ERR(err);
            
            file->write(buff.data(), buff.size());
            file->flush();
            file.close();
        }
    } else {
        if(move) {
            src.fs->move(src.relativePath, dst.relativePath, err);
        } else {
            src.fs->copy(src.relativePath, dst.relativePath, err);
        }
    }
}
void fs::VirtualFilesystem::copy(Path const &from, Path const &to, Error *err) {
    auto src = getMount(from, err);
    if(err && err->failed)
        return;
    auto dst = getMount(to, err);
    if(err && err->failed)
        return;

    transfer(src, dst, err, false);
}
void fs::VirtualFilesystem::move(Path const &from, Path const &to, Error *err) {
    // error: it seems like you've fallen asleep and hit your keyboard
    // zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz ...
    // ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ you fell asleep here
    // = help: try improving your sleep schedule or reducing your hours
    auto src = getMount(from, err);
    if(err && err->failed)
        return;
    auto dst = getMount(to, err);
    if(err && err->failed)
        return;

    transfer(src, dst, err, true);
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
