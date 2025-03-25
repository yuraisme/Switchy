#include <Windows.h>

// Обёртка для управления HANDLE с автоматическим закрытием
class HandleWrapper {
public:
    explicit HandleWrapper(HANDLE handle) : handle_(handle) {}
    ~HandleWrapper() {
        if (handle_) {
            CloseHandle(handle_);
        }
    }
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
};

// Обёртка для управления хуком
class HookWrapper {
public:
    explicit HookWrapper(HHOOK hook) : hook_(hook) {}
    ~HookWrapper() {
        if (hook_) {
            UnhookWindowsHookEx(hook_);
        }
    }
    HHOOK get() const { return hook_; }
private:
    HHOOK hook_;
};

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

typedef struct {
    BOOL popup;
} Settings;

void ShowError(LPCSTR message);
DWORD GetOSVersion();
void PressKey(int keyCode);
void ReleaseKey(int keyCode);
void ToggleCapsLockState();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// Использование SendInput для имитации нажатия клавиш
void SendKey(WORD key, bool press) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    if (!press) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    SendInput(1, &input, sizeof(INPUT));
}

HHOOK g_hHook = nullptr;
BOOL enabled = TRUE;
BOOL keystrokeCapsProcessed = FALSE;
BOOL keystrokeShiftProcessed = FALSE;
BOOL winPressed = FALSE;
Settings settings = { .popup = FALSE };

int main(int argc, char** argv) {
    // Аргументы командной строки позволяют явно включать/выключать pop-up
    if (argc > 1) {
        if (strcmp(argv[1], "nopopup") == 0) {
            settings.popup = FALSE;
        } else if (strcmp(argv[1], "popup") == 0) {
            settings.popup = TRUE;
        }
    } else {
        settings.popup = GetOSVersion() >= 10;
    }

    // Создаём мьютекс, чтобы предотвратить запуск нескольких экземпляров
    HandleWrapper hMutex(CreateMutex(NULL, FALSE, "Switchy"));
    if (hMutex.get() == NULL) {
        ShowError("Failed to create mutex!");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShowError("Another instance of Switchy is already running!");
        return 1;
    }

    // Устанавливаем глобальный хук для клавиатуры
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (g_hHook == NULL) {
        ShowError("Error calling \"SetWindowsHookEx(...)\"");
        return 1;
    }
    HookWrapper hook(g_hHook);

    MSG messages;
    while (GetMessage(&messages, NULL, 0, 0)) {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }

    return 0;
}

void ShowError(LPCSTR message) {
    MessageBox(NULL, message, "Error", MB_OK | MB_ICONERROR);
}

DWORD GetOSVersion() {
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (!hMod)
        return 0;
    RtlGetVersionPtr p = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    if (!p)
        return 0;
    RTL_OSVERSIONINFOW osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    p(&osvi);
    return osvi.dwMajorVersion;
}

void PressKey(int keyCode) {
    SendKey((WORD)keyCode, true);
}

void ReleaseKey(int keyCode) {
    SendKey((WORD)keyCode, false);
}

void ToggleCapsLockState() {
    PressKey(VK_CAPITAL);
    ReleaseKey(VK_CAPITAL);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0 || lParam == 0) {
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;
    if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED)) {
        if (key->vkCode == VK_CAPITAL) {
            if (wParam == WM_SYSKEYDOWN && !keystrokeCapsProcessed) {
                keystrokeCapsProcessed = TRUE;
                enabled = !enabled;
                return 1;
            }

            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                keystrokeCapsProcessed = FALSE;
                if (winPressed) {
                    winPressed = FALSE;
                    ReleaseKey(VK_LWIN);
                }
                if (enabled && !settings.popup && !keystrokeShiftProcessed) {
                    PressKey(VK_MENU);
                    PressKey(VK_LSHIFT);
                    ReleaseKey(VK_MENU);
                    ReleaseKey(VK_LSHIFT);
                }
            }

            if (!enabled)
                return CallNextHookEx(g_hHook, nCode, wParam, lParam);

            if (wParam == WM_KEYDOWN && !keystrokeCapsProcessed) {
                keystrokeCapsProcessed = TRUE;
                if (keystrokeShiftProcessed) {
                    ToggleCapsLockState();
                    return 1;
                } else if (settings.popup) {
                    PressKey(VK_LWIN);
                    PressKey(VK_SPACE);
                    ReleaseKey(VK_SPACE);
                    winPressed = TRUE;
                }
            }
            return 1;
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
