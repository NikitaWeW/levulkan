#include "IFilesystem.hpp"
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

Path::Path(Path const &) = default;
Path::Path(Path &&) = default;
Path::Path(std::string const &path) { *this = path; }
Path::Path(std::string &&path) { *this = path; }
Path::Path(std::string_view path) { *this = path; }
Path::Path(char const *path) { *this = path; }

Path &Path::operator=(std::string const &path) {
    mPath = path;
    return *this;
}
Path &Path::operator=(std::string &&path) {
    mPath = std::move(path);
    return *this;
}
Path &Path::operator=(std::string_view path) {
    mPath = path;
    return *this;
}
Path &Path::operator=(char const *path) {
    mPath = path;
    return *this;
}

bool Path::operator==(Path const &rhs) const {
    return mPath == rhs.mPath;
}
bool Path::operator!=(Path const &rhs) const {
    return !this->operator==(rhs);
}

Path Path::filename() const {
    assert(valid());
    return mPath.substr(mPath.find_last_of(SEPARATOR));
}
Path Path::extension() const {
    assert(valid());
    auto filenameString = filename().string();
    return filenameString.substr(filenameString.find_last_of('.'));
}
Path Path::stem() const {
    assert(valid());
    auto filenameString = filename().string();
    return filenameString.substr(0, filenameString.find_last_of('.'));
}
Path Path::parentPath() const {
    assert(valid());
    return mPath.substr(0, mPath.find_last_of(SEPARATOR));
}
bool fs::Path::isAbsolute() const {
    return !mPath.empty() && mPath.compare(0, SEPARATOR.size(), SEPARATOR) == 0;
}
bool Path::empty() const {
    return mPath.empty() || mPath == "/" || mPath == ".";
}
bool Path::valid(IFilesystem *filesystem) const {
    bool valid = true;
    valid = valid && !mPath.empty() && isAbsolute();

    if(filesystem) {
        valid = valid && filesystem->exists(*this);
        if(valid && filesystem->isDirectory(*this))
            valid = valid && mPath.compare(mPath.size() - SEPARATOR.size() - 1, SEPARATOR.size(), SEPARATOR) == 0;
    }

    valid = valid && (mPath.find_first_of("//") == std::string::npos);

    return valid;
}
fs::Path fs::Path::relativeTo(Path) const {
    // TODO
}
void fs::Path::makeAbsolute() {
    // FIXME: Complete bullshit
    if(isAbsolute())
        return;

    replaceAll(mPath, "//", "/");
    auto components = split();
    mPath.clear();
    mPath.append("/");

    uint del = 0;
    for(uint i = 0; i < components.size(); ) {
        if(components[i] == "..") {
            i = std::max<uint>(i + 1, 0);
            ++del;
            continue;
        } else if(del != 0) {
            components.erase(components.begin() + i, components.begin() + i + del);
        } else {
            ++i;
        }
    }
}
std::vector<Path> Path::split() const {
    assert(valid());

    std::vector<Path> res;
    res.reserve(std::count(mPath.begin(), mPath.end(), SEPARATOR.front()));
    size_t pos = 0;
    std::string token;
    auto s = mPath;

    while((pos = s.find(SEPARATOR)) != std::string::npos) {
        token = s.substr(0, pos);
        if(!token.empty())
            res.push_back(token);
        s.erase(0, pos + SEPARATOR.length());
    }
    res.push_back(s);

    return res;
}
Path &Path::append(Path const &rhs) {
    mPath.append(SEPARATOR).append(rhs.mPath);
    replaceAll(mPath, "//", "/");
    return *this;
}
Path &Path::concat(Path const &rhs) {
    mPath.append(rhs.mPath);
    replaceAll(mPath, "//", "/");
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
    replaceAll(mPath, "//", "/");
    return *this;
}

Path fs::operator/(Path const &lhs, Path const &rhs) {
    return Path{}.append(lhs).append(rhs);
}
Path fs::operator+(Path const &lhs, Path const &rhs) {
    return Path{}.concat(lhs).concat(rhs);
}
