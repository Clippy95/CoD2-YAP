#pragma once
#include <helper.hpp>
#include "game.h"
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
        float* vec2;
        float* vec3;
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

namespace dvars {

    WEAK game::symbol<dvar_s*(const char* name, int value, int min, int max, uint16_t flags)> Dvar_RegisterInt{ 0x431FC0 };

}