#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/data_path.h"
#include "core/save_file.h"
#include "dos/machine.h"

namespace fs = std::filesystem;

TEST_SUITE("save_file") {

TEST_CASE("validates known good save file") {
    // Find out/save/TREK.SAV relative to the test runner
    const fs::path test_save = "out/save/TREK.SAV";
    if (fs::exists(test_save)) {
        CHECK(trek::validate_save_file(test_save));
    }
}

TEST_CASE("rejects non-existent file") {
    CHECK_FALSE(trek::validate_save_file("/non/existent/path/save.sav"));
}

TEST_CASE("rejects empty file") {
    const fs::path empty = fs::temp_directory_path() / "trek_empty.sav";
    {
        std::ofstream out(empty);
    }
    CHECK_FALSE(trek::validate_save_file(empty));
    fs::remove(empty);
}

TEST_CASE("rejects file with incorrect line count") {
    const fs::path bad_count = fs::temp_directory_path() / "trek_bad_count.sav";
    {
        std::ofstream out(bad_count);
        for (int i = 0; i < 50; ++i) {
            out << i << "\n";
        }
    }
    CHECK_FALSE(trek::validate_save_file(bad_count));
    fs::remove(bad_count);
}

TEST_CASE("rejects file with invalid booleans at the end") {
    const fs::path bad_bool = fs::temp_directory_path() / "trek_bad_bool.sav";
    {
        std::ofstream out(bad_bool);
        out << "2\n"; // skill
        for (int i = 1; i < 222; ++i) {
            if (i == 24) out << "ENTERPRISE\n";
            else out << "100\n";
        }
        for (int i = 222; i < 227; ++i) {
            out << "INVALID\n";
        }
    }
    CHECK_FALSE(trek::validate_save_file(bad_bool));
    fs::remove(bad_bool);
}

TEST_CASE("accepts synthetically created valid save file") {
    const fs::path valid_temp = fs::temp_directory_path() / "trek_valid_temp.sav";
    {
        std::ofstream out(valid_temp);
        out << "3\n"; // skill 3
        for (int i = 1; i < 222; ++i) {
            if (i == 24) out << "DEFIANT\n";
            else out << "42\n";
        }
        out << "TRUE\n";
        out << "FALSE\n";
        out << "TRUE\n";
        out << "FALSE\n";
        out << "TRUE\n";
    }
    CHECK(trek::validate_save_file(valid_temp));
    fs::remove(valid_temp);
}

TEST_CASE("Machine active_save_path and prompt detection") {
    fs::path temp_dir = fs::temp_directory_path() / "trek_test_machine_saves";
    fs::create_directories(temp_dir);

    std::vector<std::uint8_t> image(0x4000, 0);
    trek::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    trek::dos::Machine m(prog, {{}, temp_dir});

    CHECK_FALSE(m.active_save_path().has_value());
    CHECK_FALSE(m.is_at_command_prompt());

    fs::path custom = temp_dir / "custom.sav";
    m.set_active_save_path(custom);
    CHECK(m.active_save_path().has_value());
    CHECK_EQ(*m.active_save_path(), custom);

    m.set_active_save_path(std::nullopt);
    CHECK_FALSE(m.active_save_path().has_value());

    // trigger_save feeds \x1bSAVE\r
    m.trigger_save();

    // handle_restore with no save file should safely cancel restore and not crash
    CHECK(m.handle_restore());

    fs::remove_all(temp_dir);
}

}
