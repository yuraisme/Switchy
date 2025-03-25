#include <Windows.h>
#include <cstdio>
#include <cstring>

// RAII-обёртка для HANDLE-ов (например, мьютекса)
class HandleWrapper {
public:
    explicit HandleWrapper(HANDLE handle) : m_handle(handle) {}
    ~HandleWrapper() {
        if (m_handle != nullptr) {
            CloseHandle(m_handle);
        }
    }
    // Запрещаем копирование
    HandleWrapper(const HandleWrapper&) = delete;
    HandleWrapper& operator=(const HandleWrapper&) = delete;
    // Перенос разрешён
    HandleWrapper(HandleWrapper&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }
    HandleWrapper& operator=(HandleWrapper&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                CloseHandle(m_handle);
            }
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }
private:
    HANDLE m_handle = nullptr;
};

// RAII-обёртка для хуков
class HookWrapper {
public:
    explicit HookWrapper(HHOOK hook) : m_hook(hook) {}
    ~HookWrapper() {
        if (m_hook != nullptr) {
            UnhookWindowsHookEx(m_hook);
        }
    }
    // Отключаем копирование
    HookWrapper(const HookWrapper&) = delete;
    HookWrapper& operator=(const HookWrapper&) = delete;
private:
    HHOOK m_hook = nullptr;
};

struct Settings {
    bool popup = false;
};

static HHOOK g_hHook = nullptr;
static bool g_enabled = true;
static bool g_capsProcessed = false;
static bool g_shiftProcessed = false;
static bool g_winPressed = false;
static Settings settings;

// Функция для получения версии ОС через RtlGetVersion (неофициальный API)
using RtlGetVersionPtr = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
DWORD GetOSVersion() {
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    RTL_OSVERSIONINFOW osvi = { 0 };
    if (hMod) {
        auto pRtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hMod, "RtlGetVersion"));
        if (pRtlGetVersion) {
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (pRtlGetVersion(&osvi) != 0) {
                // Если ошибка, можно вернуть 0 или обработать иначе
                return 0;
            }
        }
    }
    return osvi.dwMajorVersion;
}

void ShowError(LPCSTR message) {
    MessageBoxA(nullptr, message, "Error", MB_OK | MB_ICONERROR);
}

// Обёртка для формирования структуры INPUT
void SimulateKey(INPUT &input, WORD key, DWORD flags) {
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.wScan = 0;
    input.ki.dwFlags = flags;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;
}

void PressKey(int keyCode) {
    INPUT input = {};
    SimulateKey(input, static_cast<WORD>(keyCode), 0);
    SendInput(1, &input, sizeof(INPUT));
}

void ReleaseKey(int keyCode) {
    INPUT input = {};
    SimulateKey(input, static_cast<WORD>(keyCode), KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(INPUT));
}

void ToggleCapsLockState() {
    PressKey(VK_CAPITAL);
    ReleaseKey(VK_CAPITAL);
#if _DEBUG
    printf("Caps Lock state has been toggled\n");
#endif
}

// Низкоуровневый перехватчик клавиатуры
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Игнорируем инъецированные события
        if (key->flags & LLKHF_INJECTED) {
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }
#if _DEBUG
        const char* keyStatus = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? "pressed" : "released";
        printf("Key %d has been %s\n", key->vkCode, keyStatus);
#endif

        // Обработка клавиши CapsLock
        if (key->vkCode == VK_CAPITAL) {
            // При нажатии с WM_SYSKEYDOWN меняем глобальный флаг работы
            if ((wParam == WM_SYSKEYDOWN) && (!g_capsProcessed)) {
                g_capsProcessed = true;
                g_enabled = !g_enabled;
#if _DEBUG
                printf("Switchy has been %s\n", g_enabled ? "enabled" : "disabled");
#endif
                return 1; // событие обработано
            }
            // При отпускании клавиши CapsLock
            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                g_capsProcessed = false;
                if (g_winPressed) {
                    g_winPressed = false;
                    ReleaseKey(VK_LWIN);
                }
                if (g_enabled && !settings.popup) {
                    if (!g_shiftProcessed) {
                        // Имитация нажатия Alt+Shift
                        PressKey(VK_MENU);
                        PressKey(VK_LSHIFT);
                        ReleaseKey(VK_LSHIFT);
                        ReleaseKey(VK_MENU);
                    }
                    else {
                        g_shiftProcessed = false;
                    }
                }
            }
            if (!g_enabled) {
                return CallNextHookEx(g_hHook, nCode, wParam, lParam);
            }
            if ((wParam == WM_KEYDOWN) && (!g_capsProcessed)) {
                g_capsProcessed = true;
                if (g_shiftProcessed) {
                    ToggleCapsLockState();
                    return 1;
                }
                else {
                    if (settings.popup) {
                        // Имитация комбинации Win+Space
                        PressKey(VK_LWIN);
                        PressKey(VK_SPACE);
                        ReleaseKey(VK_SPACE);
                        g_winPressed = true;
                    }
                }
                return 1;
            }
        }
        // Обработка клавиши Left Shift
        else if (key->vkCode == VK_LSHIFT) {
            if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && !g_capsProcessed) {
                g_shiftProcessed = false;
            }
            if (!g_enabled) {
                return CallNextHookEx(g_hHook, nCode, wParam, lParam);
            }
            if ((wParam == WM_KEYDOWN) && (!g_shiftProcessed)) {
                g_shiftProcessed = true;
                if (g_capsProcessed) {
                    ToggleCapsLockState();
                    if (settings.popup) {
                        PressKey(VK_LWIN);
                        PressKey(VK_SPACE);
                        ReleaseKey(VK_SPACE);
                        g_winPressed = true;
                    }
                    return 1;
                }
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

int main(int argc, char** argv) {
    // Настройка popup в зависимости от аргументов командной строки и версии OS
    if (argc > 1 && strcmp(argv[1], "nopopup") == 0) {
        settings.popup = false;
    }
    else {
        settings.popup = (GetOSVersion() >= 10);
    }
#if _DEBUG
    printf("Pop-up is %s\n", settings.popup ? "enabled" : "disabled");
#endif

    // Создаем мьютекс для обеспечения единственного экземпляра
    HANDLE hMutex = CreateMutexA(nullptr, FALSE, "Global\\Switchy");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShowError("Another instance of Switchy is already running!");
        return 1;
    }
    // RAII-обёртка для мьютекса
    HandleWrapper mutexWrapper(hMutex);

    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (g_hHook == nullptr) {
        ShowError("Error calling SetWindowsHookEx(...)");
        return 1;
    }
    // RAII-обёртка для хука
    HookWrapper hookWrapper(g_hHook);

    // Запускаем цикл обработки сообщений
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
