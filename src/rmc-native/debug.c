#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "difficulty.h"
#include "tweaks_game.h"

enum { WARP = 3, HIDE, SHOW };
static HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
static unsigned command, component;
static int number, negative;
static unsigned digits;
static const int bounds[] = {18, 63, 255, 65535, 65535, 65535};
static int values[6];

static void print_console(const char *text) {
    DWORD count;
    WriteConsoleA(output, text, (DWORD)strlen(text), &count, NULL);
}

static void prompt(void) {
    static const char *const names[] = {"World (1-18)",
                                        "Room (0-63)",
                                        "Door (0-255)",
                                        "Map program (0-65535)",
                                        "Battle program (0-65535)",
                                        "Event program (0-65535)"};
    print_console(command == WARP ? names[component] : "Group (0-63)");
    print_console(" > ");
    number = negative = 0;
    digits = 0;
}

static int field_ready(void) {
    u64 player = rd64(data_at(0x2a105d0));
    return player && rd64(player + 0x5c0);
}

static void read_prompt(void) {
    DWORD available;
    if (!GetNumberOfConsoleInputEvents(input, &available)) {
        command = 0;
        return;
    }
    /* Limit work in a frame, even if the console contains a large paste. */
    if (available > 128)
        available = 128;
    while (command && available--) {
        INPUT_RECORD ev;
        DWORD got;
        if (!ReadConsoleInputW(input, &ev, 1, &got) || !got) {
            command = 0;
            return;
        }
        if (ev.EventType != KEY_EVENT || !ev.Event.KeyEvent.bKeyDown)
            continue;
        WCHAR c = ev.Event.KeyEvent.uChar.UnicodeChar;
        if (c == 27 || (c == '\r' && negative && number == 1)) {
            print_console("\r\nCancelled.\r\n");
            command = 0;
            break;
        }
        if (c == '\b') {
            if (digits) {
                number /= 10;
                digits--;
                print_console("\b \b");
            } else if (negative) {
                negative = 0;
                print_console("\b \b");
            }
        } else if (c == '-' && !digits && !negative) {
            negative = 1;
            print_console("-");
        } else if (c >= '0' && c <= '9' && digits < 6) {
            number = number * 10 + c - '0';
            digits++;
            char echo[2] = {(char)c, 0};
            print_console(echo);
        } else if (c == '\r') {
            print_console("\r\n");
            int bound = command == WARP ? bounds[component] : 63;
            int minimum = command == WARP && !component;
            if (!digits || negative || number < minimum || number > bound) {
                print_console("Value outside range. ");
                prompt();
                continue;
            }
            values[component++] = number;
            if (command == WARP && component < 6) {
                prompt();
                continue;
            }
            if (field_ready()) {
                if (command == WARP) {
                    struct {
                        u8 world, room, door, pad;
                        u16 programs[3];
                    } loc = {values[0], values[1], values[2], 0, {values[3], values[4], values[5]}};
                    _Static_assert(sizeof loc == 10, "warp record width");
                    call5(0x152990, &loc, 0, 0, 0, 0);
                } else if (command == HIDE)
                    call4(0x1629b0, values[0], 0, 0, 0);
                else
                    call4(0x162a00, values[0], 0, 0, 0);
            } else
                print_console("Field player unavailable.\r\n");
            command = 0;
        }
    }
}

static void begin_prompt(unsigned mode) {
    if (input == INVALID_HANDLE_VALUE) {
        /* GUI launches can lack a console or have redirected standard handles. */
        AllocConsole();
        input = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);
        output = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
        if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
            if (input != INVALID_HANDLE_VALUE)
                CloseHandle(input);
            if (output != INVALID_HANDLE_VALUE)
                CloseHandle(output);
            input = output = INVALID_HANDLE_VALUE;
            OutputDebugStringA("Roxas May Cry: debug console unavailable");
            return;
        }
    }
    command = mode;
    component = 0;
    print_console("\r\nRoxas May Cry. Esc or -1 cancels.\r\n");
    prompt();
}

void debug_frame(void) {
    static int enabled = -1, reset, turbo, boost, protect;
    static float speed;
    static ULONGLONG next;
    if (enabled < 0) {
        enabled = rmc_int("debug", "debug_on");
        reset = rmc_int("debug", "soft_reset");
        speed = (float)rmc_value("debug", "turbo_speed", 0);
    }
    if (!enabled)
        return;
    if (turbo)
        memcpy(rmc_site(0x717424), &speed, sizeof speed);
    if (command) {
        read_prompt();
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (now < next)
        return;
    if (GetKeyState(VK_TAB) < 0) {
        turbo = !turbo;
        float value = turbo ? speed : 1.0f;
        memcpy(rmc_site(0x717424), &value, sizeof value);
    } else if (GetKeyState(VK_F1) < 0) {
        boost = !boost;
        rmc_debug_damage(0, boost);
    } else if (GetKeyState(VK_F2) < 0) {
        protect = !protect;
        rmc_debug_damage(1, protect);
    } else if (GetKeyState(VK_F3) < 0)
        begin_prompt(WARP);
    else if (GetKeyState(VK_F4) < 0)
        begin_prompt(HIDE);
    else if (GetKeyState(VK_F5) < 0)
        begin_prompt(SHOW);
    else if (GetKeyState(VK_F6) < 0 && reset && field_ready())
        wr8(data_at(0xabac5a), 1);
    else
        return;
    next = now + 200;
}
