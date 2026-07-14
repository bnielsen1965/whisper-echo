#include "uinput.h"

#include <linux/input.h>
#include <linux/uinput.h>

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

static void uinput_err(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "uinput: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void uinput_err_sys(const char* op) {
    uinput_err("%s: %s", op, strerror(errno));
}

// ---------------------------------------------------------------------------
// Event emission helpers
// ---------------------------------------------------------------------------

static void emit_event(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    // Kernel supplies timestamp when both fields are zero.

    ssize_t n = write(fd, &ev, sizeof(ev));
    if (n < 0) {
        uinput_err_sys("write event");
    }
}

static void sync(int fd) {
    emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

static void key_press(int fd, uint16_t code) {
    emit_event(fd, EV_KEY, code, 1);
    sync(fd);
}

static void key_release(int fd, uint16_t code) {
    emit_event(fd, EV_KEY, code, 0);
    sync(fd);
}

static void key_tap(int fd, uint16_t code) {
    key_press(fd, code);
    key_release(fd, code);
}

// ---------------------------------------------------------------------------
// Character-to-keycode mapper
// ---------------------------------------------------------------------------
//
// Maps printable ASCII characters (plus a few control chars) to Linux
// input-event key codes for a standard US-QWERTY keyboard layout.
//
// Each entry: { char_literal, KEY_CODE, needs_shift }
// ---------------------------------------------------------------------------

struct CharMap {
    char c;
    uint16_t key;
    bool shift;
};

static constexpr std::array<CharMap, 96> KEYMAP = {
    {
        // Digits with shift (row above letters)
        {'!', KEY_1,   true}, {'@', KEY_2,   true}, {'#', KEY_3,   true},
        {'$', KEY_4,   true}, {'%', KEY_5,   true}, {'^', KEY_6,   true},
        {'&', KEY_7,   true}, {'*', KEY_8,   true}, {'(', KEY_9,   true},
        {')', KEY_0,   true},

        // Digits without shift
        {'1', KEY_1,   false}, {'2', KEY_2,   false}, {'3', KEY_3,   false},
        {'4', KEY_4,   false}, {'5', KEY_5,   false}, {'6', KEY_6,   false},
        {'7', KEY_7,   false}, {'8', KEY_8,   false}, {'9', KEY_9,   false},
        {'0', KEY_0,   false},

        // Punctuation: unshifted symbols (no shift)
        {'-', KEY_MINUS,     false}, {'=', KEY_EQUAL,     false},
        {'[', KEY_LEFTBRACE, false}, {']', KEY_RIGHTBRACE, false},
        {'\\', KEY_BACKSLASH, false}, {'\'', KEY_APOSTROPHE, false},
        {';', KEY_SEMICOLON, false},  {',', KEY_COMMA,     false},
        {'.', KEY_DOT,       false},  {'/', KEY_SLASH,     false},
        {'`', KEY_GRAVE,     false},

        // Punctuation: shifted symbols
        {'_', KEY_MINUS,     true},  {'+', KEY_EQUAL,     true},
        {'{', KEY_LEFTBRACE, true},  {'}', KEY_RIGHTBRACE, true},
        {'|', KEY_BACKSLASH, true},  {'"', KEY_APOSTROPHE, true},
        {':', KEY_SEMICOLON, true},  {'<', KEY_COMMA,     true},
        {'>', KEY_DOT,       true},  {'?', KEY_SLASH,     true},
        {'~', KEY_GRAVE,     true},

        // Space
        {' ', KEY_SPACE, false},

        // Lowercase letters (no shift)
        {'a', KEY_A, false}, {'b', KEY_B, false}, {'c', KEY_C, false},
        {'d', KEY_D, false}, {'e', KEY_E, false}, {'f', KEY_F, false},
        {'g', KEY_G, false}, {'h', KEY_H, false}, {'i', KEY_I, false},
        {'j', KEY_J, false}, {'k', KEY_K, false}, {'l', KEY_L, false},
        {'m', KEY_M, false}, {'n', KEY_N, false}, {'o', KEY_O, false},
        {'p', KEY_P, false}, {'q', KEY_Q, false}, {'r', KEY_R, false},
        {'s', KEY_S, false}, {'t', KEY_T, false}, {'u', KEY_U, false},
        {'v', KEY_V, false}, {'w', KEY_W, false}, {'x', KEY_X, false},
        {'y', KEY_Y, false}, {'z', KEY_Z, false},

        // Uppercase letters (shift)
        {'A', KEY_A, true}, {'B', KEY_B, true}, {'C', KEY_C, true},
        {'D', KEY_D, true}, {'E', KEY_E, true}, {'F', KEY_F, true},
        {'G', KEY_G, true}, {'H', KEY_H, true}, {'I', KEY_I, true},
        {'J', KEY_J, true}, {'K', KEY_K, true}, {'L', KEY_L, true},
        {'M', KEY_M, true}, {'N', KEY_N, true}, {'O', KEY_O, true},
        {'P', KEY_P, true}, {'Q', KEY_Q, true}, {'R', KEY_R, true},
        {'S', KEY_S, true}, {'T', KEY_T, true}, {'U', KEY_U, true},
        {'V', KEY_V, true}, {'W', KEY_W, true}, {'X', KEY_X, true},
        {'Y', KEY_Y, true}, {'Z', KEY_Z, true},
    }
};

static const CharMap* find_key(uint8_t ch) {
    for (const auto& entry : KEYMAP) {
        if (entry.c == static_cast<char>(ch)) {
            return &entry;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Type a single character
// ---------------------------------------------------------------------------

static void type_char(int fd, uint8_t ch) {
    const CharMap* mapping = find_key(ch);
    if (!mapping) {
        uinput_err("no key mapping for character 0x%02x ('%c')", ch, ch);
        return;
    }

    if (mapping->shift) {
        key_press(fd, KEY_LEFTSHIFT);
    }
    key_press(fd, mapping->key);
    key_release(fd, mapping->key);
    if (mapping->shift) {
        key_release(fd, KEY_LEFTSHIFT);
    }

    // Small delay between characters for reliability.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// ---------------------------------------------------------------------------
// Type a backspace
// ---------------------------------------------------------------------------

static void type_backspace(int fd) {
    key_tap(fd, KEY_BACKSPACE);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// ---------------------------------------------------------------------------
// Type a tab
// ---------------------------------------------------------------------------

static void type_tab(int fd) {
    key_tap(fd, KEY_TAB);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Type the full string, interpreting basic escape sequences
// ---------------------------------------------------------------------------

void uinput::type_string(int fd, const std::string& text) {
    for (const char* p = text.c_str(); *p; ++p) {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case 'n': ::key_tap(fd, KEY_ENTER);
                          std::this_thread::sleep_for(std::chrono::milliseconds(5));
                          break;
                case 't': type_tab(fd);        break;
                case 'b': type_backspace(fd);  break;
                case '\\': type_char(fd, '\\'); break;
                default:  type_char(fd, '\\');
                          if (*p) type_char(fd, static_cast<uint8_t>(*p));
                          break;
            }
        } else if (*p == '\n') {
            // Actual newline character — type Enter key
            key_tap(fd, KEY_ENTER);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else if (*p == '\r') {
            // Carriage return — ignore (often paired with \n)
        } else {
            type_char(fd, static_cast<uint8_t>(*p));
        }
    }
}

// ---------------------------------------------------------------------------
// Type a newline (Enter key)
// ---------------------------------------------------------------------------

void uinput::type_newline(int fd) {
    key_tap(fd, KEY_ENTER);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// ---------------------------------------------------------------------------
// Set up uinput device
// ---------------------------------------------------------------------------

int uinput::setup() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        uinput_err_sys("open /dev/uinput");
        return -1;
    }

    // Declare that this device emits key events.
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        uinput_err_sys("UI_SET_EVBIT");
        close(fd);
        return -1;
    }

    // Register every key code used in KEYMAP, plus modifiers and specials.
    std::vector<uint16_t> registered;
    for (const auto& entry : KEYMAP) {
        if (entry.key != KEY_LEFTSHIFT) {
            registered.push_back(entry.key);
        }
    }
    // Add shift and special keys.
    registered.push_back(KEY_LEFTSHIFT);
    registered.push_back(KEY_ENTER);
    registered.push_back(KEY_BACKSPACE);
    registered.push_back(KEY_TAB);

    for (uint16_t key : registered) {
        if (ioctl(fd, UI_SET_KEYBIT, key) < 0) {
            uinput_err_sys("UI_SET_KEYBIT");
            close(fd);
            return -1;
        }
    }

    // Device identity.
    struct uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;
    std::strncpy(setup.name, "whisper-echo virtual keyboard", sizeof(setup.name) - 1);

    // Try modern ioctl first; fall back to legacy write on older kernels.
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
        uinput_err("UI_DEV_SETUP not supported (old kernel?), falling back to legacy...");
        struct uinput_user_dev uud{};
        uud.id.bustype = BUS_USB;
        uud.id.vendor  = 0x1234;
        uud.id.product = 0x5678;
        uud.id.version = 1;
        std::strncpy(uud.name, "whisper-echo virtual keyboard",
                     sizeof(uud.name) - 1);
        if (write(fd, &uud, sizeof(uud)) < 0) {
            uinput_err_sys("write uinput_user_dev");
            close(fd);
            return -1;
        }
    }

    // Create the virtual device node.
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        uinput_err_sys("UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    // Delay to allow initialization.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    return fd;
}

// ---------------------------------------------------------------------------
// Type backspaces
// ---------------------------------------------------------------------------

void uinput::type_backspaces(int fd, int count) {
    for (int i = 0; i < count; ++i) {
        type_backspace(fd);
    }
}

// ---------------------------------------------------------------------------
// Type a space
// ---------------------------------------------------------------------------

static void type_space(int fd) {
    key_tap(fd, KEY_SPACE);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// ---------------------------------------------------------------------------
// Type spaces
// ---------------------------------------------------------------------------

void uinput::type_spaces(int fd, int count) {
    for (int i = 0; i < count; ++i) {
        type_space(fd);
    }
}

// ---------------------------------------------------------------------------
// Tear down uinput device
// ---------------------------------------------------------------------------

void uinput::teardown(int fd) {
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}
