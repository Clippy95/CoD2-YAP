// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include "loader/component_loader.h"


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

uintptr_t Com_Init_Try_Block_Function_addr;

namespace localization {
    void LoadNameMap_names();
}

char Com_Init_Try_Block_Function_stub() {


    auto result = cdecl_call<char>(Com_Init_Try_Block_Function_addr);
    component_loader::post_game_init();
    return result;
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

    component_loader::post_start();
    Memory::VP::InterceptCall(exe(0x4A3999,0x4C0E20), CG_init_ptr, CG_Init_stub);

    Memory::VP::InterceptCall(exe(0x4509AF,0x466524), post_sv_cheats_first_addr, post_sv_cheats_first);

    if (exe(1)) {
        static auto com_init_end = safetyhook::create_mid(exe(0x42E860), [](SafetyHookContext& ctx) {

            localization::LoadNameMap_names();

            });
        //Memory::VP::InterceptCall(exe(0x42E8F6), Com_Init_Try_Block_Function_addr, Com_Init_Try_Block_Function_stub);
    }

    auto hunk = exe(0x426AB1 + 1);
    if (hunk) {
        Memory::VP::Patch<uint32_t>(hunk, 512);
    }

    static const char* version = "CoD2-YAP r" BUILD_NUMBER_STR;
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

