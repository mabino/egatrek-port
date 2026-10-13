// The emulated PC with tiny hand-assembled programs (run by the interpreter;
// no recompiled code and no game data needed).
#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dos/machine.h"

namespace fs = std::filesystem;
using trek::dos::Machine;

namespace {

constexpr std::uint16_t kLoad = 0x1000;

struct Prog {
    std::vector<std::uint8_t> image;
    trek::dos::Program program;
    explicit Prog(std::vector<std::uint8_t> code) : image(std::move(code)) {
        image.resize(image.size() + 0x200);  // room for data and a stack
        const auto sp = static_cast<std::uint16_t>(image.size());
        program = trek::dos::Program{image, kLoad, kLoad, 0, kLoad, sp, 0, {}, {}, {}, {}};
    }
};

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("trek-machine-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

Machine::State run_to_end(Machine& m, int slices = 50) {
    for (int i = 0; i < slices && m.state() == Machine::State::Running; ++i) m.run(100000, i * 16.0);
    return m.state();
}

std::vector<std::uint8_t> bytes(std::initializer_list<int> list) {
    std::vector<std::uint8_t> v;
    for (int b : list) v.push_back(static_cast<std::uint8_t>(b));
    return v;
}

}  // namespace

TEST_CASE("DOS console output reaches text memory, exit code is returned") {
    // mov ax,cs / mov ds,ax / mov dx,msg / mov ah,40h / mov bx,1 / mov cx,2 / int 21h / mov ax,4C2Ah / int 21h / "HI"
    Prog p(bytes({0x8C, 0xC8, 0x8E, 0xD8, 0xBA, 0x14, 0x00, 0xB4, 0x40, 0xBB, 0x01, 0x00, 0xB9, 0x02, 0x00, 0xCD,
                  0x21, 0xB8, 0x2A, 0x4C, 0xCD, 0x21, 'H', 'I'}));
    // The message sits at offset 0x16; patch the operand.
    p.image[5] = 0x16;
    Machine m(p.program, {});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 0x2A);
    CHECK(m.mem()[0xB8000] == 'H');
    CHECK(m.mem()[0xB8002] == 'I');
}

TEST_CASE("files are created in the save folder and read back") {
    TempDir saves, data;
    std::ofstream(data.path / "ORIG.DAT") << "abc";
    // Create TEST.DAT, write 3 bytes, close; open ORIG.DAT, read 3 bytes to 0100h; exit with their sum's low byte.
    std::vector<std::uint8_t> code = bytes({
        0x8C, 0xC8, 0x8E, 0xD8,              // mov ax,cs ; mov ds,ax
        0xB4, 0x3C, 0x31, 0xC9,              // mov ah,3Ch ; xor cx,cx
        0xBA, 0x60, 0x00, 0xCD, 0x21,        // mov dx,0060h ; int 21h   (create "TEST.DAT")
        0x89, 0xC3,                          // mov bx,ax
        0xB4, 0x40, 0xB9, 0x03, 0x00,        // mov ah,40h ; mov cx,3
        0xBA, 0x70, 0x00, 0xCD, 0x21,        // mov dx,0070h ; int 21h   (write "xyz")
        0xB4, 0x3E, 0xCD, 0x21,              // mov ah,3Eh ; int 21h     (close)
        0xB8, 0x00, 0x3D,                    // mov ax,3D00h
        0xBA, 0x80, 0x00, 0xCD, 0x21,        // mov dx,0080h ; int 21h   (open "orig.dat")
        0x89, 0xC3,                          // mov bx,ax
        0xB4, 0x3F, 0xB9, 0x03, 0x00,        // mov ah,3Fh ; mov cx,3
        0xBA, 0x00, 0x01, 0xCD, 0x21,        // mov dx,0100h ; int 21h   (read)
        0xA0, 0x00, 0x01,                    // mov al,[0100h]
        0x02, 0x06, 0x01, 0x01,              // add al,[0101h]
        0x02, 0x06, 0x02, 0x01,              // add al,[0102h]
        0xB4, 0x4C, 0xCD, 0x21,              // mov ah,4Ch ; int 21h
    });
    code.resize(0x60, 0x90);
    for (char ch : std::string("TEST.DAT")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x70, 0);
    for (char ch : std::string("xyz")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x80, 0);
    for (char ch : std::string("orig.dat")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x90, 0);
    Prog p(code);
    Machine m(p.program, {data.path, saves.path});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == (('a' + 'b' + 'c') & 0xFF));
    std::ifstream written(saves.path / "TEST.DAT");
    std::string text;
    written >> text;
    CHECK(text == "xyz");
}

TEST_CASE("INT 16h blocks until a key arrives") {
    // xor ah,ah / int 16h / mov ah,4Ch / int 21h  -> exit code = ASCII of the key
    Prog p(bytes({0x30, 0xE4, 0xCD, 0x16, 0xB4, 0x4C, 0xCD, 0x21}));
    Machine m(p.program, {});
    CHECK(m.run(100000, 0) == Machine::State::Running);
    CHECK(m.waiting_for_key());
    m.key_down(0x1E, 'a');
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 'a');
}

TEST_CASE("programs can hook interrupts with INT 21h AH=25h") {
    // Handler at 0040h: inc byte [0100h] ; iret. Main: set vector 60h, int 60h twice, exit with the count.
    std::vector<std::uint8_t> code = bytes({
        0x8C, 0xC8, 0x8E, 0xD8,              // mov ax,cs ; mov ds,ax
        0xBA, 0x40, 0x00, 0xB8, 0x60, 0x25,  // mov dx,0040h ; mov ax,2560h
        0xCD, 0x21,                          // int 21h
        0xCD, 0x60, 0xCD, 0x60,              // int 60h ; int 60h
        0xA0, 0x00, 0x01,                    // mov al,[0100h]
        0xB4, 0x4C, 0xCD, 0x21,              // mov ah,4Ch ; int 21h
    });
    code.resize(0x40, 0x90);
    for (int b : {0x2E, 0xFE, 0x06, 0x00, 0x01, 0xCF}) code.push_back(static_cast<std::uint8_t>(b));  // inc byte cs:[0100h] ; iret
    code.resize(0x110, 0);
    Prog p(code);
    Machine m(p.program, {});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 2);
}

TEST_CASE("divide by zero raises INT 0, which a program can handle") {
    // Handler at 0030h sets AL=7 and exits. Main hooks INT 0 then divides by zero.
    std::vector<std::uint8_t> code = bytes({
        0x8C, 0xC8, 0x8E, 0xD8,              // mov ax,cs ; mov ds,ax
        0xBA, 0x30, 0x00, 0xB8, 0x00, 0x25,  // mov dx,0030h ; mov ax,2500h
        0xCD, 0x21,                          // int 21h
        0x31, 0xC9, 0xF7, 0xF1,              // xor cx,cx ; div cx
        0xB8, 0x01, 0x4C, 0xCD, 0x21,        // mov ax,4C01h ; int 21h (not reached)
    });
    code.resize(0x30, 0x90);
    for (int b : {0xB8, 0x07, 0x4C, 0xCD, 0x21}) code.push_back(static_cast<std::uint8_t>(b));
    Prog p(code);
    Machine m(p.program, {});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 7);
}

TEST_CASE("executing outside the program image is a fault, not a crash") {
    // jmp far 9000:0000 (empty memory)
    Prog p(bytes({0xEA, 0x00, 0x00, 0x00, 0x90}));
    Machine m(p.program, {});
    CHECK(run_to_end(m) == Machine::State::Faulted);
    CHECK(m.fault().find("left the program image at 9000:0000") != std::string::npos);
}

TEST_CASE("BIOS clock follows host time") {
    Prog p(bytes({0xEB, 0xFE}));  // jmp $
    Machine m(p.program, {});
    m.set_clock_override(0.0);
    m.run(1000, 0.0);
    const std::uint32_t t0 = m.mem()[0x46C] | m.mem()[0x46D] << 8;
    m.set_clock_override(1000.0);
    m.run(1000, 1000.0);
    const std::uint32_t t1 = m.mem()[0x46C] | m.mem()[0x46D] << 8;
    CHECK(t1 - t0 == 18);  // 18.2 ticks per second
}

TEST_CASE("PC speaker: PIT channel 2 square wave while port 61h enables it") {
    // mov al,B6h / out 43h,al / mov al,a9h / out 42h,al / mov al,04h / out 42h,al  (divisor 04A9h = ~1000 Hz)
    // in al,61h / or al,3 / out 61h,al / jmp $
    Prog p(bytes({0xB0, 0xB6, 0xE6, 0x43, 0xB0, 0xA9, 0xE6, 0x42, 0xB0, 0x04, 0xE6, 0x42, 0xE4, 0x61, 0x0C, 0x03,
                  0xE6, 0x61, 0xEB, 0xFE}));
    Machine m(p.program, {});
    m.run(1000, 0.0);
    CHECK(m.speaker().on());
    CHECK(m.speaker().frequency() == doctest::Approx(1000.15).epsilon(0.001));
    std::vector<float> out;
    m.speaker().render(out, 8000, 10.0);
    CHECK(out.size() == 80);
    int sign_changes = 0;
    for (std::size_t i = 1; i < out.size(); ++i) sign_changes += (out[i] > 0) != (out[i - 1] > 0);
    CHECK(sign_changes >= 18);  // ~1 kHz over 10 ms
}
