#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#define LIBRARY "DDRAW.dll"
#include "GMath.h"
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
uintptr_t exe(uintptr_t sp);

#ifndef COD2_DEFINITIONS_H
#define COD2_DEFINITIONS_H

#define qboolean int
#define qtrue 1
#define qfalse 0

typedef unsigned char byte;
typedef float vec_t;
typedef vector2 vec2_t;
typedef vector3 vec3_t;
typedef vec_t vec4_t[4];
typedef vec_t vec5_t[5];

#define PITCH               0       // up / down
#define YAW                 1       // left / right
#define ROLL                2       // fall over

#endif

#define MOD_NAME "CoD2-YAP"

#define ADDR(addr_win, addr_linux) (addr_win)
// Choose between Windows (W) or Linux (L) code
#define WL(win, linux) win