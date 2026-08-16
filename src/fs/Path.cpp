#include "IFilesystem.hpp"
#include "Logging.hpp"
#include <cassert>

using namespace fs;

constexpr std::string_view SEPARATOR = "/";

static void replaceAll(std::string &str, std::string_view from, std::string_view to, bool onePass = true) {
    size_t pos = str.find(from);
    while (pos != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos = str.find(from, onePass ? pos + to.size() : 0);
    }
}
void fs::Path::removeStuff() {
    replaceAll(mPath, "//", "/", false);
    // replaceAll(mPath, "/./", "/");
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
    return Path(mPath).makeAbsolute("").string() == Path(rhs.mPath).makeAbsolute("").string();
}
bool Path::operator!=(Path const &rhs) const {
    return !this->operator==(rhs);
}

std::string const &Path::string() const {
    return mPath;
}

std::string Path::filename() const {
    auto pos = mPath.find_last_of(SEPARATOR, std::max<int>(0, mPath.size() - 2));
    if(pos == std::string::npos)
        return "";
    pos += 1;
    return mPath.substr(pos, mPath.size() - pos - (mPath.back() == '/'));
}
std::string Path::extension() const {
    if(mPath.back() == '/')
        return "";
    auto filenameString = filename();
    auto pos = filenameString.find_last_of('.');
    if(pos == std::string::npos)
        return "";
    return filenameString.substr(pos);
}
std::string Path::stem() const {
    auto filenameString = filename();
    return filenameString.substr(0, filenameString.find_last_of('.'));
}
Path Path::parentPath() const {
    auto pos = mPath.find_last_of(SEPARATOR, std::max<int>(0, mPath.size() - 2));
    if(pos == std::string::npos)
        return "";
    return mPath.substr(0, pos);
}
bool fs::Path::isAbsolute() const {
    return !mPath.empty() && mPath.compare(0, SEPARATOR.size(), SEPARATOR) == 0;
}
bool Path::empty() const {
    return mPath.empty() || mPath == "." || mPath == "/";
}
std::string Path::removeFilename() {
    if(empty())
        return "";
    auto fname = filename();
    mPath.erase(mPath.size() - fname.size() - (mPath.back() == '/'));
    return fname;
}
std::string Path::removeExtension() {
    if(empty())
        return "";
    auto ext = extension();
    mPath.erase(mPath.size() - ext.size() - (mPath.back() == '/'));
    return ext;
}
Path Path::removeParentPath() {
    if(empty())
        return "";
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
            relativePath.insert(relativePath.size(), thisComponents[i] + "/");
        } else {
            if(thisComponents[i] != otherComponents[i]) {
                relativePath.insert(0, "../");
                relativePath.insert(relativePath.size(), thisComponents[i] + "/");
            }
        }
    }

    if(!relativePath.empty() && !mPath.empty() && mPath.back() != '/') {
        relativePath.erase(relativePath.size() - 1, 1);
    }

    return Path(relativePath);
}
fs::Path fs::Path::makeAbsolute(Path const &relative) const {
    auto path = relative / *this;
    
    if(isAbsolute())
        return path;

    // FIXME: O(n) component removal
    auto components = path.split();
    uint i = 0;

    // Skip ..'s in the beginning.
    for(; i < components.size() && components[i] == ".."; ++i);
    for(; i < components.size(); ++i) {
        if(components[i] == ".." && i > 0 && components[i - 1] != "..") {
            components.erase(components.begin() + i - 1, components.begin() + i + 1);
            i -= 2;
        }
    }

    return fs::Path(components, true);
}
std::vector<std::string> Path::split() const {
    std::vector<std::string> res;
    res.reserve(std::count(mPath.begin(), mPath.end(), SEPARATOR.front()) + 1);

    fs::Path remaining = *this;
    while(!remaining.empty()) {
        auto fname = remaining.removeFilename();
        if(fname.empty())
            break;
        res.emplace_back(fname);
    }

    if(!remaining.empty())
        res.emplace_back(remaining.string());

    std::reverse(res.begin(), res.end());

    return res;
}
Path &Path::append(Path const &rhs) {
    if(!empty())
        mPath.append(SEPARATOR);

    mPath.append(rhs.mPath);
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
    concat(rhs);
    return *this;
}

Path fs::operator/(Path const &lhs, Path const &rhs) {
    return Path{}.append(lhs).append(rhs);
}
Path fs::operator+(Path const &lhs, Path const &rhs) {
    return Path{}.concat(lhs).concat(rhs);
}
