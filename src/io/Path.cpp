#include "engine/io/fs/IFilesystem.hpp"
#include <cassert>

constexpr std::string_view SEPARATOR = "/";

static void replaceAll(std::string &str, std::string_view from, std::string_view to, bool onePass = true) {
    size_t pos = str.find(from);
    while (pos != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos = str.find(from, onePass ? pos + to.size() : 0);
    }
}
void egn::fs::Path::removeStuff() {
    replaceAll(mPath, "//", "/", false);
    // replaceAll(mPath, "/./", "/");
}

egn::fs::Path::Path() = default;
egn::fs::Path::Path(Path const &) = default;
egn::fs::Path::Path(Path &&) = default;
egn::fs::Path::Path(std::string const &path) { *this = path; }
egn::fs::Path::Path(std::string &&path) { *this = path; }
egn::fs::Path::Path(std::string_view path) { *this = path; }
egn::fs::Path::Path(char const *path) { *this = path; }

egn::fs::Path &egn::fs::Path::operator=(std::string const &path) {
    mPath = path;
    removeStuff();
    return *this;
}
egn::fs::Path &egn::fs::Path::operator=(std::string &&path) {
    mPath = std::move(path);
    removeStuff();
    return *this;
}
egn::fs::Path &egn::fs::Path::operator=(std::string_view path) {
    mPath = path;
    removeStuff();
    return *this;
}
egn::fs::Path &egn::fs::Path::operator=(char const *path) {
    mPath = path;
    removeStuff();
    return *this;
}

bool egn::fs::Path::operator==(Path const &rhs) const {
    return Path(mPath).makeAbsolute("").removeDirSeparator().string() == Path(rhs.mPath).makeAbsolute("").removeDirSeparator().string();
}
bool egn::fs::Path::operator!=(Path const &rhs) const {
    return !this->operator==(rhs);
}

std::string const &egn::fs::Path::string() const {
    return mPath;
}

std::string egn::fs::Path::filename() const {
    auto pos = mPath.find_last_of(SEPARATOR, std::max<int>(0, mPath.size() - 2));
    if(pos == std::string::npos)
        return "";
    pos += 1;
    return mPath.substr(pos, mPath.size() - pos - (mPath.back() == '/'));
}
std::string egn::fs::Path::extension() const {
    if(mPath.back() == '/')
        return "";
    auto filenameString = filename();
    auto pos = filenameString.find_last_of('.');
    if(pos == std::string::npos)
        return "";
    return filenameString.substr(pos);
}
std::string egn::fs::Path::stem() const {
    auto filenameString = filename();
    return filenameString.substr(0, filenameString.find_last_of('.'));
}
egn::fs::Path egn::fs::Path::parentPath() const {
    auto pos = mPath.find_last_of(SEPARATOR, std::max<int>(0, mPath.size() - 2));
    if(pos == std::string::npos)
        return "";
    return mPath.substr(0, pos);
}
bool egn::fs::Path::isAbsolute() const {
    return !mPath.empty() && mPath.compare(0, SEPARATOR.size(), SEPARATOR) == 0;
}
bool egn::fs::Path::empty() const {
    return mPath.empty() || mPath == "." || mPath == "/";
}
std::string egn::fs::Path::removeFilename() {
    if(empty())
        return "";
    auto fname = filename();
    mPath.erase(mPath.size() - fname.size() - (mPath.back() == '/'));
    return fname;
}
std::string egn::fs::Path::removeExtension() {
    if(empty())
        return "";
    auto ext = extension();
    mPath.erase(mPath.size() - ext.size() - (mPath.back() == '/'));
    return ext;
}
egn::fs::Path egn::fs::Path::removeParentPath() {
    if(empty())
        return "";
    auto path = parentPath();
    mPath.erase(0, path.string().size());
    return path;
}
egn::fs::Path egn::fs::Path::removeDirSeparator() const {
    if(!empty() && mPath.back() == '/')
        return mPath.substr(0, mPath.size() - 1);
    else 
        return mPath;
}

egn::fs::Path egn::fs::Path::makeRelative(Path const &path) const {
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
egn::fs::Path egn::fs::Path::makeAbsolute(Path const &relative) const {
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
        } else if(components[i] == ".") {
            components.erase(components.begin() + i, components.begin() + i + 1);
            i -= 1;
        }
    }

    return egn::fs::Path(components, true);
}
std::vector<std::string> egn::fs::Path::split() const {
    std::vector<std::string> res;
    res.reserve(std::count(mPath.begin(), mPath.end(), SEPARATOR.front()) + 1);

    egn::fs::Path remaining = *this;
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
egn::fs::Path &egn::fs::Path::append(Path const &rhs) {
    if(!empty())
        mPath.append(SEPARATOR);

    mPath.append(rhs.mPath);
    removeStuff();
    return *this;
}
egn::fs::Path &egn::fs::Path::concat(Path const &rhs) {
    mPath.append(rhs.mPath);
    removeStuff();
    return *this;
}
void egn::fs::Path::clear() {
    mPath.clear();
}
egn::fs::Path &egn::fs::Path::operator/=(Path const &rhs) {
    append(rhs);
    return *this;
}
egn::fs::Path &egn::fs::Path::operator+=(Path const &rhs) {
    concat(rhs);
    return *this;
}

egn::fs::Path egn::fs::operator/(Path const &lhs, Path const &rhs) {
    return Path{}.append(lhs).append(rhs);
}
egn::fs::Path egn::fs::operator+(Path const &lhs, Path const &rhs) {
    return Path{}.concat(lhs).concat(rhs);
}
