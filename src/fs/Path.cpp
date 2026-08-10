#include "IFilesystem.hpp"
#include "Logging.hpp"
#include <cassert>

using namespace fs;

constexpr std::string_view SEPARATOR = "/";

static void replaceAll(std::string &str, std::string_view from, std::string_view to) {
    size_t pos = str.find(from);
    while (pos != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos = str.find(from, pos + to.size());
    }
}
void fs::Path::removeStuff() {
    replaceAll(mPath, "//", "/");
    
    auto components = split();
    mPath.clear();
    for(auto const &component : components) {
        if(component != ".") 
            mPath.insert(mPath.size(), component);
    }
}

Path::Path() = default;
Path::Path(Path const &) = default;
Path::Path(Path &&) = default;
Path::Path(std::string const &path) { *this = path; }
Path::Path(std::string &&path) { *this = path; }
Path::Path(std::string_view path) { *this = path; }
Path::Path(char const *path) { *this = path; }

Path &Path::operator=(std::string const &path) {
    mPath = path;
    removeStuff();
    return *this;
}
Path &Path::operator=(std::string &&path) {
    mPath = std::move(path);
    removeStuff();
    return *this;
}
Path &Path::operator=(std::string_view path) {
    mPath = path;
    removeStuff();
    return *this;
}
Path &Path::operator=(char const *path) {
    mPath = path;
    removeStuff();
    return *this;
}

bool Path::operator==(Path const &rhs) const {
    return Path(mPath).makeAbsolute(".").string() == Path(rhs.mPath).makeAbsolute(".").string();
}
bool Path::operator!=(Path const &rhs) const {
    return !this->operator==(rhs);
}

std::string const &Path::string() const {
    return mPath;
}

std::string Path::filename() const {
    return mPath.substr(mPath.find_last_of(SEPARATOR));
}
std::string Path::extension() const {
    auto filenameString = filename();
    return filenameString.substr(filenameString.find_last_of('.'));
}
std::string Path::stem() const {
    auto filenameString = filename();
    return filenameString.substr(0, filenameString.find_last_of('.'));
}
Path Path::parentPath() const {
    return mPath.substr(0, mPath.find_last_of(SEPARATOR));
}
bool fs::Path::isAbsolute() const {
    return !mPath.empty() && mPath.compare(0, SEPARATOR.size(), SEPARATOR) == 0;
}
bool Path::empty() const {
    return mPath.empty() || mPath == "/" || mPath == ".";
}

std::string Path::removeFilename() {
    auto fname = filename();
    mPath.erase(mPath.size() - fname.size());
    return fname;
}
std::string Path::removeExtension() {
    auto ext = extension();
    mPath.erase(mPath.size() - ext.size());
    return ext;
}
Path Path::removeParentPath() {
    auto path = parentPath();
    mPath.erase(0, path.string().size());
    return path;
}

fs::Path fs::Path::makeRelative(Path const &path) const {
    auto thisComponents = split();
    auto otherComponents = path.split();
    std::string relativePath;
    for(uint i = 0; i < thisComponents.size() || i < otherComponents.size(); ++i) {
        if(i >= thisComponents.size()) {
            relativePath.insert(0, "../");
        } else if(i >= otherComponents.size()) {
            relativePath.insert(relativePath.size(), thisComponents[i]);
        } else {
            if(thisComponents[i] != otherComponents[i]) {
                relativePath.insert(0, "../");
                relativePath.insert(relativePath.size(), thisComponents[i]);
            }
        }
    }

    return Path(relativePath);
}
fs::Path fs::Path::makeAbsolute(Path const &relative) const {
    std::string path = (relative / *this).string();
    
    if(isAbsolute())
        return path;

    auto components = split();
    uint i = 0;

    // Skip ..'s in the beginning.
    for(; i < components.size() && components[i] == ".."; ++i);

    for(; i < components.size(); ++i) {
        if(components[i] == ".." && i > 0 && components[i - 1] != "..") {
            components.erase(components.begin() + i - 1, components.begin() + i);
            i -= 2;
        }
    }

    return fs::Path(components);
}
std::vector<std::string> Path::split() const {
    std::vector<std::string> res;
    res.reserve(std::count(mPath.begin(), mPath.end(), SEPARATOR.front()) + 1);
    
    // size_t pos = 0, prevPos = 0;
    // while((pos = mPath.find(SEPARATOR, pos + 1)) != std::string::npos) {
    //     res.emplace_back(mPath.substr(std::clamp<int>(prevPos+1, 0, mPath.size()), std::clamp<int>(pos-1, 0, mPath.size())));
    //     prevPos = pos;
    // }
    // res.emplace_back(mPath.substr(prevPos, pos));

    fs::Path remaining = mPath;
    while(!remaining.empty()) {
        res.emplace_back(remaining.removeFilename());
    }

    LOG_TRACE("split of {}: {}", mPath, res);

    return res;
}
Path &Path::append(Path const &rhs) {
    mPath.append(SEPARATOR).append(rhs.mPath);
    removeStuff();
    return *this;
}
Path &Path::concat(Path const &rhs) {
    mPath.append(rhs.mPath);
    removeStuff();
    return *this;
}
void Path::clear() {
    mPath.clear();
}
Path &Path::operator/=(Path const &rhs) {
    append(rhs);
    return *this;
}
Path &Path::operator+=(Path const &rhs) {
    removeStuff();
    return *this;
}

Path fs::operator/(Path const &lhs, Path const &rhs) {
    return Path{}.append(lhs).append(rhs);
}
Path fs::operator+(Path const &lhs, Path const &rhs) {
    return Path{}.concat(lhs).concat(rhs);
}
