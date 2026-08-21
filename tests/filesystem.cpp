#include "catch2/catch_test_macros.hpp"
#include "fs/IFilesystem.hpp"
#include "fs/NativeFilesystem.hpp"
#include "fs/MemoryFilesystem.hpp"
#include "fs/VirtualFilesystem.hpp"
#include "fs/SubFilesystem.hpp"

#define CHECK_ERR(e) \
    if(e.failed) { \
        LOG_ERROR("{}\nat {}\nat {}:{}", e.message, e.path, __FILE__, __LINE__); \
        REQUIRE_FALSE(e.failed); \
    }
constexpr uint NUM = 26;

static void testFilesystem(fs::IFilesystem *filesystem) {
    fs::Error err;
    for(auto const &path : filesystem->getContents("/", false, &err)) {
        CHECK_ERR(err);
        REQUIRE(filesystem->exists(path, &err));
        filesystem->remove(path, &err);
        REQUIRE_FALSE(filesystem->exists(path, &err));
        CHECK_ERR(err);
    }

    auto file = filesystem->open("/test_stream", 0, &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/test_stream", &err));
    REQUIRE(filesystem->isRegularFile("/test_stream", &err));
    REQUIRE(file.isOpen());
    REQUIRE(file->size() == 0);
    std::string_view buff = "Hello, World!\nThis is line two\nToday's word: ";
    std::string_view word = "Parrot";
    std::string_view res = "Hello, World!\nThis is line two\nToday's word: Parrot";
    file->seekp(0);
    file->write(buff.data(), buff.size());
    file->write(word.data(), word.size());
    REQUIRE(file->size() == res.size());
    REQUIRE(file->tellp() == res.size());
    file.close();
    REQUIRE_FALSE(file.isOpen());
    REQUIRE(filesystem->fileSize("/test_stream", &err));
    CHECK_ERR(err);

    file = filesystem->open("/test_stream", fs::FileOpenMode::Append, &err);
    CHECK_ERR(err);
    REQUIRE(file.isOpen());
    REQUIRE(file->size() == res.size());
    std::vector<char> readBuff(file->size());
    file->seekg(0);
    file->read(readBuff.data(), readBuff.size());
    readBuff.emplace_back('\0');
    REQUIRE(res.compare(readBuff.data()) == 0);
    file->seekg(0, SeekDir::End);
    REQUIRE(file->size() == file->tellg());

    filesystem->remove("/test_stream", &err);
    REQUIRE_FALSE(file.isOpen());
    file = filesystem->open("/test_stream", fs::FileOpenMode::Append, &err);
    filesystem->move("/test_stream", "/test_stream1", &err);
    REQUIRE_FALSE(file.isOpen());
    file = filesystem->open("/test_stream", fs::FileOpenMode::Append, &err);

    filesystem->open("/test1");
    filesystem->copy("/test1", "/test_stream", &err);
    REQUIRE_FALSE(file.isOpen());
    filesystem->remove("/test1", &err);
    CHECK_ERR(err);

    filesystem->createDirectories("/test1/test2", &err);
    file = filesystem->open("/test1/test2/test_file", 0, &err);
    CHECK_ERR(err);
    filesystem->remove("/test1", &err);
    filesystem->remove("/test_stream", &err);
    filesystem->remove("/test_stream1", &err);
    CHECK_ERR(err);
    REQUIRE_FALSE(file.isOpen());
    REQUIRE_FALSE(filesystem->exists("/test1"));
    REQUIRE_FALSE(filesystem->exists("/test1/test2"));
    REQUIRE_FALSE(filesystem->exists("/test1/test2/test_file"));

    auto files = {"/a.txt", "b.a", "/c.a.x", "./file0", "file1", "/dir1/dir2/dir3/directory.d/file"};
    for(uint i = 0; i < NUM; ++i) {
        for(auto const &path : files) {
            filesystem->createDirectories(fs::Path(path).parentPath(), &err);
            CHECK_ERR(err);
            auto file = filesystem->open(path, fs::FileOpenMode::Append, &err);
            CHECK_ERR(err);
            REQUIRE(filesystem->exists(path, &err));
            REQUIRE(filesystem->isRegularFile(path, &err));
            CHECK_ERR(err);
            REQUIRE(file.isOpen());

            file->seekg(0, SeekDir::End);
            REQUIRE(file->tellg() == file->size());

            auto sizeBefore = file->size();
            char character = 'a' + i;
            file->write(&character, 1);
            REQUIRE(file->size() == sizeBefore + 1);

            file.close();
            REQUIRE_FALSE(file.isOpen());
        }
    }

    REQUIRE(filesystem->getContents("/", true, &err).size() == 10);
    CHECK_ERR(err);

    for(auto const &path : files) {
        REQUIRE(filesystem->exists(path, &err));
        CHECK_ERR(err);
        auto file = filesystem->open(path, 0, &err);
        CHECK_ERR(err);
        REQUIRE(file->size() == NUM);
        std::vector<char> buff(file->size());
        file->seekp(0);
        file->read(buff.data(), buff.size());
        for(uint i = 0; i < NUM; ++i) {
            REQUIRE(buff[i] == static_cast<char>('a' + i));
        }

        file.close();
    }

    filesystem->copy("/a.txt", "/dir1/b.txt", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/b.txt", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/b.txt", &err));
    CHECK_ERR(err);

    filesystem->copy("/dir1", "/dirCopy", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dirCopy/", &err));
    REQUIRE(filesystem->isDirectory("/dirCopy/", &err));
    REQUIRE(filesystem->exists("/dirCopy/b.txt", &err));
    REQUIRE(filesystem->isRegularFile("/dirCopy/b.txt", &err));
    REQUIRE(filesystem->exists("/dirCopy/dir2/dir3/directory.d", &err));
    REQUIRE(filesystem->isDirectory("/dirCopy/dir2/dir3/directory.d", &err));
    REQUIRE(filesystem->exists("/dirCopy/dir2/dir3/directory.d/file", &err));
    REQUIRE(filesystem->isRegularFile("/dirCopy/dir2/dir3/directory.d/file", &err));
    CHECK_ERR(err);

    filesystem->move("/dirCopy", "/dirMoved", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dirMoved/", &err));
    REQUIRE(filesystem->isDirectory("/dirMoved/", &err));
    REQUIRE(filesystem->exists("/dirMoved/b.txt", &err));
    REQUIRE(filesystem->isRegularFile("/dirMoved/b.txt", &err));
    REQUIRE(filesystem->exists("/dirMoved/dir2/dir3/directory.d", &err));
    REQUIRE(filesystem->isDirectory("/dirMoved/dir2/dir3/directory.d", &err));
    REQUIRE(filesystem->exists("/dirMoved/dir2/dir3/directory.d/file", &err));
    REQUIRE(filesystem->isRegularFile("/dirMoved/dir2/dir3/directory.d/file", &err));
    CHECK_ERR(err);

    filesystem->copy("/file0", "/dir1/file0_copy", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/file0_copy", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/file0_copy", &err));
    CHECK_ERR(err);

    filesystem->copy("/file0", "/dir1", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/file0", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/file0", &err));
    CHECK_ERR(err);

    filesystem->copy("/file0", "/dir1/file0_copy", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/file0_copy", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/file0_copy", &err));
    CHECK_ERR(err);
    
    filesystem->move("/file0", "/dir1", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/file0", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/file0", &err));
    REQUIRE_FALSE(filesystem->exists("/file0", &err));
    CHECK_ERR(err);

    filesystem->move("/file1", "/dir1/file0_moved", &err);
    CHECK_ERR(err);
    REQUIRE(filesystem->exists("/dir1/file0_moved", &err));
    REQUIRE(filesystem->isRegularFile("/dir1/file0_moved", &err));
    REQUIRE_FALSE(filesystem->exists("/file1", &err));
    CHECK_ERR(err);


    for(auto const &path : filesystem->getContents("/", true, &err)) {
        CHECK_ERR(err);
        REQUIRE(filesystem->exists(path, &err));
        if(filesystem->isDirectory(path, &err))
            continue;
        REQUIRE(filesystem->isRegularFile(path, &err));
        REQUIRE(filesystem->fileSize(path) == NUM);
        auto file = filesystem->open(path, 0, &err);
        CHECK_ERR(err);
        REQUIRE(file->size() == NUM);
        std::vector<char> buff(file->size());
        file->seekp(0);
        file->read(buff.data(), buff.size());
        for(uint i = 0; i < NUM; ++i) {
            REQUIRE(buff[i] == static_cast<char>('a' + i));
        }

        file.close();
    }

    filesystem->fileSize("/definitely/does/not/exist", &err);
    REQUIRE(err.failed);
    err.failed = false;
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

    REQUIRE(fs::Path("/a/b/c/").removeDirSeparator().string() == "/a/b/c");
    REQUIRE(fs::Path("/a/b/c/") == fs::Path("/a/b/c"));

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

    REQUIRE(fs::Path("../a/b/./.././../c/.").makeAbsolute("") == fs::Path("/../c"));
    REQUIRE(fs::Path("./a/b/../.././../c").makeAbsolute("") == fs::Path("/../c"));
    REQUIRE(fs::Path("././a/b/../c../d/e/../f").makeAbsolute("") == fs::Path("/a/c../d/f"));

    REQUIRE(fs::Path("../../../c/d").makeAbsolute("/a/b/e/f/g") == fs::Path("/a/b/c/d"));
    REQUIRE(fs::Path("../../c/d").makeAbsolute("/a/b/e/f") == fs::Path("/a/b/c/d"));
}
TEST_CASE("fs::NativeFilesystem tests", "[engine]") {
    for(uint i = 0; i < 2; ++i) {
        std::unique_ptr<fs::IFilesystem> filesystem(new fs::NativeFilesystem("./tmp/test.d/"));
        testFilesystem(filesystem.get());
    }
}
TEST_CASE("fs::VirtualFilesystem tests", "[engine]") {
    for(uint i = 0; i < 2; ++i) {
        std::unique_ptr<fs::VirtualFilesystem> filesystem(new fs::VirtualFilesystem());
        std::unique_ptr<fs::IFilesystem> root(new fs::NativeFilesystem("tmp/virtual_tests/root"));
        std::unique_ptr<fs::IFilesystem> tmp(new fs::NativeFilesystem("tmp/virtual_tests/tmp"));
        std::unique_ptr<fs::IFilesystem> res(new fs::NativeFilesystem("assets"));
        std::unique_ptr<fs::IFilesystem> shaders(new fs::NativeFilesystem("shaders"));
        filesystem->mount(root.get(), "/");
        filesystem->mount(tmp.get(), "/tmp");
        filesystem->mount(res.get(), "/res");
        filesystem->mount(shaders.get(), "/shaders");

        fs::Error err;
        REQUIRE(filesystem->exists("/"));
        REQUIRE(filesystem->exists("/res"));
        REQUIRE(filesystem->exists("/shaders"));

        if(filesystem->exists("/res-copy")) {
            filesystem->remove("/res-copy");
        }

        REQUIRE_FALSE(filesystem->exists("/res-copy"));
        
        filesystem->createDirectory("/res-copy");
        CHECK_ERR(err);
        for(auto entry : filesystem->getContents("/res")) {
            filesystem->copy(entry, entry.makeRelative("/res").makeAbsolute("/res-copy"));
            CHECK_ERR(err);
            REQUIRE(filesystem->exists(entry.makeRelative("/res").makeAbsolute("/res-copy")));
            CHECK_ERR(err);
        }

        filesystem->move("/res-copy", "/tmp/trash");
        CHECK_ERR(err);
        
        filesystem->unmount("/res");
        filesystem->unmount("/shaders");

        fs::SubFilesystem testFs(filesystem.get(), "/test");
        testFilesystem(&testFs);
    }
}
TEST_CASE("fs::MemoryFilesystem tests", "[engine]") {
    std::vector<std::byte> data;
    std::vector<fs::Path> contents;
    for(uint i = 0; i < 2; ++i) {
        std::unique_ptr<fs::MemoryFilesystem> filesystem(new fs::MemoryFilesystem());
        if(!data.empty())
            filesystem->deserialize(data.data(), data.size());
        testFilesystem(filesystem.get());
        data = filesystem->serialize();
        std::ofstream file("./tmp/mem_test", std::ios::out | std::ios::trunc | std::ios::binary);
        file.write(reinterpret_cast<char const *>(data.data()), data.size());
        contents = filesystem->getContents("/", true);
    }

    REQUIRE_FALSE(data.empty());
    std::unique_ptr<fs::MemoryFilesystem> filesystem(new fs::MemoryFilesystem());
    filesystem->deserialize(data.data(), data.size());

    for(auto const &entry : contents) {
        REQUIRE(filesystem->exists(entry));
        if(!filesystem->isRegularFile(entry)) {
            continue;
        }

        REQUIRE(filesystem->isRegularFile(entry));
        REQUIRE(filesystem->fileSize(entry) == NUM);
        auto file = filesystem->open(entry, 0);
        REQUIRE(file->size() == NUM);
        std::vector<char> buff(file->size());
        file->seekp(0);
        file->read(buff.data(), buff.size());
        for(uint i = 0; i < NUM; ++i) {
            REQUIRE(buff[i] == static_cast<char>('a' + i));
        }

        file.close();
    }
}
