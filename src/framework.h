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


union itemDefData_t
{
	uint32_t listBox;
	uint32_t editField;
	uint32_t multi;
	uint32_t enumDvarName;
	uint32_t data;
};


struct rectDef_s
{
	float x;
	float y;
	float w;
	float h;
	uint32_t horzAlign;
	uint32_t vertAlign;
};

struct windowDef_t
{
	rectDef_s rect[4];
	rectDef_s rectClient[4];
	const char* name;
/*	char rect[96];
	char rectClient[96]*/;
	const char* group;
	const char* cinematicName;
	const char* cinematic;
	uint32_t style;
	uint32_t border;
	uint32_t ownerDraw;
	uint32_t ownerDrawFlags;
	uint32_t borderSize;
	uint32_t staticFlags;
	char dynamicFlags[16];
	char rectEffects0[96];
	char rectEffects1[96];
	char offsetTime[16];
	uint32_t nextTime;
	char foreColor[16];
	char backColor[16];
	char borderColor[16];
	char outlineColor[16];
	uint32_t background;
};

struct menuDef_t;

struct itemDef_s
{
	//char window[528];
	windowDef_t window;
	//char textRect[96];
	rectDef_s textRect[4];
	uint32_t type;
	uint32_t dataType;
	uint32_t alignment;
	uint32_t fontEnum;
	uint32_t textalignment;
	uint32_t textalignx;
	uint32_t textaligny;
	uint32_t textscale;
	uint32_t textStyle;
	const char* text;
	uint32_t textSavegameInfo;
	menuDef_t *parent;
	const char* mouseEnterText;
	const char* mouseExitText;
	const char* mouseEnter;
	const char* mouseExit;
	const char* action;
	const char* onAccept;
	const char* onFocus;
	const char* leaveFocus;
	const char* dvar;
	const char* dvarTest;
	const char* onKey;
	const char* enableDvar;
	uint32_t dvarFlags;
	uint32_t focusSound;
	uint32_t special;
	char cursorPos[16];
	itemDefData_t typeData;
	uint32_t imageTrack;
};

struct menuDef_t
{
	windowDef_t window;
	int32_t font;
	int32_t fullScreen;
	uint32_t itemCount;
	uint32_t fontIndex;
	uint8_t cursorItem[16];
	int32_t fadeCycle;
	int32_t fadeClamp;
	int32_t fadeAmount;
	int32_t fadeInAmount;
	int32_t blurRadius;
	const char* onOpen;
	const char* onClose;
	const char* onESC;
	const char* onKey;
	char soundName[4];
	int32_t imageTrack;
	vec4_t focusColor;
	vec4_t disableColor;
	itemDef_s** items;
};