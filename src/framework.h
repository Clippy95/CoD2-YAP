#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#define LIBRARY "DDRAW.dll"

#define LIBRARYW TEXT(LIBRARY)
// cdecl
template<typename Ret = void, typename... Args>
inline Ret cdecl_call(uintptr_t addr, Args... args) {
    return reinterpret_cast<Ret(__cdecl*)(Args...)>(addr)(args...);
}

// stdcall
template<typename Ret = void, typename... Args>
inline Ret stdcall_call(uintptr_t addr, Args... args) {
    return reinterpret_cast<Ret(__stdcall*)(Args...)>(addr)(args...);
}

// fastcall
template<typename Ret = void, typename... Args>
inline Ret fastcall_call(uintptr_t addr, Args... args) {
    return reinterpret_cast<Ret(__fastcall*)(Args...)>(addr)(args...);
}

// thiscall
template<typename Ret = void, typename... Args>
inline Ret thiscall_call(uintptr_t addr, Args... args) {
    return reinterpret_cast<Ret(__thiscall*)(Args...)>(addr)(args...);
}
#define BASE_GFX 0x10000000
uintptr_t gfx(uintptr_t sp, uintptr_t mp = 0);

#define MOD_NAME "CoD2-YAP"