#include <helper.hpp>
#include "component_loader.h"
#include <Hooking.Patterns.h>

#include <filesystem>
#include <fstream>
#include "nlohmann/json.hpp"

#include <optional>
#include <buildnumber.h>
#include "framework.h"

#include <MemoryMgr.h>

#include "dvars.h"
#include "hooking.h"
#include "GMath.h"

#define SAVE_DEBUG_PRINT(format, ...) \
    do { \
if(!dvars::developer){ \
dvars::developer = dvars::Dvar_FindVar("developer"); \
} \
        if (dvars::developer && dvars::developer->value.integer >= 2) { \
            printf(format, ##__VA_ARGS__); \
        } \
    } while(0)

namespace saves {
    dvar_s* yap_save_remove_levelshots_name;

    dvar_s* yap_save_show_savemenu;

    char levelshot[256]{};
    uintptr_t va_screenshotJPEG_og;
    const char* va_screenshotJPEG(const char* format, const char* path) {
        dvar_s* com_playerProfile = dvars::Dvar_FindVar("com_playerProfile");

        // Check if levelshot has characters in it
        if (levelshot[0] != '\0') {
            // Remove "levelshots/" prefix if it exists
            const char* actual_path = levelshot;
            const char* prefix = "levelshots/";
            size_t prefix_len = strlen(prefix);

            if (yap_save_remove_levelshots_name ->value.integer && strncmp(levelshot, prefix, prefix_len) == 0) {
                actual_path = levelshot + prefix_len; // Skip the "levelshots/" part
            }

            // Build new path: players/com_PlayerProfile/save/actual_path
            char custom_path[512];
            sprintf_s(custom_path, sizeof(custom_path), "players/%s/save/%s",
                com_playerProfile->value.string, actual_path);

            // Clear levelshot array
            levelshot[0] = '\0';

            // Call with custom path
            const char* result = cdecl_call<const char*>(va_screenshotJPEG_og, format, custom_path);
            SAVE_DEBUG_PRINT("results %s\n", result);
            return result;
        }

        // Check if path ends with .svg
        size_t len = strlen(path);
        if (len >= 4 && strcmp(path + len - 4, ".svg") == 0) {
            char* new_path = new char[len - 3];
            strncpy_s(new_path, len - 3, path, len - 4);
            new_path[len - 4] = '\0';
            const char* result = cdecl_call<const char*>(va_screenshotJPEG_og, format, new_path);
            delete[] new_path;
            SAVE_DEBUG_PRINT("results %s\n", result);
            return result;
        }

        // If no .svg extension, pass path as-is
        auto results = cdecl_call<const char*>(va_screenshotJPEG_og, format, path);
        SAVE_DEBUG_PRINT("results %s\n", results);
        return results;
    }

    const char* GetMapnameFromSVG(const char* svg_path) {
        static char mapname[64];
        mapname[0] = '\0';

        SAVE_DEBUG_PRINT("[GetMapnameFromSVG] Trying to read from: %s\n", svg_path);

        dvar_s* fs_basepath = dvars::Dvar_FindVar("fs_basepath");
        dvar_s* fs_game = dvars::Dvar_FindVar("fs_game");

        if (!fs_basepath || !fs_basepath->value.string) {
            SAVE_DEBUG_PRINT("[GetMapnameFromSVG] fs_basepath not found\n");
            return nullptr;
        }

        char full_path[1024];
        FILE* file = nullptr;

        // Try fs_game folder first if it exists
        if (fs_game && fs_game->value.string && fs_game->value.string[0] != '\0') {
            sprintf_s(full_path, sizeof(full_path), "%s/%s/%s",
                fs_basepath->value.string, fs_game->value.string, svg_path);
            SAVE_DEBUG_PRINT("[GetMapnameFromSVG] Trying basegame path: %s\n", full_path);
            fopen_s(&file, full_path, "rb");
        }

        // Try main folder if not found
        if (!file) {
            sprintf_s(full_path, sizeof(full_path), "%s/main/%s",
                fs_basepath->value.string, svg_path);
            SAVE_DEBUG_PRINT("[GetMapnameFromSVG] Trying main path: %s\n", full_path);
            fopen_s(&file, full_path, "rb");
        }

        if (file) {
            SAVE_DEBUG_PRINT("[GetMapnameFromSVG] File opened successfully\n");
            // Seek to offset 0x20 and read mapname
            fseek(file, 0x20, SEEK_SET);
            fread(mapname, 1, sizeof(mapname) - 1, file);
            mapname[sizeof(mapname) - 1] = '\0';

            // Find null terminator or end of string
            for (int i = 0; i < sizeof(mapname); i++) {
                if (mapname[i] == '\0' || mapname[i] < 32 || mapname[i] > 126) {
                    mapname[i] = '\0';
                    break;
                }
            }

            fclose(file);
            SAVE_DEBUG_PRINT("[GetMapnameFromSVG] Read mapname: %s\n", mapname);
            return mapname;
        }

        SAVE_DEBUG_PRINT("[GetMapnameFromSVG] Could not open file\n");
        return nullptr;
    }

    bool FS_FileExists(const char* filename) {
        SAVE_DEBUG_PRINT("[FS_FileExists] Checking: %s\n", filename);

        dvar_s* fs_basepath = dvars::Dvar_FindVar("fs_basepath");
        dvar_s* fs_game = dvars::Dvar_FindVar("fs_game");

        if (!fs_basepath || !fs_basepath->value.string) {
            SAVE_DEBUG_PRINT("[FS_FileExists] fs_basepath not found\n");
            return false;
        }

        char full_path[1024];

        // Check in fs_game folder first if it exists
        if (fs_game && fs_game->value.string && fs_game->value.string[0] != '\0') {
            sprintf_s(full_path, sizeof(full_path), "%s/%s/%s",
                fs_basepath->value.string, fs_game->value.string, filename);
            SAVE_DEBUG_PRINT("[FS_FileExists] Checking basegame: %s\n", full_path);

            FILE* file = nullptr;
            if (fopen_s(&file, full_path, "rb") == 0 && file) {
                fclose(file);
                SAVE_DEBUG_PRINT("[FS_FileExists] Found in basegame\n");
                return true;
            }
        }

        // Check in main folder
        sprintf_s(full_path, sizeof(full_path), "%s/main/%s",
            fs_basepath->value.string, filename);
        SAVE_DEBUG_PRINT("[FS_FileExists] Checking main: %s\n", full_path);

        FILE* file = nullptr;
        if (fopen_s(&file, full_path, "rb") == 0 && file) {
            fclose(file);
            SAVE_DEBUG_PRINT("[FS_FileExists] Found in main\n");
            return true;
        }

        SAVE_DEBUG_PRINT("[FS_FileExists] Not found\n");
        return false;
    }

    const char* FindSVGInSaveFolders(const char* svg_name, const char* preferred_profile) {
        static char found_path[512];
        found_path[0] = '\0';

        SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Looking for: %s (preferred profile: %s)\n",
            svg_name, preferred_profile ? preferred_profile : "none");

        dvar_s* fs_basepath = dvars::Dvar_FindVar("fs_basepath");
        dvar_s* fs_game = dvars::Dvar_FindVar("fs_game");

        if (!fs_basepath || !fs_basepath->value.string) {
            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] fs_basepath not found\n");
            return nullptr;
        }

        char players_path[1024];
        char full_svg_path[1024];

        // Use fs_game if set, otherwise use main
        if (fs_game && fs_game->value.string && fs_game->value.string[0] != '\0') {
            sprintf_s(players_path, sizeof(players_path), "%s/%s/players",
                fs_basepath->value.string, fs_game->value.string);
            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Using fs_game: %s\n", fs_game->value.string);
        }
        else {
            sprintf_s(players_path, sizeof(players_path), "%s/main/players",
                fs_basepath->value.string);
            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Using main folder\n");
        }

        SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Searching in: %s\n", players_path);

        auto CheckProfileFolder = [&](const char* profile_name) -> bool {
            sprintf_s(full_svg_path, sizeof(full_svg_path), "%s/%s/save/%s.svg",
                players_path, profile_name, svg_name);

            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Checking: %s\n", full_svg_path);

            FILE* file = nullptr;
            if (fopen_s(&file, full_svg_path, "rb") == 0 && file) {
                fclose(file);
                SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Found SVG at: %s\n", full_svg_path);
                sprintf_s(found_path, sizeof(found_path), "players/%s/save/%s.svg",
                    profile_name, svg_name);
                return true;
            }

            char save_path[1024];
            sprintf_s(save_path, sizeof(save_path), "%s/%s/save",
                players_path, profile_name);

            char subfolder_pattern[1024];
            sprintf_s(subfolder_pattern, sizeof(subfolder_pattern), "%s/*", save_path);

            WIN32_FIND_DATAA subfolder_data;
            HANDLE hSubFind = FindFirstFileA(subfolder_pattern, &subfolder_data);

            if (hSubFind != INVALID_HANDLE_VALUE) {
                do {
                    if (strcmp(subfolder_data.cFileName, ".") == 0 || strcmp(subfolder_data.cFileName, "..") == 0) {
                        continue;
                    }

                    if (subfolder_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        sprintf_s(full_svg_path, sizeof(full_svg_path), "%s/%s/%s.svg",
                            save_path, subfolder_data.cFileName, svg_name);

                        SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Checking subfolder: %s\n", full_svg_path);

                        if (fopen_s(&file, full_svg_path, "rb") == 0 && file) {
                            fclose(file);
                            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Found SVG at: %s\n", full_svg_path);
                            sprintf_s(found_path, sizeof(found_path), "players/%s/save/%s/%s.svg",
                                profile_name, subfolder_data.cFileName, svg_name);
                            FindClose(hSubFind);
                            return true;
                        }
                    }
                } while (FindNextFileA(hSubFind, &subfolder_data));

                FindClose(hSubFind);
            }

            return false;
            };

        if (preferred_profile && preferred_profile[0] != '\0') {
            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Checking preferred profile first: %s\n", preferred_profile);
            if (CheckProfileFolder(preferred_profile)) {
                return found_path;
            }
        }

        WIN32_FIND_DATAA find_data;
        char search_pattern[1024];
        sprintf_s(search_pattern, sizeof(search_pattern), "%s/*", players_path);

        HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
        if (hFind == INVALID_HANDLE_VALUE) {
            SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] Could not open players directory\n");
            return nullptr;
        }

        do {
            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }

            if (preferred_profile && strcmp(find_data.cFileName, preferred_profile) == 0) {
                continue;
            }

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (CheckProfileFolder(find_data.cFileName)) {
                    FindClose(hFind);
                    return found_path;
                }
            }
        } while (FindNextFileA(hFind, &find_data));

        FindClose(hFind);
        SAVE_DEBUG_PRINT("[FindSVGInSaveFolders] SVG not found in any save folder\n");
        return nullptr;
    }

    uint32_t CL_RegisterRawImageNoMip_saveimage(const char* path, int imageTrack, int unk1) {
        SAVE_DEBUG_PRINT("[CL_RegisterRawImageNoMip_saveimage] Called with path: %s, imageTrack: %d, unk1: %d\n",
            path ? path : "NULL", imageTrack, unk1);

        dvar_s* ui_savegame = dvars::Dvar_FindVar("ui_savegame");
        dvar_s* com_playerProfile = dvars::Dvar_FindVar("com_playerProfile");

        SAVE_DEBUG_PRINT("[DEBUG] ui_savegame: %s, com_playerProfile: %s\n",
            (ui_savegame && ui_savegame->value.string) ? ui_savegame->value.string : "NULL",
            (com_playerProfile && com_playerProfile->value.string) ? com_playerProfile->value.string : "NULL");

        char custom_path[512];
        char fallback_path[512];
        const char* mapname = nullptr;

        if (!path || path[0] == '\0') {
            SAVE_DEBUG_PRINT("[DEBUG] Path is null or empty\n");

            if (ui_savegame && ui_savegame->value.string && com_playerProfile && com_playerProfile->value.string) {
                sprintf_s(custom_path, sizeof(custom_path), "players/%s/save/%s.jpg",
                    com_playerProfile->value.string, ui_savegame->value.string);
                SAVE_DEBUG_PRINT("[DEBUG] Generated custom_path: %s\n", custom_path);

                if (FS_FileExists(custom_path)) {
                    SAVE_DEBUG_PRINT("[DEBUG] Custom JPG exists, using it\n");
                    return cdecl_call<uint32_t>(*(uintptr_t*)0x617AC4, custom_path, imageTrack, unk1);
                }

                SAVE_DEBUG_PRINT("[DEBUG] Custom JPG doesn't exist, searching for SVG\n");
            }
        }
        else {
            SAVE_DEBUG_PRINT("[DEBUG] Path exists: %s\n", path);

            const char* actual_path = path;
            const char* prefix = "levelshots/";
            size_t prefix_len = strlen(prefix);

            if (yap_save_remove_levelshots_name->value.integer && strncmp(path, prefix, prefix_len) == 0) {
                actual_path = path + prefix_len;
                SAVE_DEBUG_PRINT("[DEBUG] Removed 'levelshots/' prefix, actual_path: %s\n", actual_path);
            }
            else {
                SAVE_DEBUG_PRINT("[DEBUG] Not removing prefix\n");
            }

            if (com_playerProfile && com_playerProfile->value.string) {
                sprintf_s(custom_path, sizeof(custom_path), "players/%s/save/%s.jpg",
                    com_playerProfile->value.string, actual_path);
                SAVE_DEBUG_PRINT("[DEBUG] Generated custom_path from existing path: %s\n", custom_path);

                if (FS_FileExists(custom_path)) {
                    SAVE_DEBUG_PRINT("[DEBUG] Custom JPG exists, using it\n");
                    return cdecl_call<uint32_t>(*(uintptr_t*)0x617AC4, custom_path, imageTrack, unk1);
                }

                SAVE_DEBUG_PRINT("[DEBUG] Custom JPG doesn't exist, searching for SVG\n");
            }
            else {
                SAVE_DEBUG_PRINT("[DEBUG] com_playerProfile is null or empty\n");
            }
        }

        if (ui_savegame && ui_savegame->value.string) {
            const char* current_profile = (com_playerProfile && com_playerProfile->value.string) ?
                com_playerProfile->value.string : nullptr;
            const char* svg_path = FindSVGInSaveFolders(ui_savegame->value.string, current_profile);
            if (svg_path) {
                mapname = GetMapnameFromSVG(svg_path);
            }
        }

        if (mapname && mapname[0] != '\0') {
            sprintf_s(fallback_path, sizeof(fallback_path), "loadscreen_%s", mapname);
            SAVE_DEBUG_PRINT("[DEBUG] Using loadscreen fallback: %s (from mapname: %s)\n", fallback_path, mapname);
            return cdecl_call<uint32_t>(*(uintptr_t*)0x617AC0, fallback_path, imageTrack, unk1);
        }
        else {
            SAVE_DEBUG_PRINT("[DEBUG] No mapname found from SVG\n");
        }

        SAVE_DEBUG_PRINT("[DEBUG] Using final fallback with original path: %s\n", path ? path : "NULL");
        return cdecl_call<uint32_t>(*(uintptr_t*)0x617AC4, path, imageTrack, unk1);
    }

	class component final : public component_interface
	{
	public:
		void post_unpack() override {
            yap_save_remove_levelshots_name = dvars::Dvar_RegisterInt("yap_save_remove_levelshots_name", 1, 0, 1, DVAR_ARCHIVE);
            yap_save_show_savemenu = dvars::Dvar_RegisterInt("yap_save_show_savemenu", 1, 0, 1, DVAR_ARCHIVE, "Shows the save menu in the main menu");
		}

		void post_start() override {
			Memory::Nop(exe(0x4E0517), 2); // Allow for original screenshotJPEG savegame save/%s to work.
			Memory::VP::Patch(exe(0x4E051A + 1), "screenshotJPEG savegame %s"); // no need for save/ anymore as they pass in the correct path now!
            Memory::VP::InterceptCall(exe(0x4E051F), va_screenshotJPEG_og, va_screenshotJPEG); // they pass in a .svg at the end which isn't ideal for screenshots, you'd end up with .svg.jpg, so we'll remove it.

            static auto SV_AddPendingSave = safetyhook::create_mid(exe(0x44A140), [](SafetyHookContext& ctx) {

                const char** imagename = (const char**)(ctx.esp + 0x8);

                if (imagename && *imagename) {
                    strcpy_s(levelshot, sizeof(levelshot), *imagename);
                }

                });
            auto ptr = exe(0x004C3C83);
            Memory::VP::Nop(ptr, 6);
            Memory::VP::InjectHook(ptr, CL_RegisterRawImageNoMip_saveimage, Memory::VP::HookType::Call);

            static auto Item_Parse = safetyhook::create_mid(exe(0x4D115C), [](SafetyHookContext& ctx) {

                

                itemDef_s* item = *(itemDef_s**)(ctx.esp + 0x428);
                component_loader::after_item_parse(item);
                });
                

		}
        void after_item_parse(itemDef_s* this_item) override {
            auto item = this_item;
            if (item && item->window.name && !strcmp(item->window.name, "loadgame")) {
                printf("menu %s\n", item->window.name);

                if (item->dvar) {
                    printf("dvar %s\n", item->dvar);
                    //game::Z_Free(item->dvar);
                }

                if (item->dvarTest) {
                    game::Z_Free(item->dvarTest);
                    item->dvarTest = game::String_Alloc("yap_save_show_savemenu");
                    printf("dvarTest %s\n", item->dvarTest);

                    //item->dvarTest = NULL;

                }

                if (item->enableDvar) {
                    printf("enableDvar %s 0x%X\n", item->enableDvar, item->dvarFlags);
                    //game::Z_Free(item->enableDvar);
                    //item->enableDvar = NULL;
                }
            }
        }


	};

}
REGISTER_COMPONENT(saves::component);