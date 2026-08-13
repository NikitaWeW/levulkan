#include "catch2/catch_test_macros.hpp"
#include "fs/IFilesystem.hpp"
#include "fs/NativeFilesystem.hpp"
#include "fs/ArchiveFilesystem.hpp"

static void testFilesystem(fs::IFilesystem *filesystem) {
    for(auto const &path : filesystem->getContents("/")) {
        REQUIRE(filesystem->exists(path));
        filesystem->removeAll(path);
        REQUIRE_FALSE(filesystem->exists(path));
    }

    auto files = {"/a.txt", "b.a", "/c.a.x", "./file0", "file1", "/dir1/dir2/dir3/directory.d/file"};
    constexpr uint NUM = 10;
    for(uint i = 0; i < NUM; ++i) {
        for(auto const &path : files) {
            filesystem->createDirectories(fs::Path(path).parentPath());
            auto file = filesystem->open(path, fs::FileOpenMode::Append);
            REQUIRE(filesystem->exists(path));
            REQUIRE(filesystem->isRegularFile(path));
            REQUIRE(file.isOpen());
            REQUIRE(file->tellp() == file->size());

            file->seekg(0, SeekDir::End);
            REQUIRE(file->tellg() == file->size());

            auto sizeBefore = file->size();
            char character = 'a' + i;
            file->write(&character, 1);
            REQUIRE(file->size() == sizeBefore + 1);

            file.close();
            REQUIRE(!file.isOpen());
        }
    }

    REQUIRE(filesystem->getContents("/", true).size() == 10);

    for(auto const &path : files) {
        REQUIRE(filesystem->exists(path));
        auto file = filesystem->open(path);
        REQUIRE(file->size() == NUM);
        std::vector<char> buff(file->size());
        file->seekp(0);
        file->read(buff.data(), buff.size());
        for(uint i = 0; i < NUM; ++i) {
            REQUIRE(buff[i] == static_cast<char>('a' + i));
        }

        file.close();
    }

    filesystem->copy("/a.txt", "/dir1/b.txt");
    REQUIRE(filesystem->exists("/dir1/b.txt"));
    REQUIRE(filesystem->isRegularFile("/dir1/b.txt"));

    filesystem->copy("/dir1", "/dirCopy");
    REQUIRE(filesystem->exists("/dirCopy/"));
    REQUIRE(filesystem->isDirectory("/dirCopy/"));
    REQUIRE(filesystem->exists("/dirCopy/b.txt"));
    REQUIRE(filesystem->isRegularFile("/dirCopy/b.txt"));

    filesystem->move("/dirCopy", "/dirMoved");
    REQUIRE(filesystem->exists("/dir1/b.txt"));
    REQUIRE(filesystem->isRegularFile("/dir1/b.txt"));
}

TEST_CASE("fs::Path tests", "[engine]") {
    REQUIRE(fs::Path().empty() == true);
    REQUIRE(fs::Path("/a/b").string() == "/a/b");
    
    {
        std::string str = "/path/to/file";
        REQUIRE(fs::Path(str).string() == "/path/to/file");
        REQUIRE(fs::Path(std::move(str)).string() == "/path/to/file");
        REQUIRE(fs::Path(std::string_view("/view")).string() == "/view");
        REQUIRE(fs::Path("/char").string() == "/char");

        std::vector<std::string> strs = { "path", "to", "file" };
        REQUIRE(fs::Path(strs, true) == "/path/to/file");
        REQUIRE(fs::Path(strs, false) == "path/to/file");
    }

    {
        fs::Path original("/a/b");
        fs::Path copy(original);
        REQUIRE(copy.string() == "/a/b");

        fs::Path moved(std::move(original));
        REQUIRE(moved.string() == "/a/b");

        fs::Path assign;
        assign = "/new/path";
        REQUIRE(assign.string() == "/new/path");
    }

    fs::Path file("/path/to/file.txt");
    REQUIRE(file.filename() == "file.txt");
    REQUIRE(file.extension() == ".txt");
    REQUIRE(file.stem() == "file");
    REQUIRE(file.parentPath() == "/path/to");

    fs::Path dir("/path/to/dir/");
    REQUIRE(dir.filename() == "dir");
    REQUIRE(dir.extension() == "");
    REQUIRE(dir.parentPath() == "/path/to");

    fs::Path dir1("/path/to/dir");
    REQUIRE(dir1.filename() == "dir");
    REQUIRE(dir1.extension() == "");
    REQUIRE(dir1.parentPath() == "/path/to");

    fs::Path dir2("/a/b/c");
    REQUIRE(dir2.filename() == "c");
    REQUIRE(dir2.extension() == "");
    REQUIRE(dir2.parentPath() == "/a/b");

    fs::Path root("/");
    REQUIRE(root.filename() == "");
    REQUIRE(root.extension() == "");
    REQUIRE(root.parentPath() == "");
    
    fs::Path p1("/path/to");
    p1.append(fs::Path("dir/file.txt"));
    REQUIRE(p1 == "/path/to/dir/file.txt");

    fs::Path p2("./path/to/");
    p2 /= fs::Path("/dir");
    REQUIRE(p2 == "./path/to/dir");

    fs::Path p3("/path/to/file");
    p3 += fs::Path(".txt");
    REQUIRE(p3 == "/path/to/file.txt");

    fs::Path p4("/path");
    p4.clear();
    REQUIRE(p4.empty() == true);
    REQUIRE(p4.string() == "");
    REQUIRE(fs::Path().empty() == true);

    fs::Path to_split("/path/to/file.txt");
    auto components1 = to_split.split();
    REQUIRE(components1.size() == 3);
    REQUIRE(components1[0] == "path");
    REQUIRE(components1[1] == "to");
    REQUIRE(components1[2] == "file.txt");

    fs::Path to_split_dir("path/to/directory/");
    auto components2 = to_split_dir.split();
    REQUIRE(components2.size() == 3);
    REQUIRE(components2[0] == "path");
    REQUIRE(components2[1] == "to");
    REQUIRE(components2[2] == "directory");

    REQUIRE(fs::Path("/a/b/c/d").makeRelative("/a/b/e/f") == fs::Path("../../c/d"));
    REQUIRE(fs::Path("/a/b/c/d").makeRelative("/a/b/e/f/g") == fs::Path("../../../c/d"));

    REQUIRE(fs::Path("../a/b/../../c").makeAbsolute("") == fs::Path("/../c"));
    REQUIRE(fs::Path("a/b/../../../c").makeAbsolute("") == fs::Path("/../c"));
    REQUIRE(fs::Path("a/b/../c../d/e/../f").makeAbsolute("") == fs::Path("/a/c../d/f"));

    REQUIRE(fs::Path("../../../c/d").makeAbsolute("/a/b/e/f/g") == fs::Path("/a/b/c/d"));
    REQUIRE(fs::Path("../../c/d").makeAbsolute("/a/b/e/f") == fs::Path("/a/b/c/d"));
}
TEST_CASE("fs::NativeFilesystem tests", "[engine]") {
    for(uint i = 0; i < 2; ++i) {
        std::unique_ptr<fs::IFilesystem> filesystem(new fs::NativeFilesystem());
        filesystem->setBasePath("./tmp/test.d/");
        testFilesystem(filesystem.get());
    }
}