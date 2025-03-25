#include <Windows.h>

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

typedef struct {
	BOOL popup;
} Settings;

// Флаги состояний в одном байте
#define FLAG_ENABLED 0x01
#define FLAG_CAPS_PROCESSED 0x02
#define FLAG_SHIFT_PROCESSED 0x04
#define FLAG_WIN_PRESSED 0x08

void ShowError(LPCSTR message);
DWORD GetOSVersion();
void PressKey(int keyCode);
void ReleaseKey(int keyCode);
void ToggleCapsLockState();
void ShowLanguagePopup();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

HHOOK hHook;
BYTE stateFlags = FLAG_ENABLED; // Инициализируем как включенное

Settings settings = {
	.popup = FALSE
};

// Кэшированная версия ОС
static DWORD cachedOSVersion = 0;

int main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "nopopup") == 0)
	{
		settings.popup = FALSE;
	}
	else
	{
		settings.popup = GetOSVersion() >= 10;
	}

	HANDLE hMutex = CreateMutex(0, 0, "Switchy");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		ShowError("Another instance of Switchy is already running!");
		return 1;
	}

	hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, 0, 0);
	if (hHook == NULL)
	{
		ShowError("Error calling \"SetWindowsHookEx(...)\"");
		return 1;
	}

	MSG messages;
	while (GetMessage(&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}

	UnhookWindowsHookEx(hHook);

	return 0;
}

void ShowError(LPCSTR message)
{
	MessageBox(NULL, message, "Error", MB_OK | MB_ICONERROR);
}

DWORD GetOSVersion()
{
	if (cachedOSVersion == 0)
	{
		HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
		RTL_OSVERSIONINFOW osvi = { 0 };

		if (hMod)
		{
			RtlGetVersionPtr p = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
			if (p)
			{
				osvi.dwOSVersionInfoSize = sizeof(osvi);
				p(&osvi);
				cachedOSVersion = osvi.dwMajorVersion;
			}
		}
	}
	return cachedOSVersion;
}

void PressKey(int keyCode)
{
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = keyCode;
	input.ki.dwFlags = 0;
	SendInput(1, &input, sizeof(INPUT));
}

void ReleaseKey(int keyCode)
{
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = keyCode;
	input.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(INPUT));
}

void ToggleCapsLockState()
{
	PressKey(VK_CAPITAL);
	ReleaseKey(VK_CAPITAL);
}

void ShowLanguagePopup()
{
	PressKey(VK_LWIN);
	PressKey(VK_SPACE);
	ReleaseKey(VK_SPACE);
	stateFlags |= FLAG_WIN_PRESSED;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam;
	if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED))
	{
		if (key->vkCode == VK_CAPITAL)
		{
			if (wParam == WM_SYSKEYDOWN && !(stateFlags & FLAG_CAPS_PROCESSED))
			{
				stateFlags |= FLAG_CAPS_PROCESSED;
				stateFlags ^= FLAG_ENABLED;
				return 1;
			}

			if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
			{
				stateFlags &= ~FLAG_CAPS_PROCESSED;

				if (stateFlags & FLAG_WIN_PRESSED)
				{
					stateFlags &= ~FLAG_WIN_PRESSED;
					ReleaseKey(VK_LWIN);
				}

				if ((stateFlags & FLAG_ENABLED) && !settings.popup)
				{
					if (!(stateFlags & FLAG_SHIFT_PROCESSED))
					{
						PressKey(VK_MENU);
						PressKey(VK_LSHIFT);
						ReleaseKey(VK_MENU);
						ReleaseKey(VK_LSHIFT);
					}
					else
					{
						stateFlags &= ~FLAG_SHIFT_PROCESSED;
					}
				}
			}

			if (!(stateFlags & FLAG_ENABLED))
			{
				return CallNextHookEx(hHook, nCode, wParam, lParam);
			}

			if (wParam == WM_KEYDOWN && !(stateFlags & FLAG_CAPS_PROCESSED))
			{
				stateFlags |= FLAG_CAPS_PROCESSED;

				if (stateFlags & FLAG_SHIFT_PROCESSED)
				{
					ToggleCapsLockState();
					return 1;
				}
				else if (settings.popup)
				{
					ShowLanguagePopup();
				}
			}
			return 1;
		}
		else if (key->vkCode == VK_LSHIFT)
		{
			if ((wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && !(stateFlags & FLAG_CAPS_PROCESSED))
			{
				stateFlags &= ~FLAG_SHIFT_PROCESSED;
			}

			if (!(stateFlags & FLAG_ENABLED))
			{
				return CallNextHookEx(hHook, nCode, wParam, lParam);
			}

			if (wParam == WM_KEYDOWN && !(stateFlags & FLAG_SHIFT_PROCESSED))
			{
				stateFlags |= FLAG_SHIFT_PROCESSED;

				if (stateFlags & FLAG_CAPS_PROCESSED)
				{
					ToggleCapsLockState();
					if (settings.popup)
					{
						ShowLanguagePopup();
					}
					return 0;
				}
			}
			return 0;
		}
	}

	return CallNextHookEx(hHook, nCode, wParam, lParam);
}
