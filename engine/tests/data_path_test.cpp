#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "core/data_path.h"

namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path root;
    TempDir() {
        root = fs::temp_directory_path() / ("trek-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(root);
        std::ofstream(root / "Trek.Exe") << "x";
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};
}  // namespace

TEST_CASE("resolves DOS names case-insensitively") {
    TempDir t;
    trek::DataPath data(t.root);
    auto hit = data.resolve("TREK.EXE");
    REQUIRE(hit.has_value());
    CHECK(hit->filename() == "Trek.Exe");
    CHECK(data.resolve("trek.exe") == hit);
}

TEST_CASE("missing files and paths are rejected") {
    TempDir t;
    trek::DataPath data(t.root);
    CHECK_FALSE(data.resolve("TREK.SAV").has_value());
    CHECK_FALSE(data.resolve("..").has_value());
    CHECK_FALSE(data.resolve("..\\etc\\passwd").has_value());
    CHECK_FALSE(data.resolve("sub/TREK.EXE").has_value());
    CHECK_FALSE(data.resolve("").has_value());
}

TEST_CASE("install detection") {
    TempDir t;
    CHECK(trek::DataPath::looks_like_install(t.root));
    fs::remove(t.root / "Trek.Exe");
    CHECK_FALSE(trek::DataPath::looks_like_install(t.root));
}
