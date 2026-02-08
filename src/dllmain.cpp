// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include "loader/component_loader.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include "MemoryMgr.h"

#include <safetyhook.hpp>
#include "hooking.h"

#include "buildnumber.h"

#include "cexception.hpp"
#include "shared.h"
bool isMP = false;
uintptr_t exe(uintptr_t sp, uintptr_t mp) {
    if(!isMP)
    return sp;

    return mp;
}

LPVOID GetModuleEndAddress(HMODULE hModule) {
    if (hModule == NULL) {
        return NULL;
    }


    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;


    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }


    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);


    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }


    DWORD imageSize = pNtHeaders->OptionalHeader.SizeOfImage;


    return (LPVOID)((BYTE*)hModule + imageSize);
}

void OpenConsoleAndRedirectIO() {
    // 1. Allocate a console if one isn't already attached
    if (GetConsoleWindow() == NULL) {
        AllocConsole();
        SetConsoleTitleA(MOD_NAME " console");
    }

    // 2. Redirect standard I/O streams to the console
    // Redirect stdout to the console output handle
    FILE* pFileStdout = nullptr;
    if (freopen_s(&pFileStdout, "CONOUT$", "w", stdout) != 0) {
        // Handle error if redirection fails
        // Optionally, use SetStdHandle here if needed
    }

    // Redirect stdin to the console input handle
    FILE* pFileStdin = nullptr;
    if (freopen_s(&pFileStdin, "CONIN$", "r", stdin) != 0) {
        // Handle error if redirection fails
    }

    // Redirect stderr to the console output handle
    FILE* pFileStderr = nullptr;
    if (freopen_s(&pFileStderr, "CONOUT$", "w", stderr) != 0) {
        // Handle error if redirection fails
    }


}

EXTERN_C __declspec(dllexport) int __cdecl SteamStartup(unsigned int usingMask, void* pError)
{
    return 1;
}

EXTERN_C __declspec(dllexport) int __cdecl SteamCleanup(void* pError)
{
    return 1;
}

EXTERN_C __declspec(dllexport) int __cdecl SteamIsAppSubscribed(unsigned int appId, int* pbIsAppSubscribed, int* pbIsSubscriptionPending, void* pError)
{
    *pbIsAppSubscribed = 1;
    *pbIsSubscriptionPending = 0;
    return 1;
}

SafetyHookInline LoadLibraryD;
SafetyHookInline FreeLibraryD;
uintptr_t gfx_d3d_dll;
HMODULE __stdcall LoadLibraryHook(const char* filename) {

    auto hModule = LoadLibraryD.unsafe_stdcall<HMODULE>(filename);

    if (strstr(filename, "steam.dll") != NULL && !hModule) {
        HMODULE moduleHandle;
        GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)LoadLibraryHook, &moduleHandle);
        return moduleHandle;
    }

    if (strstr(filename, "gfx_d3d") != NULL) {

        bool was_unloaded = gfx_d3d_dll == 0;

        gfx_d3d_dll = (uintptr_t)hModule;
        if(was_unloaded)
        component_loader::post_gfx();
    }
    return hModule;

}

bool is_shutdown = false;

#define GFX_D3D_OFF(x) (gfx_d3d_dll + (x - BASE_GFX))
uintptr_t gfx(uintptr_t sp, uintptr_t mp) {

    if (gfx_d3d_dll == NULL)
        return NULL;

    uintptr_t result = 0;


    if (!isMP)
        result = sp;
    else
        result = mp;
        result = GFX_D3D_OFF(result);
    
    return result;

}

BOOL __stdcall FreeLibraryHook(HMODULE hLibModule) {
    if (!is_shutdown) {
        char LibraryName[256]{};
        GetModuleFileNameA(hLibModule, LibraryName, sizeof(LibraryName));

        if (strstr(LibraryName, "gfx_d3d") != NULL) {
            gfx_d3d_dll = 0;
        }

        uintptr_t moduleStart = (uintptr_t)hLibModule;
        uintptr_t moduleEnd = (uintptr_t)GetModuleEndAddress(hLibModule);

        if (moduleEnd != 0) {
            // Check inline hooks
            for (auto& hookPtr : g_inlineHooks) {
                if (hookPtr) {
                    uintptr_t targetAddr = (uintptr_t)hookPtr->target_address();
                    if (targetAddr >= moduleStart && targetAddr < moduleEnd) {
                        hookPtr->reset();
                    }
                }
            }

            // Check mid hooks
            for (auto& hookPtr : g_midHooks) {
                if (hookPtr) {
                    uintptr_t targetAddr = (uintptr_t)hookPtr->target_address();
                    if (targetAddr >= moduleStart && targetAddr < moduleEnd) {
                        hookPtr->reset();
                    }
                }
            }

            // Clear out any reset hooks
            g_inlineHooks.erase(
                std::remove_if(g_inlineHooks.begin(), g_inlineHooks.end(),
                    [](const std::unique_ptr<SafetyHookInline>& h) { return !h || !(*h); }),
                g_inlineHooks.end()
            );

            g_midHooks.erase(
                std::remove_if(g_midHooks.begin(), g_midHooks.end(),
                    [](const std::unique_ptr<SafetyHookMid>& h) { return !h || !(*h); }),
                g_midHooks.end()
            );

        }
    }
    auto hModule = FreeLibraryD.unsafe_stdcall<BOOL>(hLibModule);
    return hModule;
}
uintptr_t CG_init_ptr;
int CG_Init_stub() {
    auto result = cdecl_call<int>(CG_init_ptr);
    component_loader::post_cg_init();
    return result;

}

uintptr_t post_sv_cheats_first_addr;
int post_sv_cheats_first() {
    component_loader::post_unpack();
    return cdecl_call<int>(post_sv_cheats_first_addr);
}
void Init() {
    OpenConsoleAndRedirectIO();
    uint32_t value;
    Memory::VP::Read(0x00434815, value);

    if (value == 0x57675868) {
        isMP = false;
        printf("SP\n");
    }
    else if (value == 0x3800F08F) {
        isMP = true;
        printf("MP\n");
        MessageBoxA(NULL, "YAP", "MP", 0);
    }
    else {
        MessageBoxA(NULL, "YAP", "Unsupported Title", 0);
        return;
    }
    shared::Init();

    SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);

    FreeLibraryD = safetyhook::create_inline(FreeLibrary, FreeLibraryHook);
    LoadLibraryD = safetyhook::create_inline(LoadLibraryA, LoadLibraryHook);

    HHOOK windowsHook = SetWindowsHookExA(WH_CALLWNDPROC, [](int code, WPARAM w, LPARAM l) -> LRESULT {
        if (code < 0) return CallNextHookEx(NULL, code, w, l);

        CWPSTRUCT* p = reinterpret_cast<CWPSTRUCT*>(l);
        static HWND hConsole = NULL;
        static bool isWine = false;
        static char windowTitle[256];
        static constexpr char COD2SPWINDOW[] = "Call of Duty 2";
        static constexpr char COD2MPWINDOW[] = "Multiplayer";
        static constexpr char EXCEPTIONTRACERWINDOW[] = "Application Crash";
        static constexpr char EXTERNALCONSOLEWINDOW[] = "Console";

        if (!hConsole) {
            hConsole = GetConsoleWindow();
            typedef const char* (CDECL* wine_get_version_t)(void);
            HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
            wine_get_version_t pwine_get_version = NULL;
            if (hNtdll) {
                pwine_get_version = (wine_get_version_t)GetProcAddress(hNtdll, "wine_get_version");
            }
            isWine = (pwine_get_version != NULL);
        }

        // Filter messages
        static std::unordered_map<HWND, uint8_t> windowsHookCache;
        // Bits: 0 = CoD2(SP/MP), 1 = ExceptionTracer, 2 = ExternalConsole, 3 = AlreadyProcessed
        if (p->message != WM_CREATE && p->message != WM_SIZE) {
            return CallNextHookEx(NULL, code, w, l);
        }

        // Check the cache
        auto it = windowsHookCache.find(p->hwnd);
        if (it != windowsHookCache.end()) {
            uint8_t flags = it->second;
            if (flags & (1 << 1)) return CallNextHookEx(NULL, code, w, l);  // ExceptionTracer
            if (p->message == WM_SIZE && (flags & 8)) return CallNextHookEx(NULL, code, w, l);  // AlreadyProcessed
        }

        // Check for the game's own windows
        GetWindowTextA(p->hwnd, windowTitle, sizeof(windowTitle));
        bool isCoD2SP = (strcmp(windowTitle, COD2SPWINDOW) == 0);
        bool isCoD2MP = (strstr(windowTitle, COD2MPWINDOW) != nullptr);
        bool isExceptionTracer = (strcmp(windowTitle, EXCEPTIONTRACERWINDOW) == 0);
        bool isExternalConsole = (strstr(windowTitle, EXTERNALCONSOLEWINDOW) != nullptr);

        // Cache the window type
        uint8_t flags = 0;
        if (isCoD2SP || isCoD2MP) flags |= 1;          // Bit 0: CoD2(SP/MP)
        else if (isExceptionTracer) flags |= (1 << 1); // Bit 1: ExceptionTracer
        else if (isExternalConsole) flags |= (1 << 2); // Bit 2: ExternalConsole
        windowsHookCache[p->hwnd] = flags;

        if (isExceptionTracer) return CallNextHookEx(NULL, code, w, l);

        if (p->message == WM_CREATE) {
            // Set the game icon for all windows
            if (hConsole == NULL || isWine) {
                char modulePath[MAX_PATH];
                if (GetModuleFileNameA(NULL, modulePath, sizeof(modulePath))) {
                    HICON hIcon = ExtractIconA(NULL, modulePath, 0);
                    if (hIcon && hIcon != (HICON)INVALID_HANDLE_VALUE) {
                        HICON hDup = CopyIcon(hIcon);
                        SetClassLongPtrW(p->hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(hDup));
                        DestroyIcon(hIcon);
                    }
                }
            }
            else if (hConsole != p->hwnd) {
                HICON hIconBig = reinterpret_cast<HICON>(GetClassLongPtrA(hConsole, GCLP_HICON));
                HICON hIconSmall = reinterpret_cast<HICON>(GetClassLongPtrA(hConsole, GCLP_HICONSM));
                if (hIconBig) SetClassLongPtrA(p->hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(hIconBig));
                if (hIconSmall) SetClassLongPtrA(p->hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(hIconSmall));
            }

            // Exclude the splash screen
            if (!(GetWindowLongA(p->hwnd, GWL_STYLE) & WS_DLGFRAME)) {
                return CallNextHookEx(NULL, code, w, l);
            }

            if (isCoD2SP || isCoD2MP) {
                // Add minimize button for game windows
                LONG style = GetWindowLongA(p->hwnd, GWL_STYLE);
                SetWindowLongA(p->hwnd, GWL_STYLE, style | WS_MINIMIZEBOX);

                // Automatically apply dark titlebar to game windows
                if (!isWine && p->message == WM_CREATE) {
                    BOOL darkMode = TRUE;
                    // DWMWA_USE_IMMERSIVE_DARK_MODE
                    if (hConsole && hConsole != p->hwnd) DwmGetWindowAttribute(hConsole, 20, &darkMode, sizeof(darkMode));
                    if (FAILED(DwmSetWindowAttribute(p->hwnd, 20, &darkMode, sizeof(darkMode)))) {
                        // XP/Vista/7/DWM-off
                    }
                }
            }

            // Unhide external console
            else if (isExternalConsole) {
                ShowWindow(p->hwnd, SW_SHOW);
                SetWindowPos(p->hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                PostMessage(p->hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            }
        }

        // Move windows to the foreground
        else if (p->message == WM_SIZE && !isExternalConsole) {
            LONG style = GetWindowLongA(p->hwnd, GWL_STYLE);
            if ((style & WS_SYSMENU) == 0) return CallNextHookEx(NULL, code, w, l);

            windowsHookCache[p->hwnd] |= 8;  // Bit 3: AlreadyProcessed

            // Detect only for new windows
            auto it2 = windowsHookCache.find(p->hwnd);
            if (it2 != windowsHookCache.end() && (it2->second & 8)) {
                return CallNextHookEx(NULL, code, w, l);
            }

            WINDOWPLACEMENT wp = { sizeof(wp) };
            if (GetWindowPlacement(p->hwnd, &wp)) {
                AllowSetForegroundWindow(ASFW_ANY);
                if (wp.showCmd != SW_NORMAL) {
                    ShowWindow(p->hwnd, SW_RESTORE);
                }
                SetWindowPos(p->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                SetWindowPos(p->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                SetForegroundWindow(p->hwnd);
            }
        }
        return CallNextHookEx(NULL, code, w, l); }, NULL, GetCurrentThreadId());

    component_loader::post_start();
    Memory::VP::InterceptCall(exe(0x4A3999,0x4C0E20), CG_init_ptr, CG_Init_stub);

    Memory::VP::InterceptCall(exe(0x4509AF,0x466524), post_sv_cheats_first_addr, post_sv_cheats_first);

    auto hunk = exe(0x426AB1 + 1);
    if (hunk) {
        Memory::VP::Patch<uint32_t>(hunk, 512);
    }

    static const char* version = MOD_NAME " r" BUILD_NUMBER_STR;
    if(exe(1))
    Memory::VP::Patch<const char*>(exe((0x004060E6 + 1)), version);

}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        Init();

        HMODULE moduleHandle;
        // idk why but this makes it not DETATCH prematurely
        GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)DllMain, &moduleHandle);

    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

