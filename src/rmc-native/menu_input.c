#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdio.h>

static XINPUT_GAMEPAD read_pad(void) {
    static DWORD(WINAPI * state)(DWORD, XINPUT_STATE *);
    if (!state) {
        const wchar_t *libs[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
        for (unsigned i = 0; i < 3 && !state; ++i) {
            HMODULE lib = GetModuleHandleW(libs[i]);
            if (lib)
                state = (void *)GetProcAddress(lib, "XInputGetState");
        }
    }
    XINPUT_STATE pad;
    if (state)
        for (unsigned i = 0; i < 4; ++i)
            if (!state(i, &pad))
                return pad.Gamepad;
    return (XINPUT_GAMEPAD){0};
}
int menu_key(int title) { return GetAsyncKeyState(title ? VK_F2 : VK_SHIFT) < 0; }

unsigned menu_buttons(void) { return read_pad().wButtons; }
void menu_pad_text(char *text, size_t cap, int row) {
    XINPUT_GAMEPAD pad = read_pad();
    if (!row)
        snprintf(text, cap, "Buttons %04X  LT %u  RT %u", pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger);
    else if (row == 1)
        snprintf(text, cap, "Left %d / %d", pad.sThumbLX, pad.sThumbLY);
    else
        snprintf(text, cap, "Right %d / %d", pad.sThumbRX, pad.sThumbRY);
}
