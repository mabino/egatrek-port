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

TEST_CASE("Machine unsaved progress tracking") {
    fs::path temp_dir = fs::temp_directory_path() / "trek_test_unsaved_progress";
    fs::create_directories(temp_dir);

    std::vector<std::uint8_t> image(0x4000, 0);
    trek::dos::Program prog{image, 0x1000, 0x1000, 0, 0x1000, 0x200, 0, {}, {}, {}, {}};
    trek::dos::Machine m(prog, {{}, temp_dir});

    // Initially no unsaved progress
    CHECK_FALSE(m.has_unsaved_progress());

    m.mark_unsaved();
    CHECK(m.has_unsaved_progress());

    m.mark_saved();
    CHECK_FALSE(m.has_unsaved_progress());

    // Non-SAVE command via handle_cmd_input
    // Setup DS:0x1065 as Pascal string "MOVE" (length 4, 'M', 'O', 'V', 'E')
    m.wr8(m.cpu.s[trek::dos::DS], 0x1065, 4);
    m.wr8(m.cpu.s[trek::dos::DS], 0x1066, 'M');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1067, 'O');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1068, 'V');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1069, 'E');
    m.handle_cmd_input();
    CHECK(m.has_unsaved_progress());

    // SAVE command via handle_cmd_input should not mark unsaved
    m.mark_saved();
    m.wr8(m.cpu.s[trek::dos::DS], 0x1065, 4);
    m.wr8(m.cpu.s[trek::dos::DS], 0x1066, 'S');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1067, 'A');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1068, 'V');
    m.wr8(m.cpu.s[trek::dos::DS], 0x1069, 'E');
    m.handle_cmd_input();
    CHECK_FALSE(m.has_unsaved_progress());

    // handle_save resets unsaved progress
    m.mark_unsaved();
    CHECK(m.has_unsaved_progress());
    m.handle_save();
    CHECK_FALSE(m.has_unsaved_progress());

    // Game over flag at DS:0x1526 clears unsaved progress
    m.mark_unsaved();
    CHECK(m.has_unsaved_progress());
    m.wr8(m.cpu.s[trek::dos::DS], 0x1526, 1);
    CHECK_FALSE(m.has_unsaved_progress());

    fs::remove_all(temp_dir);
}

TEST_CASE("accepts valid save file with CRLF line endings") {
    const fs::path crlf_temp = fs::temp_directory_path() / "trek_crlf_temp.sav";
    {
        std::ofstream out(crlf_temp, std::ios::binary);
        out << "1\r\n"; // skill 1
        for (int i = 1; i < 222; ++i) {
            if (i == 24) out << "POTEMKIN\r\n";
            else out << "99\r\n";
        }
        out << "FALSE\r\n";
        out << "FALSE\r\n";
        out << "TRUE\r\n";
        out << "TRUE\r\n";
        out << "FALSE\r\n";
    }
    CHECK(trek::validate_save_file(crlf_temp));
    fs::remove(crlf_temp);
}

}

