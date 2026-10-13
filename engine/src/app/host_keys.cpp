#include "app/host_keys.h"

#include <cctype>

namespace trek {

namespace {
constexpr char kShifted[][2] = {{'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
                                {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'},
                                {'[', '{'}, {']', '}'}, {';', ':'}, {'\'', '"'}, {'`', '~'}, {'\\', '|'},
                                {',', '<'}, {'.', '>'}, {'/', '?'}};
}  // namespace

std::uint8_t host_shift_flags(SDL_Keymod mod) {
    std::uint8_t f = 0;
    if (mod & SDL_KMOD_RSHIFT) f |= 0x01;
    if (mod & SDL_KMOD_LSHIFT) f |= 0x02;
    if (mod & SDL_KMOD_CTRL) f |= 0x04;
    if (mod & SDL_KMOD_ALT) f |= 0x08;
    if (mod & SDL_KMOD_CAPS) f |= 0x40;
    return f;
}

std::optional<dos::BiosKey> host_key(SDL_Keycode key, SDL_Keymod mod) {
    const bool shift = mod & SDL_KMOD_SHIFT, ctrl = mod & SDL_KMOD_CTRL, alt = mod & SDL_KMOD_ALT;
    if (key >= SDLK_F1 && key <= SDLK_F10) {
        const int n = static_cast<int>(key - SDLK_F1);
        const int base = alt ? 0x68 : ctrl ? 0x5E : shift ? 0x54 : 0x3B;
        return dos::BiosKey{static_cast<std::uint8_t>(base + n), 0};
    }
    switch (key) {
        case SDLK_UP: return dos::key_for_name("UP");
        case SDLK_DOWN: return dos::key_for_name("DOWN");
        case SDLK_LEFT: return dos::key_for_name("LEFT");
        case SDLK_RIGHT: return dos::key_for_name("RIGHT");
        case SDLK_HOME: return dos::key_for_name("HOME");
        case SDLK_END: return dos::key_for_name("END");
        case SDLK_PAGEUP: return dos::key_for_name("PGUP");
        case SDLK_PAGEDOWN: return dos::key_for_name("PGDN");
        case SDLK_INSERT: return dos::key_for_name("INS");
        case SDLK_DELETE: return dos::key_for_name("DEL");
        case SDLK_ESCAPE: return dos::key_for_name("ESC");
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return dos::key_for_name("ENTER");
        case SDLK_BACKSPACE: return dos::key_for_name("BS");
        case SDLK_TAB: return dos::key_for_name("TAB");
        default: break;
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_0) {
        static constexpr char digits[] = "1234567890";
        return dos::key_for_char(digits[key - SDLK_KP_1]);
    }
    if (key == SDLK_KP_PERIOD) return dos::key_for_char('.');
    if (key == SDLK_KP_MINUS) return dos::key_for_char('-');
    if (key == SDLK_KP_PLUS) return dos::key_for_char('+');
    if (key >= 0x80 || key == 0) return std::nullopt;

    char ch = static_cast<char>(key);
    if (ch >= 'a' && ch <= 'z') {
        auto k = dos::key_for_char(ch);
        if (!k) return std::nullopt;
        if (ctrl) return dos::BiosKey{k->scan, static_cast<std::uint8_t>(ch - 'a' + 1)};
        if (alt) return dos::BiosKey{k->scan, 0};
        const bool upper = shift != static_cast<bool>(mod & SDL_KMOD_CAPS);
        return dos::BiosKey{k->scan, static_cast<std::uint8_t>(upper ? std::toupper(ch) : ch)};
    }
    if (shift)
        for (const auto& pair : kShifted)
            if (pair[0] == ch) ch = pair[1];
    return dos::key_for_char(ch);
}

}  // namespace trek
