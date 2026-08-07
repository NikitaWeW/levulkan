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
            auto *file = filesystem->open(path, fs::FileOpenMode::Append);
            REQUIRE(filesystem->exists(path));
            REQUIRE(filesystem->isRegularFile(path));
            REQUIRE(file->isOpen());
            REQUIRE(file->tellp() == file->size());

            file->seekg(0, fs::SeekDir::End);
            REQUIRE(file->tellg() == file->size());

            auto sizeBefore = file->size();
            char character = 'a' + i;
            file->write(&character, 1);
            REQUIRE(file->size() == sizeBefore + 1);

            file->close();
            REQUIRE(!file->isOpen());
        }
    }

    REQUIRE(filesystem->getContents("/", true).size() == 10);

    for(auto const &path : files) {
        REQUIRE(filesystem->exists(path));
        auto *file = filesystem->open(path);
        REQUIRE(file->size() == NUM);
        std::vector<char> buff(file->size());
        file->seekp(0);
        file->read(buff.data(), buff.size());
        for(uint i = 0; i < NUM; ++i) {
            REQUIRE(buff[i] == 'a' + i);
        }

        file->close();
    }

    filesystem->copy("/a.txt", "/dir1/b.txt");
    REQUIRE(filesystem->exists("/dir1/b.txt"));
    REQUIRE(filesystem->isRegularFile("/dir1/b.txt"));

    filesystem->copy("/dir1", "/dirCopy");
    REQUIRE(filesystem->exists("/dir1/b.txt"));
    REQUIRE(filesystem->isRegularFile("/dir1/b.txt"));

    // TODO: finish tests
}

TEST_CASE("fs::Path tests", "[engine]") {
    REQUIRE(fs::Path().empty() == true);
    REQUIRE(fs::Path("/a/b").string() == "/a/b");
    
    std::string str = "/path/to/file";
    REQUIRE(fs::Path(str).string() == "/path/to/file");
    REQUIRE(fs::Path(std::move(str)).string() == "/path/to/file");
    REQUIRE(fs::Path(std::string_view("/view")).string() == "/view");
    REQUIRE(fs::Path("/char").string() == "/char");

    fs::Path original("/a/b");
    fs::Path copy(original);
    REQUIRE(copy.string() == "/a/b");
    
    fs::Path moved(std::move(original));
    REQUIRE(moved.string() == "/a/b");

    fs::Path assign;
    assign = "/new/path";
    REQUIRE(assign.string() == "/new/path");

    fs::Path file("/path/to/file.txt");
    REQUIRE(file.filename().string() == "file.txt");
    REQUIRE(file.extension().string() == ".txt");
    REQUIRE(file.stem().string() == "file");
    REQUIRE(file.parentPath().string() == "/path/to");

    fs::Path dir("/path/to/dir/");
    REQUIRE(dir.filename().string() == "");
    REQUIRE(dir.extension().string() == "");
    REQUIRE(dir.parentPath().string() == "/path/to");

    fs::Path root("/");
    REQUIRE(root.filename().string() == "");
    REQUIRE(root.extension().string() == "");
    REQUIRE(root.parentPath().string() == "/");
    
    fs::Path p1("/path/to");
    p1.append(fs::Path("dir/file.txt"));
    REQUIRE(p1.string() == "/path/to/dir/file.txt");

    fs::Path p2("/path/to/");
    p2 /= fs::Path("dir");
    REQUIRE(p2.string() == "/path/to/dir");

    fs::Path p3("/path/to/file");
    p3 += fs::Path(".txt");
    REQUIRE(p3.string() == "/path/to/file.txt");

    fs::Path p4("/path");
    p4.clear();
    REQUIRE(p4.empty() == true);
    REQUIRE(p4.string() == "");
    REQUIRE(fs::Path().empty() == true);

    REQUIRE(fs::Path("/valid/path").valid() == true);
    REQUIRE(fs::Path("relative/path").valid() == false);
    REQUIRE(fs::Path("").valid() == false);
    REQUIRE(fs::Path("//double//slash").valid() == false);

    fs::Path to_split("/path/to/file.txt");
    std::vector<fs::Path> components = to_split.split();
    REQUIRE(components.size() == 3);
    REQUIRE(components[0].string() == "path");
    REQUIRE(components[1].string() == "to");
    REQUIRE(components[2].string() == "file.txt");
}
TEST_CASE("fs::NativeFilesystem tests", "[engine]") {
    std::unique_ptr<fs::IFilesystem> filesystem(new fs::NativeFilesystem());
    filesystem->setBasePath("./tmp/test.d/");
    testFilesystem(filesystem.get());
}
TEST_CASE("fs::ArchiveFilesystem tests", "[engine]") {
    std::unique_ptr<fs::IFilesystem> filesystem(new fs::ArchiveFilesystem());
    filesystem->setBasePath("./tmp/test_data");
    testFilesystem(filesystem.get());
}