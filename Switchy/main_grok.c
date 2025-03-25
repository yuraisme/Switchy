#include <Windows.h>
#if _DEBUG
#include <stdio.h>
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...) do {} while (0)
#endif

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

typedef struct {
    BOOL popup;
} Settings;

typedef struct {
    BOOL enabled;
    BOOL capsProcessed;
    BOOL shiftProcessed;
    BOOL winPressed;
    Settings settings;
} KeyboardState;

HHOOK hHook;
KeyboardState state = { TRUE, FALSE, FALSE, FALSE, {FALSE} };

void ShowError(LPCSTR message) {
    MessageBox(NULL, message, "Error", MB_OK | MB_ICONERROR);
}

DWORD GetOSVersion() {
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
    if (hMod) {
        RtlGetVersionPtr p = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (p && p(&osvi) == 0) {
            return osvi.dwMajorVersion;
        }
    }
    return 0;
}

void PressKey(int keyCode) {
    keybd_event(keyCode, 0, 0, 0);
}

void ReleaseKey(int keyCode) {
    keybd_event(keyCode, 0, KEYEVENTF_KEYUP, 0);
}

void ToggleCapsLockState() {
    PressKey(VK_CAPITAL);
    ReleaseKey(VK_CAPITAL);
    // LOG("Caps Lock state has been toggled\n");
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED)) {
        // LOG("Key %d has been %s\n", key->vkCode, 
        //     (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? "pressed" : "released");

        if (key->vkCode == VK_CAPITAL) {
            if (wParam == WM_SYSKEYDOWN && !state.capsProcessed) {
                state.capsProcessed = TRUE;
                state.enabled = !state.enabled;
                // LOG("Switchy has been %s\n", state.enabled ? "enabled" : "disabled");
                return 1;
            }
            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                state.capsProcessed = FALSE;
                if (state.winPressed) {
                    state.winPressed = FALSE;
                    ReleaseKey(VK_LWIN);
                }
                if (state.enabled && !state.settings.popup && !state.shiftProcessed) {
                    PressKey(VK_MENU);
                    PressKey(VK_LSHIFT);
                    ReleaseKey(VK_LSHIFT);
                    ReleaseKey(VK_MENU);
                }
            }
            if (wParam == WM_KEYDOWN && !state.capsProcessed) {
                state.capsProcessed = TRUE;
                if (state.shiftProcessed) {
                    ToggleCapsLockState();
                    return 1;
                }
                else if (state.settings.popup) {
                    PressKey(VK_LWIN);
                    PressKey(VK_SPACE);
                    ReleaseKey(VK_SPACE);
                    state.winPressed = TRUE;
                }
            }
            return state.enabled ? 1 : CallNextHookEx(hHook, nCode, wParam, lParam);
        }
        else if (key->vkCode == VK_LSHIFT) {
            if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && !state.capsProcessed) {
                state.shiftProcessed = FALSE;
            }
            if (wParam == WM_KEYDOWN && !state.shiftProcessed) {
                state.shiftProcessed = TRUE;
                if (state.capsProcessed) {
                    ToggleCapsLockState();
                    if (state.settings.popup) {
                        PressKey(VK_LWIN);
                        PressKey(VK_SPACE);
                        ReleaseKey(VK_SPACE);
                        state.winPressed = TRUE;
                    }
                    return 1;
                }
            }
            return state.enabled ? 0 : CallNextHookEx(hHook, nCode, wParam, lParam);
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "nopopup") == 0) {
        state.settings.popup = FALSE;
    }
    else {
        state.settings.popup = GetOSVersion() >= 10;
    }
    // LOG("Pop-up is %s\n", state.settings.popup ? "enabled" : "disabled");

    HANDLE hMutex = CreateMutex(NULL, FALSE, "Switchy");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShowError("Another instance of Switchy is already running!");
        return 1;
    }

    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (hHook == NULL) {
        ShowError("Error calling \"SetWindowsHookEx(...)\"");
        CloseHandle(hMutex);
        return 1;
    }

    MSG messages;
    while (GetMessage(&messages, NULL, 0, 0)) {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }

    UnhookWindowsHookEx(hHook);
    CloseHandle(hMutex);
    return 0;
}