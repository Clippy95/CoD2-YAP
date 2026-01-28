#pragma once
#include <helper.hpp>
#include "game.h"
#include "GMath.h"
enum dvarFlags_e : uint16_t
{
    DVAR_NOFLAG = 0x0,
    DVAR_ARCHIVE = 0x1,     // save to config_mp.cfg
    DVAR_USERINFO = 0x2,    // cvars used in connection string
    DVAR_SERVERINFO = 0x4,  // cvars used to describe the server settings to clients when clients ask
    DVAR_SYSTEMINFO = 0x8,  // cvars that are synchronized to all clients
    DVAR_NOWRITE = 0x10,    // write protected, but can be changed by server
    DVAR_LATCH = 0x20,
    DVAR_ROM = 0x40,
    DVAR_CHEAT = 0x80,
    DVAR_CONFIG = 0x100, // same on client and server side, stored in configstrings
    DVAR_SAVED = 0x200,
    DVAR_SCRIPTINFO = 0x400, // same as DVAR_SERVERINFO, but set from script
    DVAR_CHANGEABLE_RESET = 0x1000, // allow changing min and max values when cvar is re-registered
    DVAR_RENDERER = 0x2000,
    DVAR_EXTERNAL = 0x4000,
    DVAR_AUTOEXEC = 0x8000
};

enum dvarType_e : uint8_t
{
    DVAR_TYPE_BOOL = 0x0,
    DVAR_TYPE_FLOAT = 0x1,
    DVAR_TYPE_VEC2 = 0x2,
    DVAR_TYPE_VEC3 = 0x3,
    DVAR_TYPE_VEC4 = 0x4,
    DVAR_TYPE_INT = 0x5,
    DVAR_TYPE_ENUM = 0x6,
    DVAR_TYPE_STRING = 0x7,
    DVAR_TYPE_COLOR = 0x8
};

union DvarLimits
{
    struct
    {
        int stringCount;
        const char** strings;
    } enumeration;
    struct
    {
        int min;
        int max;
    } integer;
    struct
    {
        float min;
        float max;
    } decimal;
};

typedef struct
{
    union
    {
        bool boolean;
        int integer;
        float decimal;
        vector2* vec2;
        vector3* vec3;
        float* vec4;
        const char* string;
        unsigned char color[4];
    };
} dvarValue_t;
static_assert(sizeof(dvarValue_t) == 0x4, "sizeof(dvarValue_t)");

typedef struct dvar_s
{
    char const* name;
    enum dvarFlags_e flags;
    enum dvarType_e type;
    bool modified;
    dvarValue_t value;
    dvarValue_t latchedValue;
    dvarValue_t defaultValue;
    union DvarLimits limits;
    struct dvar_s* next;
    struct dvar_s* hashNext;
} dvar_t;
enum DvarSetSource : __int32
{
    DVAR_SOURCE_INTERNAL = 0x0,
    DVAR_SOURCE_EXTERNAL = 0x1,
    DVAR_SOURCE_SCRIPT = 0x2,
    DVAR_SOURCE_DEVGUI = 0x3,
};

namespace dvars {



    inline std::unordered_map<std::string, std::string> descriptions_runtime;
    WEAK game::symbol<dvar_s* (const char*, int, int, int, uint16_t)> Dvar_RegisterInt_Original{ 0x431FC0,0x4380A0 };
    WEAK game::symbol<dvar_s* (const char*, bool, uint16_t)> Dvar_RegisterBool_Original{ 0x431F60,0x4380A0 };
    WEAK game::symbol<dvar_s* (const char*, float, float, float, uint16_t)> Dvar_RegisterFloat_Original{ 0x432020,0x00438100 };
    WEAK game::symbol<dvar_s* (const char*, float, float, float, float, uint16_t)> Dvar_RegisterVec2_Original{ 0x4320A0,0x438180 };
    WEAK game::symbol<dvar_s* (const char* name,
        float x,
        float y,
        float z,
        float w,
        float min,
        float max,
        uint16_t flags)> Dvar_RegisterVec4_Original{ 0x4321C0,0x4382A0 };
    WEAK game::symbol<dvar_s* (const char*)> Dvar_FindVar{ 0x431260,0x4373A0 };

    WEAK game::symbol<void (dvar_s* dvar, int value)> Dvar_SetInt{ 0x432B10,0x00438bf0 };
    WEAK game::symbol<void(dvar_s* dvar, int value)> Dvar_SetBool{ 0x432AB0,0x00438b90 };

    inline dvar_s* developer;

    inline dvar_s* Dvar_RegisterInt(const char* name, int value, int min, int max, uint16_t flags, const char* description = nullptr) {
        if (description && description[0] != '\0') {
            descriptions_runtime[name] = description;
        }

        return Dvar_RegisterInt_Original(name, value, min, max, flags);
    }

    inline dvar_s* Dvar_RegisterBool(const char* name, bool value, uint16_t flags, const char* description = nullptr) {
        if (description && description[0] != '\0') {
            descriptions_runtime[name] = description;
        }

        return Dvar_RegisterBool_Original(name, value, flags);
    }

    inline dvar_s* Dvar_RegisterFloat(const char* name, float value, float min, float max, uint16_t flags, const char* description = nullptr) {
        if (description && description[0] != '\0') {
            descriptions_runtime[name] = description;
        }
        return Dvar_RegisterFloat_Original(name, value, min, max, flags);
    }

    inline dvar_s* Dvar_RegisterVec2(const char* name, float x, float y, float min, float max, uint16_t flags, const char* description = nullptr) {
        if (description && description[0] != '\0') {
            descriptions_runtime[name] = description;
        }
        return Dvar_RegisterVec2_Original(name, x, y, min, max, flags);
    }

    inline dvar_s* __cdecl Dvar_RegisterVec4(
        const char* name,
        float x,
        float y,
        float z,
        float w,
        float min,
        float max,
        uint16_t flags,
        const char* description = nullptr) {
        if (description && description[0] != '\0') {
            descriptions_runtime[name] = description;
        }
        return Dvar_RegisterVec4_Original(name, x, y, z,w,min,max,flags);
    }

    // doesn't really return the dvar, don't use the return.

}