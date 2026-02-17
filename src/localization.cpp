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
#include "..\hooking.h"
#include "game.h"
#include "dvars.h"
namespace localization {

	SafetyHookInline _vsnprintf_game;

	std::unordered_map<std::string, std::string> g_nameMap;

	std::string UnescapeHex(const std::string& s) {
		std::string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size(); ) {
			if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] == 'x') {
				char hex[3] = { s[i + 2], s[i + 3], 0 };
				out += (char)strtol(hex, nullptr, 16);
				i += 4;
			}
			else {
				out += s[i++];
			}
		}
		return out;
	}

	void LoadNameMap(const char* path) {
		std::ifstream file(path);
		if (!file.is_open()) {
			printf("[NameMap] Failed to open: %s\n", path);
			return;
		}
		g_nameMap.clear();
		std::string line;
		while (std::getline(file, line)) {
			if (line.empty()) continue;

			auto sep = line.find('=');
			if (sep == std::string::npos) continue;

			std::string key = line.substr(0, sep);
			std::string value = line.substr(sep + 1);

			if (!key.empty() && key.back() == '\r') key.pop_back();
			if (!value.empty() && value.back() == '\r') value.pop_back();

			g_nameMap[key] = UnescapeHex(value);
		}

		printf("[NameMap] Loaded %d entries from %s\n", g_nameMap.size(), path);
	}



	int __cdecl _vsnprintf_hook(char* const Buffer, const size_t BufferCount, const char* const Format, va_list ArgList) {
		auto result = _vsnprintf_game.unsafe_ccall<int>(Buffer, BufferCount, Format, ArgList);

		// Name map substitution
		if (!g_nameMap.empty()) {
			std::string buf(Buffer);
			bool modified = false;

			for (auto& [key, value] : g_nameMap) {
				size_t pos = buf.find(key);
				while (pos != std::string::npos) {
					buf.replace(pos, key.size(), value);
					modified = true;
					pos = buf.find(key, pos + value.size()); // avoid infinite loop on overlapping
				}
			}

			if (modified) {
				// Write back — respect BufferCount, ensure null terminator
				strncpy_s(Buffer, BufferCount, buf.c_str(), _TRUNCATE);
			}
		}

		return result;
	}

	// has to be called externally, for some reason one of the FS functions corrupts vector pointer (esi)??????
	__declspec(noinline) void LoadNameMap_names() {
		const char* gamedir = *(const char**)0x1CBAD00;
		auto fs_basepath = dvars::Dvar_FindVar("fs_basepath")->value.string;
		auto fs_game = dvars::Dvar_FindVar("fs_game")->value.string;

		auto loadFromGamedir = [&](const char* folder) {
			int max_files = 0;
			auto files = game::FS_ListFilteredFiles(gamedir, "fonts", NULL, NULL, &max_files);
			game::FS_SortFileList(files, max_files);

			for (int i = 0; i < max_files; i++) {
				if (!files[i]) continue;

				std::string_view name(files[i]);
				if (!name.ends_with("/")) continue; // only folders

				// strip trailing slash to get language name e.g. "english"
				std::string lang(name.substr(0, name.size() - 1));

				std::string path = std::string(fs_basepath) + "\\" + folder + "\\raw\\" + lang + "\\cod2Map.txt";
				printf("[NameMap] trying: %s\n", path.c_str());
				LoadNameMap(path.c_str());
			}

			game::FS_FreeFileList(files);
			};

		// main first (lower priority)
		loadFromGamedir("main");

		// fs_game on top (higher priority)
		if (fs_game && fs_game[0] != '\0') {
			loadFromGamedir(fs_game);
		}
	}
    class component final : public component_interface
    {
    public:
        void post_unpack() override {
            if (!exe(1))
                return;

			game::Cmd_AddCommand("reload_cod2Map", LoadNameMap_names);
			_vsnprintf_game = safetyhook::create_inline(0x553CFA, _vsnprintf_hook);

        }
		void post_game_init() override {
			//LoadNameMap_names();
		}
        void post_start() override {



        }

    };

}
REGISTER_COMPONENT(localization::component);
