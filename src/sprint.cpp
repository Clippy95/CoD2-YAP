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
#include "cod2_player.h"
#include <menudef.h>

#define CMD_SPRINT (1 << 31)
#define PMF_SPRINTING CMD_SPRINT

#define MENU_DEBUG_PRINT(format, ...) \
    do { \
if(!dvars::developer){ \
dvars::developer = dvars::Dvar_FindVar("developer"); \
} \
        if (dvars::developer && dvars::developer->value.integer >= 2) { \
            printf(format, ##__VA_ARGS__); \
        } \
    } while(0)

struct kbutton_t
{
	uint64_t down;
	uint32_t downtime;
	int32_t msec;
	int8_t active;
	int8_t wasPressed;
};

__declspec(naked) void retptr() {
	__asm {
		ret
	}
}

namespace sprint {

	struct state {
		uint32_t lastSprintTime;
		// modern CODs do command time check, but UO/COD3 have a normalized float from 0.f->1.f
		float SprintFaituge;
		bool preventFatigueRegen; // Flag to prevent regen when player is trying to sprint while exhausted
		int lastCommandTime;
		int lastUpdateTime;
		bool wasSprinting;
	};

	state g_sprintState = { 0, 1.0f, false, 0, 0, false };


	dvar_s* yap_sprint_fatigue_min_threshold;
	dvar_s* yap_sprint_fatigue_drain_rate;
	dvar_s* yap_sprint_fatigue_regen_rate;

	dvar_s* yap_sprint_fatigue_drainonmove;

	dvar_s* yap_sprint_fatigue_regen_delay;

	dvar_s* yap_sprint_gun_rot_p;
	dvar_s* yap_sprint_gun_rot_r;
	dvar_s* yap_sprint_gun_rot_y;

	dvar_s* yap_sprint_gun_mov_f;
	dvar_s* yap_sprint_gun_mov_r;
	dvar_s* yap_sprint_gun_mov_u;

	dvar_s* yap_sprint_gun_ofs_f;
	dvar_s* yap_sprint_gun_ofs_r;
	dvar_s* yap_sprint_gun_ofs_u;

	dvar_s* yap_sprint_bind_holdbreath;

	dvar_s* yap_sprint_gun_bob_horz;

	dvar_s* yap_sprint_gun_bob_vert;

	dvar_s* yap_sprint_weaponBobAmplitudeSprinting;

	dvar_s* yap_sprint_bobAmplitudeSprinting;

	dvar_s* yap_sprintCameraBob;

	dvar_s* yap_sprint_allow_start_reload;

	dvar_s* yap_sprint_enable;

	dvar_s* yap_sprint_internal_yet;

	dvar_s* yap_player_sprintSpeedScale;

	dvar_s* yap_player_sprintStrafeSpeedScale;

	dvar_s* yap_sprint_reload_cancel;

	// Fake dvars for eWeapon values
	dvar_s yap_sprint_gun_rot_p_fake;
	dvar_s yap_sprint_gun_rot_r_fake;
	dvar_s yap_sprint_gun_rot_y_fake;
	dvar_s yap_sprint_gun_mov_f_fake;
	dvar_s yap_sprint_gun_mov_r_fake;
	dvar_s yap_sprint_gun_mov_u_fake;
	dvar_s yap_sprint_gun_ofs_f_fake;
	dvar_s yap_sprint_gun_ofs_r_fake;
	dvar_s yap_sprint_gun_ofs_u_fake;
	dvar_s yap_sprint_gun_bob_horz_fake;
	dvar_s yap_sprint_gun_bob_vert_fake;

	// Pointers to dvars (these will point to either real or fake)
	dvar_s* yap_sprint_gun_rot_p_ptr;
	dvar_s* yap_sprint_gun_rot_r_ptr;
	dvar_s* yap_sprint_gun_rot_y_ptr;
	dvar_s* yap_sprint_gun_mov_f_ptr;
	dvar_s* yap_sprint_gun_mov_r_ptr;
	dvar_s* yap_sprint_gun_mov_u_ptr;
	dvar_s* yap_sprint_gun_ofs_f_ptr;
	dvar_s* yap_sprint_gun_ofs_r_ptr;
	dvar_s* yap_sprint_gun_ofs_u_ptr;
	dvar_s* yap_sprint_gun_bob_horz_ptr;
	dvar_s* yap_sprint_gun_bob_vert_ptr;


	dvar_s* yap_sprint_trying;

	dvar_s* yap_sprint_is_sprinting;

	dvar_s* yay_sprint_gun_always_read_real;
	dvar_s* yay_sprint_mode;

	dvar_s* yay_sprint_display_icon;

	dvar_s* timers_yap_sprint_int;

	static int lastRealTime = 0;
	namespace sprint_timers {
		// Timer indices in the first vec4
		enum TimerIndex {
			LAST_REAL_TIME = 0,
			LAST_SPRINT_TIME = 1,
			LAST_COMMAND_TIME = 2,
			LAST_UPDATE_TIME = 3
		};

		// State indices in the second vec4
		enum StateIndex {
			SPRINT_FATIGUE = 0,
			PREVENT_FATIGUE_REGEN = 1,
			WAS_SPRINTING = 2,
			UNUSED = 3
		};

		dvar_s* timers_yap_sprint_state;

		void init_state_dvar() {
			if (!timers_yap_sprint_state) {
				timers_yap_sprint_state = dvars::Dvar_RegisterVec4("timers_yap_sprint_state", 1.0f, 0, 0, 0, -FLT_MAX, FLT_MAX, DVAR_SCRIPTINFO | DVAR_NOWRITE, "DO NOT TOUCH THIS!!");
			}
		}

		void save_timers() {
			if (!timers_yap_sprint_int || !timers_yap_sprint_state) return;

			// Save timers
			timers_yap_sprint_int->value.vec4[LAST_REAL_TIME] = *(float*)&lastRealTime;
			timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME] = *(float*)&g_sprintState.lastSprintTime;
			timers_yap_sprint_int->value.vec4[LAST_COMMAND_TIME] = *(float*)&g_sprintState.lastCommandTime;
			timers_yap_sprint_int->value.vec4[LAST_UPDATE_TIME] = *(float*)&g_sprintState.lastUpdateTime;

			timers_yap_sprint_int->latchedValue.vec4[LAST_REAL_TIME] = timers_yap_sprint_int->value.vec4[LAST_REAL_TIME];
			timers_yap_sprint_int->latchedValue.vec4[LAST_SPRINT_TIME] = timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME];
			timers_yap_sprint_int->latchedValue.vec4[LAST_COMMAND_TIME] = timers_yap_sprint_int->value.vec4[LAST_COMMAND_TIME];
			timers_yap_sprint_int->latchedValue.vec4[LAST_UPDATE_TIME] = timers_yap_sprint_int->value.vec4[LAST_UPDATE_TIME];

			// Save state
			timers_yap_sprint_state->value.vec4[SPRINT_FATIGUE] = g_sprintState.SprintFaituge;
			timers_yap_sprint_state->value.vec4[PREVENT_FATIGUE_REGEN] = g_sprintState.preventFatigueRegen ? 1.0f : 0.0f;
			timers_yap_sprint_state->value.vec4[WAS_SPRINTING] = g_sprintState.wasSprinting ? 1.0f : 0.0f;

			timers_yap_sprint_state->latchedValue.vec4[SPRINT_FATIGUE] = timers_yap_sprint_state->value.vec4[SPRINT_FATIGUE];
			timers_yap_sprint_state->latchedValue.vec4[PREVENT_FATIGUE_REGEN] = timers_yap_sprint_state->value.vec4[PREVENT_FATIGUE_REGEN];
			timers_yap_sprint_state->latchedValue.vec4[WAS_SPRINTING] = timers_yap_sprint_state->value.vec4[WAS_SPRINTING];
		}

		void load_timers() {
			if (!timers_yap_sprint_int || !timers_yap_sprint_state) return;

			// Load timers
			lastRealTime = *(int*)&timers_yap_sprint_int->value.vec4[LAST_REAL_TIME];
			g_sprintState.lastSprintTime = *(uint32_t*)&timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME];
			g_sprintState.lastCommandTime = *(int*)&timers_yap_sprint_int->value.vec4[LAST_COMMAND_TIME];
			g_sprintState.lastUpdateTime = *(int*)&timers_yap_sprint_int->value.vec4[LAST_UPDATE_TIME];

			// Load state
			g_sprintState.SprintFaituge = timers_yap_sprint_state->value.vec4[SPRINT_FATIGUE];
			g_sprintState.preventFatigueRegen = timers_yap_sprint_state->value.vec4[PREVENT_FATIGUE_REGEN] != 0.0f;
			g_sprintState.wasSprinting = timers_yap_sprint_state->value.vec4[WAS_SPRINTING] != 0.0f;
		}

		int& get_last_real_time() {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				return *(int*)&timers_yap_sprint_int->value.vec4[LAST_REAL_TIME];
			}
			return lastRealTime;
		}

		void set_last_real_time(int value) {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				timers_yap_sprint_int->value.vec4[LAST_REAL_TIME] = *(float*)&value;
				timers_yap_sprint_int->latchedValue.vec4[LAST_REAL_TIME] = timers_yap_sprint_int->value.vec4[LAST_REAL_TIME];
			}
			else {
				lastRealTime = value;
			}
		}

		uint32_t& get_last_sprint_time() {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				return *(uint32_t*)&timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME];
			}
			return g_sprintState.lastSprintTime;
		}

		void set_last_sprint_time(uint32_t value) {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME] = *(float*)&value;
				timers_yap_sprint_int->latchedValue.vec4[LAST_SPRINT_TIME] = timers_yap_sprint_int->value.vec4[LAST_SPRINT_TIME];
			}
			else {
				g_sprintState.lastSprintTime = value;
			}
		}

		float get_sprint_fatigue() {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				return timers_yap_sprint_state->value.vec4[SPRINT_FATIGUE];
			}
			return g_sprintState.SprintFaituge;
		}

		void set_sprint_fatigue(float value) {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				timers_yap_sprint_state->value.vec4[SPRINT_FATIGUE] = value;
				timers_yap_sprint_state->latchedValue.vec4[SPRINT_FATIGUE] = value;
			}
			else {
				g_sprintState.SprintFaituge = value;
			}
		}

		bool get_prevent_fatigue_regen() {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				return timers_yap_sprint_state->value.vec4[PREVENT_FATIGUE_REGEN] != 0.0f;
			}
			return g_sprintState.preventFatigueRegen;
		}

		void set_prevent_fatigue_regen(bool value) {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				float fval = value ? 1.0f : 0.0f;
				timers_yap_sprint_state->value.vec4[PREVENT_FATIGUE_REGEN] = fval;
				timers_yap_sprint_state->latchedValue.vec4[PREVENT_FATIGUE_REGEN] = fval;
			}
			else {
				g_sprintState.preventFatigueRegen = value;
			}
		}

		bool get_was_sprinting() {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				return timers_yap_sprint_state->value.vec4[WAS_SPRINTING] != 0.0f;
			}
			return g_sprintState.wasSprinting;
		}

		void set_was_sprinting(bool value) {
			if (yap_sprint_enable && yap_sprint_enable->value.integer == 1) {
				float fval = value ? 1.0f : 0.0f;
				timers_yap_sprint_state->value.vec4[WAS_SPRINTING] = fval;
				timers_yap_sprint_state->latchedValue.vec4[WAS_SPRINTING] = fval;
			}
			else {
				g_sprintState.wasSprinting = value;
			}
		}
	}

	void set_sprinting(bool sprinting) {
		yap_sprint_is_sprinting->value.integer = sprinting ? 1 : 0;
		yap_sprint_is_sprinting->latchedValue.integer = sprinting ? 1 : 0;
		yap_sprint_is_sprinting->modified = true;

		if (dvars::developer && dvars::developer->value.integer) {
			printf("Sprint state changed to: %d\n", sprinting);
		}
	}


	bool can_sprint() {
		return sprint_timers::get_sprint_fatigue() >= yap_sprint_fatigue_min_threshold->value.decimal;
	}

	struct eWeaponDef {
		std::string weaponName;
		float vSprintBob[2];
		float sprintSpeedScale;
		float vSprintRot[3];
		float vSprintMove[3];
		float vSprintOfs[3];

		eWeaponDef() :
			weaponName(""),
			vSprintBob{ 0.0f, 0.0f },
			sprintSpeedScale{ 1.0f },
			vSprintRot{ 0.0f, 0.0f, 0.0f },
			vSprintMove{ 0.0f, 0.0f, 0.0f },
			vSprintOfs{ 0.0f, 0.0f, 0.0f } {
		}
	};

	std::unordered_map<std::string, eWeaponDef> g_eWeaponDefs;

	std::array<eWeaponDef*, 128> g_eWeaponDefsByIndex;


	void ClearEWeaponIndexCache() {
		g_eWeaponDefsByIndex.fill(nullptr);
	}

	dvar_s* yap_eweapon_semi_match;
	dvar_s* yap_eweapon_load_mode;

	const eWeaponDef* GetEWeapon(const char* weaponName) {
		if (!weaponName || g_eWeaponDefs.empty()) return nullptr;

		// Semi-match with contains (case-sensitive)
		if (yap_eweapon_semi_match && yap_eweapon_semi_match->value.integer) {
			std::string searchName = weaponName;

			for (const auto& [key, value] : g_eWeaponDefs) {
				// Check if either the key contains searchName or searchName contains key
				if (key.find(searchName) != std::string::npos ||
					searchName.find(key) != std::string::npos) {
					return &value;
				}
			}
		}
		else {
			// Exact match (case-sensitive)
			auto it = g_eWeaponDefs.find(weaponName);
			if (it != g_eWeaponDefs.end()) {
				return &it->second;
			}
		}

		return nullptr;
	}

	uintptr_t GetCurrentWeapon() {
		uint32_t index = 0;
		uint32_t* unk1 = (uint32_t*)0xF708F4;

		if ((*unk1 & 0x20000) != 0) {
			index = cdecl_call<uint32_t>(0x4DC660);
		}
		else {
			index = *(uint32_t*)0xF70994;

			if ((*unk1 & 0x10) == 0) {
				index = *(uint32_t*)0xF70998;
			}

		}

		uintptr_t* ptrs = (uintptr_t*)0x01C64548;

		return ptrs[index];

	}

	const char* GetCurrentWeaponName() {
		if (GetCurrentWeapon()) {
			if(dvars::developer && dvars::developer->value.integer)
			printf("name %s\n", *(const char**)GetCurrentWeapon());
			return *(const char**)(GetCurrentWeapon());
		}


		return NULL;
	}

	uint32_t GetCurrentWeaponIndex() {
		uint32_t index = 0;
		uint32_t* unk1 = (uint32_t*)0xF708F4;

		if ((*unk1 & 0x20000) != 0) {
			index = cdecl_call<uint32_t>(0x4DC660);
		}
		else {
			index = *(uint32_t*)0xF70994;
			if ((*unk1 & 0x10) == 0) {
				index = *(uint32_t*)0xF70998;
			}
		}
		return index;
	}

	const eWeaponDef* GetEWeaponByIndex(uint32_t index) {
		if (index >= 128) return nullptr;
		return g_eWeaponDefsByIndex[index];
	}

	const eWeaponDef* GetCurrentEWeapon() {

		if(yap_eweapon_load_mode->value.boolean)
		return GetEWeaponByIndex(GetCurrentWeaponIndex());

		return GetEWeapon(GetCurrentWeaponName());
	}

	// Shared function to parse JSON into eWeaponDef
	bool ParseEWeaponJSON(const std::filesystem::path& filepath, const std::string& weaponName, eWeaponDef& weaponDef) {
		try {
			std::ifstream file(filepath);
			nlohmann::json j = nlohmann::json::parse(file);

			weaponDef.weaponName = weaponName;

			if (j.contains("sprintBobH")) {
				weaponDef.vSprintBob[0] = j["sprintBobH"].get<float>();
			}
			if (j.contains("sprintBobV")) {
				weaponDef.vSprintBob[1] = j["sprintBobV"].get<float>();
			}
			if (j.contains("sprintSpeedScale")) {
				weaponDef.sprintSpeedScale = j["sprintSpeedScale"].get<float>();
			}
			if (j.contains("SprintRot") && j["SprintRot"].is_array() && j["SprintRot"].size() == 3) {
				weaponDef.vSprintRot[0] = j["SprintRot"][0].get<float>();
				weaponDef.vSprintRot[1] = j["SprintRot"][1].get<float>();
				weaponDef.vSprintRot[2] = j["SprintRot"][2].get<float>();
			}
			if (j.contains("SprintMove") && j["SprintMove"].is_array() && j["SprintMove"].size() == 3) {
				weaponDef.vSprintMove[0] = j["SprintMove"][0].get<float>();
				weaponDef.vSprintMove[1] = j["SprintMove"][1].get<float>();
				weaponDef.vSprintMove[2] = j["SprintMove"][2].get<float>();
			}
			if (j.contains("SprintOfs") && j["SprintOfs"].is_array() && j["SprintOfs"].size() == 3) {
				weaponDef.vSprintOfs[0] = j["SprintOfs"][0].get<float>();
				weaponDef.vSprintOfs[1] = j["SprintOfs"][1].get<float>();
				weaponDef.vSprintOfs[2] = j["SprintOfs"][2].get<float>();
			}

			return true;
		}
		catch (const std::exception& e) {
			Com_Printf("Failed to parse %s: %s\n", filepath.string().c_str(), e.what());
			return false;
		}
	}

	void LoadEWeaponsFromDirectory(const std::filesystem::path& eWeaponsDir, bool overwrite = true) {
		if (!std::filesystem::exists(eWeaponsDir)) {
			Com_Printf("eWeapons directory not found: %s\n", eWeaponsDir.string().c_str());
			return;
		}
		Com_Printf("Loading eWeapons from: %s\n", eWeaponsDir.string().c_str());

		for (const auto& entry : std::filesystem::directory_iterator(eWeaponsDir)) {
			if (entry.path().extension() != ".json") continue;

			std::string weaponName = entry.path().stem().string();
			std::transform(weaponName.begin(), weaponName.end(), weaponName.begin(),
				[](unsigned char c) { return std::tolower(c); });

			if (!overwrite && g_eWeaponDefs.find(weaponName) != g_eWeaponDefs.end()) {
				Com_Printf("Skipping '%s' (already loaded with higher priority)\n", weaponName.c_str());
				continue;
			}

			eWeaponDef weaponDef;
			if (ParseEWeaponJSON(entry.path(), weaponName, weaponDef)) {
				g_eWeaponDefs[weaponName] = weaponDef;
				Com_Printf("Loaded eWeapon '%s' - sprintBobH: %.3f, sprintBobV: %.3f, sprintSpeedScale: %.3f\n",
					weaponName.c_str(),
					weaponDef.vSprintBob[0],
					weaponDef.vSprintBob[1],
					weaponDef.sprintSpeedScale);
			}
		}
	}

	void LoadSingleEWeapon(const std::filesystem::path& filepath, const std::string& weaponName) {
		eWeaponDef weaponDef;
		if (ParseEWeaponJSON(filepath, weaponName, weaponDef)) {
			g_eWeaponDefs[weaponName] = weaponDef;
			Com_Printf("Loaded eWeapon '%s' from %s\n", weaponName.c_str(), filepath.string().c_str());
		}
	}

	void LoadEWeaponsOnDemand() {
		// Check if game has loaded weapons yet
		int maxWeapons = *(int*)exe(0x1CBAD9C);
		if (maxWeapons == 0) {
			Com_Printf("Game not loaded yet, cannot load eWeapons on-demand\n");
			return;
		}

		g_eWeaponDefs.clear();
		ClearEWeaponIndexCache();

		char modulePath[MAX_PATH];
		GetModuleFileNameA(NULL, modulePath, MAX_PATH);
		std::filesystem::path exePath(modulePath);
		std::filesystem::path baseDir = exePath.parent_path();
		auto* fs_game = dvars::Dvar_FindVar("fs_game");
		auto* fs_basegame = dvars::Dvar_FindVar("fs_basegame");

		uintptr_t* ptrs = (uintptr_t*)exe(0x01C64548);

		// Only load eWeapons for weapons currently in the game
		for (int i = 0; i <= maxWeapons ; i++) {
			if (!ptrs[i]) continue;

			const char* weaponName = *(const char**)ptrs[i];
			if (!weaponName) continue;

			std::string lowerName = weaponName;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
				[](unsigned char c) { return std::tolower(c); });

			// Try to load this specific weapon's JSON
			std::filesystem::path weaponJsonName = lowerName + ".json";

			// Search in priority order: fs_game > fs_basegame > base
			bool loaded = false;

			if (fs_game && fs_game->value.string && fs_game->value.string[0] != '\0') {
				std::filesystem::path fsGamePath = baseDir / fs_game->value.string / "eWeapons" / weaponJsonName;
				if (std::filesystem::exists(fsGamePath)) {
					LoadSingleEWeapon(fsGamePath, lowerName);
					loaded = true;
				}
			}

			if (!loaded && fs_basegame && fs_basegame->value.string && fs_basegame->value.string[0] != '\0') {
				std::filesystem::path fsBasePath = baseDir / fs_basegame->value.string / "eWeapons" / weaponJsonName;
				if (std::filesystem::exists(fsBasePath)) {
					LoadSingleEWeapon(fsBasePath, lowerName);
					loaded = true;
				}
			}

			if (!loaded) {
				std::filesystem::path basePath = baseDir / "eWeapons" / weaponJsonName;
				if (std::filesystem::exists(basePath)) {
					LoadSingleEWeapon(basePath, lowerName);
					loaded = true;
				}
			}

			if (loaded) {
				g_eWeaponDefsByIndex[i] = &g_eWeaponDefs[lowerName];
			}
		}

		Com_Printf("Loaded %d eWeapon definitions (on-demand mode)\n", g_eWeaponDefs.size());
	}


	void loadEWeapons() {
		// Mode 0: Load all from directories (old method)
		// Mode 1: Load only weapons in current session (new method)
		if (yap_eweapon_load_mode && yap_eweapon_load_mode->value.boolean) {
			LoadEWeaponsOnDemand();
			return;
		}

		// Old method: load everything from directories
		g_eWeaponDefs.clear();
		char modulePath[MAX_PATH];
		GetModuleFileNameA(NULL, modulePath, MAX_PATH);
		std::filesystem::path exePath(modulePath);
		std::filesystem::path baseDir = exePath.parent_path();
		auto* fs_game = dvars::Dvar_FindVar("fs_game");
		auto* fs_basegame = dvars::Dvar_FindVar("fs_basegame");

		std::filesystem::path baseEWeaponsDir = baseDir / "eWeapons";
		LoadEWeaponsFromDirectory(baseEWeaponsDir, true);

		if (fs_basegame && fs_basegame->value.string && fs_basegame->value.string[0] != '\0') {
			std::filesystem::path fsBaseGameDir = baseDir / fs_basegame->value.string;
			std::filesystem::path fsBaseGameEWeaponsDir = fsBaseGameDir / "eWeapons";
			LoadEWeaponsFromDirectory(fsBaseGameEWeaponsDir, true);
		}

		if (fs_game && fs_game->value.string && fs_game->value.string[0] != '\0') {
			std::filesystem::path fsGameDir = baseDir / fs_game->value.string;
			std::filesystem::path fsGameEWeaponsDir = fsGameDir / "eWeapons";
			LoadEWeaponsFromDirectory(fsGameEWeaponsDir, true);
			Com_Printf("Loaded %d eWeapon definitions (fs_game: '%s' has priority)\n",
				g_eWeaponDefs.size(),
				fs_game->value.string);
		}
		else {
			Com_Printf("Loaded %d eWeapon definitions from base directory\n",
				g_eWeaponDefs.size());
		}
	}

	void ESICall(void* arg1,uintptr_t addr) {
		__asm {
			pushad
			mov esi,arg1
			call addr
			popad
		}
	}

	void IN_KeyUp(kbutton_t* key) {
		ESICall(key, 0x408BD0);
	}

	void IN_KeyDown(kbutton_t* key) {
		ESICall(key, 0x408B50);
	}
	kbutton_t sprint;
	void IN_SprintDown() {
		if (dvars::developer && dvars::developer->value.integer)
		printf("sprint down %p", &sprint);
		IN_KeyDown(&sprint);
	}

	void IN_SprintUp() {
		IN_KeyUp(&sprint);
	}


	void IN_BreathSprint_Down() {
		kbutton_t* holdbreath = (kbutton_t*)0x005D20D4;
		IN_KeyDown(holdbreath);
		IN_SprintDown();
	}

	void IN_BreathSprint_Up() {
		kbutton_t* holdbreath = (kbutton_t*)0x005D20D4;
		IN_KeyUp(holdbreath);
		IN_SprintUp();
	}

	void IN_HoldBreath_Down() {
		kbutton_t* holdbreath = (kbutton_t*)0x005D20D4;
		IN_KeyDown(holdbreath);
		if (yap_sprint_bind_holdbreath && yap_sprint_bind_holdbreath->value.integer)
		IN_SprintDown();
	}

	void IN_HoldBreath_Up() {
		kbutton_t* holdbreath = (kbutton_t*)0x005D20D4;
		IN_KeyUp(holdbreath);
		if(yap_sprint_bind_holdbreath && yap_sprint_bind_holdbreath->value.integer)
		IN_SprintUp();
	}

	uintptr_t setup_binds_og;
#define s_SPRINT_AND_BREATH "sprintbreath"
#define s_SPRINT "sprint"

	const char* custom_bindings[] = { "+" s_SPRINT_AND_BREATH , "+" s_SPRINT };
	uintptr_t I_stricmp_addr = 0x43F6D0;
	int I_stricmp(int idk, const char* main_string, const char* changing) {
		int result;
		__asm {
			mov eax, idk
			mov edx, main_string
			mov ecx, changing
			call I_stricmp_addr
			mov result,eax
		}
		return result;
	}

	int I_stricmp_new_custom_bindings(int idk, const char* main_string, const char* changing) {

		__asm {
			mov idk, eax
			mov main_string, edx
			mov changing, ecx
		}

		bool isLastEntry = false;
		int result = I_stricmp(0x7FFFFFFF, main_string, changing);
		if (result && main_string == (const char*)0x5A51F8 || changing == (const char*)0x5A51F8) {
			isLastEntry = true;
		}

		if (isLastEntry && result) {

			for (int i = 0; i < ARRAYSIZE(custom_bindings); i++) {
				if (I_stricmp(0x7FFFFFFF, main_string, custom_bindings[i]) == 0 ||
					I_stricmp(0x7FFFFFFF, changing, custom_bindings[i]) == 0) {
					result = 0;
					break;
				}
			}
		}

		return result;
	}

	int setup_binds() {

		game::Cmd_AddCommand("+" s_SPRINT, IN_SprintDown);
		game::Cmd_AddCommand("-" s_SPRINT, IN_SprintUp);

		game::Cmd_AddCommand("+" s_SPRINT_AND_BREATH, IN_BreathSprint_Down);
		game::Cmd_AddCommand("-" s_SPRINT_AND_BREATH, IN_BreathSprint_Up);

		return cdecl_call<int>(setup_binds_og);
	}
	uintptr_t GetKeyBindingLocal_addr = 0x4CDBF0;
	int GetKeyBindingLocalizedString(const char* command_name, char (*string)[128]) {
		int result;
		__asm {
			pushad
			mov ecx, command_name
			mov esi, string
			call GetKeyBindingLocal_addr
			mov result,eax
			popad
		}

		return result;

	}

	int GetKeyBindingLocalizedString_meleebreath_stub(const char* command_name, char (*string)[128]) {
		int ecx_og;
		int esi_og;
		__asm {
			mov ecx_og, ecx
			mov esi_og, esi
		}
		auto result = GetKeyBindingLocalizedString((const char*)ecx_og, (char (*)[128])esi_og);

		if (!result)
			 result = GetKeyBindingLocalizedString("+" s_SPRINT_AND_BREATH, (char (*)[128])esi_og);

		return result;

	}

	SAFETYHOOK_NOINLINE void update_sprint_gun_dvars() {
		if (yay_sprint_gun_always_read_real->value.integer == 0 && GetCurrentEWeapon()) {
			auto eweapon = GetCurrentEWeapon();

			// Set fake dvar values from eWeapon
			yap_sprint_gun_rot_p_fake.value.decimal = eweapon->vSprintRot[0];
			yap_sprint_gun_rot_r_fake.value.decimal = eweapon->vSprintRot[2];
			yap_sprint_gun_rot_y_fake.value.decimal = eweapon->vSprintRot[1];
			yap_sprint_gun_mov_f_fake.value.decimal = eweapon->vSprintMove[0];
			yap_sprint_gun_mov_r_fake.value.decimal = eweapon->vSprintMove[1];
			yap_sprint_gun_mov_u_fake.value.decimal = eweapon->vSprintMove[2];
			//yap_sprint_gun_ofs_f_fake.value.decimal = eweapon->vSprintMove[0];
			//yap_sprint_gun_ofs_r_fake.value.decimal = eweapon->vSprintMove[1];
			//yap_sprint_gun_ofs_u_fake.value.decimal = eweapon->vSprintMove[2];
			yap_sprint_gun_bob_horz_fake.value.decimal = eweapon->vSprintBob[0];
			yap_sprint_gun_bob_vert_fake.value.decimal = eweapon->vSprintBob[1];

			// Point to fakes
			yap_sprint_gun_rot_p_ptr = &yap_sprint_gun_rot_p_fake;
			yap_sprint_gun_rot_r_ptr = &yap_sprint_gun_rot_r_fake;
			yap_sprint_gun_rot_y_ptr = &yap_sprint_gun_rot_y_fake;
			yap_sprint_gun_mov_f_ptr = &yap_sprint_gun_mov_f_fake;
			yap_sprint_gun_mov_r_ptr = &yap_sprint_gun_mov_r_fake;
			yap_sprint_gun_mov_u_ptr = &yap_sprint_gun_mov_u_fake;
			yap_sprint_gun_ofs_f_ptr = &yap_sprint_gun_ofs_f_fake;
			yap_sprint_gun_ofs_r_ptr = &yap_sprint_gun_ofs_r_fake;
			yap_sprint_gun_ofs_u_ptr = &yap_sprint_gun_ofs_u_fake;
			yap_sprint_gun_bob_horz_ptr = &yap_sprint_gun_bob_horz_fake;
			yap_sprint_gun_bob_vert_ptr = &yap_sprint_gun_bob_vert_fake;
		}
		else {
			// Point to real dvars
			yap_sprint_gun_rot_p_ptr = yap_sprint_gun_rot_p;
			yap_sprint_gun_rot_r_ptr = yap_sprint_gun_rot_r;
			yap_sprint_gun_rot_y_ptr = yap_sprint_gun_rot_y;
			yap_sprint_gun_mov_f_ptr = yap_sprint_gun_mov_f;
			yap_sprint_gun_mov_r_ptr = yap_sprint_gun_mov_r;
			yap_sprint_gun_mov_u_ptr = yap_sprint_gun_mov_u;
			yap_sprint_gun_ofs_f_ptr = yap_sprint_gun_ofs_f;
			yap_sprint_gun_ofs_r_ptr = yap_sprint_gun_ofs_r;
			yap_sprint_gun_ofs_u_ptr = yap_sprint_gun_ofs_u;
			yap_sprint_gun_bob_horz_ptr = yap_sprint_gun_bob_horz;
			yap_sprint_gun_bob_vert_ptr = yap_sprint_gun_bob_vert;
		}
	}

	void yap_activate_sprint() {
		yap_sprint_trying->value.integer = 1;
		yap_sprint_trying->latchedValue.integer = 1;
		yap_sprint_trying->modified = true;
	}

	void yap_deactivate_sprint() {
		yap_sprint_trying->value.integer = 0;
		yap_sprint_trying->latchedValue.integer = 0;
		yap_sprint_trying->modified = true;
	}

	bool yap_is_sprinting() {
		return yap_sprint_is_sprinting->value.integer != 0;
	}




	bool yap_is_trying_sprinting() {
		return yap_sprint_trying->value.integer != 0;
	}



	vector3 yap_sprint_gun_mov() {
		return vector3{
			yap_sprint_gun_mov_f_ptr->value.decimal,
			yap_sprint_gun_mov_r_ptr->value.decimal,
			yap_sprint_gun_mov_u_ptr->value.decimal
		};
	}

	vector3 yap_sprint_gun_rot() {
		return vector3{
			yap_sprint_gun_rot_p_ptr->value.decimal,
			yap_sprint_gun_rot_r_ptr->value.decimal,
			yap_sprint_gun_rot_y_ptr->value.decimal
		};
	}

	vector3 yap_sprint_gun_ofs() {
		return vector3{
			yap_sprint_gun_ofs_f_ptr->value.decimal,
			yap_sprint_gun_ofs_r_ptr->value.decimal,
			yap_sprint_gun_ofs_u_ptr->value.decimal
		};
	}

	vector2 yap_sprint_gun_bob() {
		return vector2{
			yap_sprint_gun_bob_horz_ptr->value.decimal,
			yap_sprint_gun_bob_vert_ptr->value.decimal
		};
	}

	double __cdecl CG_GetWeaponVerticalBobFactor_sprint(float a1, float a2, float a3)
	{
		double v3; // st7
		double v4; // st7
		float v6; // [esp+0h] [ebp-4h]



		if (*(uintptr_t*)exe(0xF709B8) == 11)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeProne")->value.decimal;
		}
		else if (*(uintptr_t*)exe(0xF709B8) == 40)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeDucked")->value.decimal;
		}
		else
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeStanding")->value.decimal;
		}

		if (yap_is_sprinting()) {

					float vert_bob = yap_sprint_weaponBobAmplitudeSprinting->value.vec2->y * yap_sprint_gun_bob().y;
					v3 = vert_bob;
				
			
		}

		v4 = v3 * a2;
		v6 = v4;
		if (v4 > a3)
			v6 = a3;
		return (sin(a1 * 4.0 + 1.5707964) * 0.2 + sin(a1 + a1)) * v6 * 0.75;
	}


	double __cdecl CG_GetVerticalBobFactor_sprint(float a1, float a2, float a3)
	{
		double v3; // st7
		double v4; // st7
		float v6; // [esp+0h] [ebp-4h]



		if (*(uintptr_t*)exe(0xF709B8) == 11)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeProne")->value.decimal;
		}
		else if (*(uintptr_t*)exe(0xF709B8) == 40)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeDucked")->value.decimal;
		}
		else
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeStanding")->value.decimal;
		}

		if (yap_is_sprinting()) {

			float vert_bob = yap_sprint_bobAmplitudeSprinting->value.vec2->y;
			v3 = vert_bob;


		}

		v4 = v3 * a2;
		v6 = v4;
		if (v4 > a3)
			v6 = a3;
		return (sin(a1 * 4.0 + 1.5707964) * 0.2 + sin(a1 + a1)) * v6 * 0.75;
	}

	double __cdecl CG_GetViewGetHorizontalBob_sprint(float a1, float a2, float a3)
	{
		double v3; // st7
		double v4; // st7
		float v6; // [esp+0h] [ebp-4h]



		if (*(uintptr_t*)exe(0xF709B8) == 11)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeProne")->value.decimal;
		}
		else if (*(uintptr_t*)exe(0xF709B8) == 40)
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeDucked")->value.decimal;
		}
		else
		{
			v3 = dvars::Dvar_FindVar("bg_bobAmplitudeStanding")->value.decimal;
		}

		if (yap_is_sprinting()) {

			v3 = yap_sprint_bobAmplitudeSprinting->value.vec2->x;


		}

		v4 = v3 * a2;
		v6 = v4;
		if (v4 > a3)
			v6 = a3;
		return v6 * sin(a1);
	}

	void PM_UpdateSprintingFlag(pmove_t* pm) {
		if (!pm || !pm->ps)
			return;

		if (!yap_sprint_enable || yap_sprint_enable->value.integer == 0) {
			pm->ps->pm_flags &= ~PMF_SPRINTING;
			set_sprinting(false);
			return;
		}
		auto flags = pm->ps->pm_flags;

		bool tryingToMove = yay_sprint_mode->value.integer ? pm->cmd.forwardmove > 0 : (pm->cmd.forwardmove || pm->cmd.rightmove);

		bool wantsToSprint = tryingToMove && (pm->cmd.buttons & CMD_SPRINT);

		float* thing = (float*)pm->ps;
		float adsFraction = thing[46];
		bool isADS = flags & PMF_SIGHT_AIMING;

		bool AllowedToSprint = !isADS && !(flags & PMF_CROUCH) && !(flags & PMF_PRONE) && !(flags & PMF_FRAG) && !(flags & PMF_MANTLE) && !(flags & PMF_LADDER);

		AllowedToSprint = AllowedToSprint && (pm->ps->speed >= (int)((float)dvars::Dvar_FindVar("g_speed")->defaultValue.integer * 0.85f));

		AllowedToSprint = AllowedToSprint && (pm->ps->leanf == 0.f);

		if (wantsToSprint && can_sprint() && AllowedToSprint) {
			pm->ps->pm_flags |= PMF_SPRINTING;
			set_sprinting(true);
			sprint_timers::set_prevent_fatigue_regen(false);
		}
		else {
			pm->ps->pm_flags &= ~PMF_SPRINTING;
			set_sprinting(false);

			if (wantsToSprint && !can_sprint() && AllowedToSprint) {
				sprint_timers::set_prevent_fatigue_regen(true);
			}
			else {
				sprint_timers::set_prevent_fatigue_regen(false);
			}
		}

		if (dvars::developer && dvars::developer->value.integer) {
			printf("pm_flags 0x%X adsFrac: %.3f %d, wantsSprint: %d, canSprint: %d, isADS: %d, preventRegen: %d\n",
				pm->ps->pm_flags, adsFraction, pm->ps->commandTime, wantsToSprint, can_sprint(), isADS, sprint_timers::get_prevent_fatigue_regen());
		}
	}

	// in UO this is only done from g side but in CoD2+ everythings been merged hmmm (well expect for linux server)
	void PM_UpdateFatigue(pmove_t* pm) {
		if (!pm || !pm->ps)
			return;

		if (!yap_sprint_enable || yap_sprint_enable->value.integer == 0) {
			return;
		}

		int lastRealTime_val = sprint_timers::get_last_real_time();
		int realTime = *(int*)0xF708DC;

		if (lastRealTime_val == 0) {
			sprint_timers::set_last_real_time(realTime);
			return;
		}

		if (realTime == lastRealTime_val)
			return;

		int frameDeltaMs = realTime - lastRealTime_val;
		sprint_timers::set_last_real_time(realTime);

		if (frameDeltaMs <= 0 || frameDeltaMs > 1000)
			return;

		float frameTimeSec = frameDeltaMs * 0.001f;
		float threshold = 4.5f;

		vector2& velocity_2d = *(vector2*)&pm->ps->velocity;
		float speedSquared = velocity_2d.length();
		bool is_moving = yap_sprint_fatigue_drainonmove->value.integer ? speedSquared > threshold : true;

		if (yap_is_sprinting()) {
			if (is_moving) {
				sprint_timers::set_last_sprint_time(realTime);

				float currentFatigue = sprint_timers::get_sprint_fatigue();
				float drainAmount = frameTimeSec * yap_sprint_fatigue_drain_rate->value.decimal;
				currentFatigue -= drainAmount;

				if (currentFatigue < 0.0f) {
					currentFatigue = 0.0f;
					set_sprinting(false);
					pm->ps->pm_flags &= ~PMF_SPRINTING;
				}

				sprint_timers::set_sprint_fatigue(currentFatigue);
				sprint_timers::set_prevent_fatigue_regen(false);
			}
		}
		else {
			uint32_t lastSprintTime = sprint_timers::get_last_sprint_time();
			float timeSinceLastSprint = (realTime - lastSprintTime) * 0.001f;

			if (!sprint_timers::get_prevent_fatigue_regen() &&
				timeSinceLastSprint >= yap_sprint_fatigue_regen_delay->value.decimal) {

				float currentFatigue = sprint_timers::get_sprint_fatigue();
				currentFatigue += frameTimeSec * yap_sprint_fatigue_regen_rate->value.decimal;
				if (currentFatigue > 1.0f) {
					currentFatigue = 1.0f;
				}
				sprint_timers::set_sprint_fatigue(currentFatigue);
			}
		}
	}




	SafetyHookInline PM_UpdatePlayerWalkingFlagD;


	SafetyHookInline PM_UpdateAimDownSightFlagD;

	void PM_UpdateAimDownSightFlag(pml_t* pml, pmove_t* pm) {
		auto func = PM_UpdateAimDownSightFlagD.original<void*>();
		__asm {
			pushad
			mov edi, pm
			mov eax, pml
			call func
			popad
		}
	}

	uintptr_t PM_Weapon_Idle_addr = 0x4DF0A0;

	void __cdecl PM_Weapon_Idle(playerState_t* ps) {
		__asm {
			pushad
			mov eax, ps
			call PM_Weapon_Idle_addr
			popad
		}
	}

	void __stdcall PM_UpdateAimDownSightFlag_detour(pml_t* pml, pmove_t* pm) {
		PM_UpdateAimDownSightFlag(pml, pm);
		auto result = PM_UpdatePlayerWalkingFlagD.unsafe_thiscall<int>(pm);

		PM_UpdateSprintingFlag(pm);
		PM_UpdateFatigue(pm);

	}

	void PM_UpdateAimDownSightFlag_stub() {

		__asm
		{
			push edi
			push eax
			call PM_UpdateAimDownSightFlag_detour
		}

	}

	int __fastcall PM_UpdatePlayerWalkingFlag_hook(pmove_t* pm,void* dummy1) {
		return 0;
		


	}
	float t[] = { -1.f,-1.f };
	float s[] = { -1.f,-1.f };
	uintptr_t UI_DrawHandlePic_addr = 0x4D4790;
	void UI_DrawHandlePic_with_t(float x, float y, float w, float h, int verticalAlign, int horizontalAlign,float s0,float t0,float s1,float t1, vec4_t* color, void* shader,bool do_we = false) {
		if (do_we) {
			t[0] = t0;
			t[1] = t1;
			s[0] = s0;
			s[1] = s1;
		}
		else {
			t[0] = -1.f;
			t[1] = -1.f;
			s[0] = -1.f;
			s[1] = -1.f;
		}
		__asm {
			mov ebx, verticalAlign
			mov edi, horizontalAlign
			push shader
			push color
			push h
			push w
			push y
			push x
			call UI_DrawHandlePic_addr
			add esp,0x18
		}
		t[0] = -1.f;
		t[1] = -1.f;
		s[0] = -1.f;
		s[1] = -1.f;
		

	}
constexpr auto GREY_MAYBE = 0.6f;

uintptr_t stance_sprint_shader = 0;

void UI_DrawHandlePic_stub(float x, float y, float w, float h, vec4_t* color, void* shader) {
	int ebx_og;
	int edi_og;
	__asm {
		mov ebx_og, ebx
		mov edi_og, edi
	}

	vec4_t dem_color = { GREY_MAYBE,GREY_MAYBE,GREY_MAYBE,1.f };

	float fatiguePercent = sprint_timers::get_sprint_fatigue();

	if (yay_sprint_display_icon && yay_sprint_display_icon->value.integer && yap_is_sprinting()) {
		shader = (void*)stance_sprint_shader;
	}

	UI_DrawHandlePic_with_t(x, y, w, h, ebx_og, edi_og, 1.f, 1.f, 1.f, 1.f, &dem_color, shader);

	float filledHeight = h * fatiguePercent;
	float emptyHeight = h * (1.0f - fatiguePercent);

	UI_DrawHandlePic_with_t(x, y + emptyHeight, w, filledHeight, ebx_og, edi_og,
		0.f, 1.0f - fatiguePercent, 1.f, 1.f,
		color, shader, true);
}

	void CL_DrawStretchPic_sprint_stub_lazy(float x, float y, float width, float height, float s0, float t0, float s1, float t1, int shader, int color)
	{
		if (s[0] != -1.0f) {
			s0 = s[0];
		}

		if (s[1] != -1.0f) {
			s1 = s[1];
		}

		if (t[0] != -1.0f) {
			t0 = t[0];
		}

		if (t[1] != -1.0f) {
			t1 = t[1];
		}

		cdecl_call<void>(*(uintptr_t*)0x617B5C, x, y, width, height, s0, t0, s1, t1, shader, color);

		// Reset to -1 after call
		s[0] = -1.0f;
		s[1] = -1.0f;
		t[0] = -1.0f;
		t[1] = -1.0f;
	}
	void GetMaxSpeedHack(SafetyHookContext& ctx) {
		pmove_t* pm = (pmove_t*)(ctx.edi);

		float& maxspeed = *(float*)(ctx.esp + 0x10);

		if (pm && pm->ps && pm->ps->pm_flags & PMF_SPRINTING) {
			maxspeed *= yap_player_sprintSpeedScale->value.decimal;

			auto eWeapon = GetCurrentEWeapon();

			if (eWeapon) {
				maxspeed *= eWeapon->sprintSpeedScale;
			}

		}

	}
	uint32_t default_material = -1;
	int CL_RegisterMaterial(const char* material_name, int unk1, int unk2) {
		return cdecl_call<int>(*(uintptr_t*)0x617AC0, material_name, unk1, unk2);

	}
	uintptr_t PM_WalkMove_og;
	int PM_WalkMove(pmove_t* pm, pml_t* pml) {
		if (pm->ps->pm_flags & PMF_SPRINTING) {
			pm->cmd.rightmove = (int)(float)((float)pm->cmd.rightmove * yap_player_sprintStrafeSpeedScale->value.decimal);
		}

		return cdecl_call<int>(PM_WalkMove_og, pm, pml);

	}
	uintptr_t PM_IDK4DE720_og;


	int __cdecl PM_IDK4DE720(pmove_t* pm, int a2) {


		auto ps = pm->ps;


		if (yap_sprint_reload_cancel->value.integer &&
			(pm->ps->pm_flags & PMF_SPRINTING) &&
			pm->ps->weaponstate == 5) {


			pm->ps->weaponstate = 17;
			PM_Weapon_Idle(pm->ps);
			ps->weapAnim = 2;
		}


		auto result = cdecl_call<int>(PM_IDK4DE720_og, pm, a2);

		return result;

	}

	struct binding_s_XRef {
		uintptr_t patch_location;  // Exact address of the 4-byte operand to patch
		size_t offset;             // Offset from new base
	};

	binding_s_XRef binding_s_xrefs[] = {
		{ 0x004CD855, 0x000C },  // mov     esi, offset g_bindings.key1 -> 0x005A4E94
		{ 0x004CD89B, 0x0010 },  // mov     esi, offset g_bindings.key2 -> 0x005A4E98
		{ 0x004CD921, 0x000C },  // mov     eax, offset g_bindings.key1 -> 0x005A4E94
		{ 0x004CD945, 0x0000 },  // mov     esi, offset g_bindings -> 0x005A4E88
		{ 0x004CD991, 0x0000 },  // mov     edi, offset g_bindings -> 0x005A4E88
		{ 0x004CD9F0, 0x000C },  // mov     eax, ss:g_bindings.key1[ebp] -> 0x005A4E94
		{ 0x004CDA24, 0x0010 },  // mov     eax, ss:g_bindings.key2[ebp] -> 0x005A4E98
		{ 0x004CDAE5, 0x0000 },  // mov     esi, offset g_bindings -> 0x005A4E88
		{ 0x004CDB19, 0x000C },  // cmp     g_bindings.key1[eax*4], 0FFFFFFFFh -> 0x005A4E94
		{ 0x004CDB48, 0x0000 },  // mov     esi, offset g_bindings -> 0x005A4E88
		{ 0x004CDB99, 0x000C },  // mov     eax, g_bindings.key1[esi] -> 0x005A4E94
		{ 0x004CDBBB, 0x0010 },  // mov     eax, g_bindings.key2[esi] -> 0x005A4E98
		{ 0x004CE195, 0x0010 },  // mov     eax, offset g_bindings.key2 -> 0x005A4E98
		{ 0x004CE1E4, 0x000C },  // mov     eax, g_bindings.key1[esi] -> 0x005A4E94
		{ 0x004CE1FA, 0x000C },  // mov     g_bindings.key1[esi], edi -> 0x005A4E94
		{ 0x004CE200, 0x0010 },  // mov     eax, g_bindings.key2[esi] -> 0x005A4E98
		{ 0x004CE21C, 0x000C },  // mov     g_bindings.key1[esi], edi -> 0x005A4E94
		{ 0x004CE228, 0x0010 },  // cmp     g_bindings.key2[esi], 0FFFFFFFFh -> 0x005A4E98
		{ 0x004CE231, 0x0010 },  // mov     g_bindings.key2[esi], edi -> 0x005A4E98
		{ 0x004CE243, 0x0010 },  // mov     eax, g_bindings.key2[esi] -> 0x005A4E98
		{ 0x004CE253, 0x000C },  // mov     g_bindings.key1[esi], edi -> 0x005A4E94
		{ 0x004CE259, 0x0010 },  // mov     g_bindings.key2[esi], 0FFFFFFFFh -> 0x005A4E98
	};

	const size_t binding_s_xref_count = sizeof(binding_s_xrefs) / sizeof(binding_s_xrefs[0]);




	void patch_binding_s_references(void* new_base, size_t array_count) {
		// Patch all structure field references
		for (size_t i = 0; i < binding_s_xref_count; i++) {
			void* patch_addr = (void*)binding_s_xrefs[i].patch_location;
			void* new_value = (void*)((uintptr_t)new_base + binding_s_xrefs[i].offset);
			Memory::VP::Patch<void*>(patch_addr, new_value);
			printf("Patched 0x%p -> 0x%p (offset +0x%zX)\n",
				patch_addr, new_value, binding_s_xrefs[i].offset);
		}

		// Calculate end addresses for different field iterations
		void* array_end = (void*)((uintptr_t)new_base + (array_count * sizeof(game::binding_s)));

		// 0x5A520C: End of array (base iteration)
		void* new_end_0x5A520C = array_end;

		// 0x5A5218: End when iterating from key1 offset (+0x0C from array_end)
		void* new_end_0x5A5218 = (void*)((uintptr_t)array_end + 0x0C);

		// 0x5A521C: End when iterating from key2 offset (+0x10 from array_end)
		void* new_end_0x5A521C = (void*)((uintptr_t)array_end + 0x10);

		// Patch all end-of-array comparisons

		// Original end checks (0x5A520C)
		Memory::VP::Patch<void*>((void*)0x004CD968, new_end_0x5A520C);
		Memory::VP::Patch<void*>((void*)0x004CD9BD, new_end_0x5A520C);
		Memory::VP::Patch<void*>((void*)0x004CDB08, new_end_0x5A520C);
		Memory::VP::Patch<void*>((void*)0x004CDB6A, new_end_0x5A520C);

		// Controls_GetConfig - key1 iteration (0x5A5218)
		Memory::VP::Patch<void*>((void*)(0x004CD881 + 2), new_end_0x5A5218);  // cmp esi, offset dword_5A5218
		Memory::VP::Patch<void*>((void*)(0x004CD933 + 1), new_end_0x5A5218);  // cmp eax, offset dword_5A5218

		// sub_4CD890 - key2 iteration (0x5A521C)
		Memory::VP::Patch<void*>((void*)(0x004CD8FC + 2), new_end_0x5A521C);  // cmp esi, offset off_5A521C
		Memory::VP::Patch<void*>((void*)(0x004CE1BD + 1), new_end_0x5A521C);  // cmp eax, offset off_5A521C

		printf("Patched end-of-array references:\n");
		printf("  0x5A520C -> 0x%p (array end)\n", new_end_0x5A520C);
		printf("  0x5A5218 -> 0x%p (key1 iteration end)\n", new_end_0x5A5218);
		printf("  0x5A521C -> 0x%p (key2 iteration end)\n", new_end_0x5A521C);
		printf("  Base: 0x%p, Count: %zu entries\n", new_base, array_count);
	}


	class component final : public component_interface
	{
	public:

		void post_unpack() override {
			if (!exe(1))
				return;
			sprint_timers::init_state_dvar();
			timers_yap_sprint_int = dvars::Dvar_RegisterVec4("timers_yap_sprint_int",0,0,0,0,INT32_MIN, INT32_MAX,DVAR_SCRIPTINFO | DVAR_NOWRITE,"DO NOT TOUCH THIS!!");
			dvars::developer = dvars::Dvar_FindVar("developer");
			yay_sprint_display_icon = dvars::Dvar_RegisterInt("yap_sprint_display_icon", 1, 0, 1, DVAR_ARCHIVE, "Displays the \"stance_sprint\" material on the stance draw when sprinting");
			yap_sprint_fatigue_min_threshold = dvars::Dvar_RegisterFloat("yap_sprint_fatigue_min_threshold", 0.05f, 0.0f, 1.0f, DVAR_ARCHIVE);
			yap_sprint_fatigue_drain_rate = dvars::Dvar_RegisterFloat("yap_sprint_fatigue_drain_rate", (0.6667f) / 2.f, 0.0f, 10.0f, DVAR_ARCHIVE);
			yap_sprint_fatigue_drainonmove = dvars::Dvar_RegisterInt("yap_sprint_fatigue_drainonmove", 1, 0, 1, DVAR_ARCHIVE, "Only drain sprint fatigue when the player is at least moving");
			yap_sprint_fatigue_regen_rate = dvars::Dvar_RegisterFloat("yap_sprint_fatigue_regen_rate", 0.3333f, 0.0f, 10.0f, DVAR_ARCHIVE);
			yap_sprint_fatigue_regen_delay = dvars::Dvar_RegisterFloat("yap_sprint_fatigue_regen_delay", 1.0f, 0.0f, 10.0f, DVAR_ARCHIVE);

			yap_sprint_gun_rot_p = dvars::Dvar_RegisterFloat("yap_sprint_gun_rot_p", 0.f, -FLT_MAX, FLT_MAX,0);
			yap_sprint_gun_rot_r = dvars::Dvar_RegisterFloat("yap_sprint_gun_rot_r", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_rot_y = dvars::Dvar_RegisterFloat("yap_sprint_gun_rot_y", 0.f, -FLT_MAX, FLT_MAX, 0);


			yap_sprint_gun_mov_f = dvars::Dvar_RegisterFloat("yap_sprint_gun_mov_f", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_mov_r = dvars::Dvar_RegisterFloat("yap_sprint_gun_mov_r", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_mov_u = dvars::Dvar_RegisterFloat("yap_sprint_gun_mov_u", 0.f, -FLT_MAX, FLT_MAX, 0);

			yap_sprint_gun_ofs_f = dvars::Dvar_RegisterFloat("yap_sprint_gun_ofs_f", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_ofs_r = dvars::Dvar_RegisterFloat("yap_sprint_gun_ofs_r", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_ofs_u = dvars::Dvar_RegisterFloat("yap_sprint_gun_ofs_u", 0.f, -FLT_MAX, FLT_MAX, 0);


			yap_sprint_gun_bob_horz = dvars::Dvar_RegisterFloat("yap_sprint_gun_bob_horz", 0.f, -FLT_MAX, FLT_MAX, 0);
			yap_sprint_gun_bob_vert = dvars::Dvar_RegisterFloat("yap_sprint_gun_bob_horz", 0.f, -FLT_MAX, FLT_MAX, 0);

			yap_sprint_internal_yet = dvars::Dvar_RegisterInt("yap_sprint_internal_yet", 1, 0, 1, DVAR_ROM);

			yap_sprint_enable = dvars::Dvar_RegisterInt("yap_sprint_enable", 1, 0, 2, DVAR_ARCHIVE, "Enables an engine-native sprint\n"
				"requires binding +sprintbreath or +sprint\n1 = Enabled & the timers for the the sprint are stored in dvars\n2 = Enabled & the timers for the sprint are stored this play-session");

			yap_sprint_weaponBobAmplitudeSprinting = dvars::Dvar_RegisterVec2("yap_sprint_weaponBobAmplitudeSprinting", 0.02f, 0.014f, 0.0f, 1.f, DVAR_ARCHIVE);
			yap_sprint_bobAmplitudeSprinting = dvars::Dvar_RegisterVec2("yap_sprint_bobAmplitudeSprinting", 0.02f, 0.014f, 0.0f, 1.f, DVAR_ARCHIVE);
			yap_sprint_trying = dvars::Dvar_RegisterInt("yap_sprint_trying", 0, 0, 1, DVAR_ROM);
			yap_sprint_is_sprinting = dvars::Dvar_RegisterInt("yap_sprint_is_sprinting", 0, 0, 1, 0);

			yay_sprint_gun_always_read_real = dvars::Dvar_RegisterInt("yap_sprint_gun_always_read_real", 0, 0, 1, 0);

			yay_sprint_mode = dvars::Dvar_RegisterInt("yap_sprint_mode", 0, 0, 1, DVAR_ARCHIVE,"0 = Allow sprinting omnidirectionally like in United Offensive\n1 = Only allow sprinting when at least moving forward like IW3+");

			yap_eweapon_semi_match = dvars::Dvar_RegisterInt("yap_eweapon_semi_match", 0, 0, 1, DVAR_ARCHIVE);

			yap_eweapon_load_mode = dvars::Dvar_RegisterBool("yap_eweapon_load_mode", 1, DVAR_ARCHIVE | DVAR_LATCH,"0 = Loads all weapons names from directory\n1 = Loads all weapons names from current loaded server");

			yap_player_sprintSpeedScale = dvars::Dvar_RegisterFloat("yap_player_sprintSpeedScale", 1.6f, 0.f, FLT_MAX,DVAR_ARCHIVE,"The scale applied to the player speed when sprinting");

			yap_player_sprintStrafeSpeedScale = dvars::Dvar_RegisterFloat("yap_player_sprintStrafeSpeedScale", 0.667, 0.01f, 1.f, DVAR_ARCHIVE, "The speed at which you can strafe while sprinting");

			yap_sprint_reload_cancel = dvars::Dvar_RegisterInt("yap_sprint_reload_cancel", 0, 0, 1, 0, "Cancels reloading when starting to sprint");

			yap_sprint_bind_holdbreath = dvars::Dvar_RegisterInt("yap_sprint_bind_holdbreath", 1, 0, 1, DVAR_ARCHIVE);

			yap_sprintCameraBob = dvars::Dvar_RegisterFloat("yap_sprintCameraBob", 0.f, 0.f, 2.f, DVAR_ARCHIVE, "The speed the camera bobs while you sprint");

			yap_sprint_allow_start_reload = dvars::Dvar_RegisterInt("yap_sprint_allow_start_reload", 0, 0, 1, DVAR_ARCHIVE, "Turning this on allows you to initiate a reload while sprinting.");

			update_sprint_gun_dvars();
			game::Cmd_AddCommand("reload_eweapons", loadEWeapons);
			Memory::VP::Nop(0x4D485C, 6);
			Memory::VP::InjectHook(0x4D485C, CL_DrawStretchPic_sprint_stub_lazy, Memory::VP::HookType::Call);
		}

		void post_cg_init() override {
			if ( !exe(1))
				return;
			loadEWeapons();
		}

		void post_start() override {
			if (!exe(1))
				return;
			uint32_t arraysize = 0;
			auto moddebindings = game::GetModdedBindings(arraysize);
			patch_binding_s_references(moddebindings, arraysize);
			PM_UpdateAimDownSightFlagD = safetyhook::create_inline(exe(0x4DD020), PM_UpdateAimDownSightFlag_stub);

			static auto registering_graphics = safetyhook::create_mid(0x4A2ECE, [](SafetyHookContext& ctx) {
				default_material = CL_RegisterMaterial("$default", 3, 7);
				stance_sprint_shader = CL_RegisterMaterial("stance_sprint", 3, 7);
				if (stance_sprint_shader == default_material)
					stance_sprint_shader = 0;
				printf("SPRINTING IS UHHH %d\n", stance_sprint_shader);


				});

			Memory::VP::Patch<void*>(exe((0x40A1A0 + 1)), IN_HoldBreath_Down);
			Memory::VP::Patch<void*>(exe((0x40A1AF + 1)), IN_HoldBreath_Up);
			Memory::VP::InjectHook(0x4A9EE7, GetKeyBindingLocalizedString_meleebreath_stub);

			static auto CG_CheckPlayerStanceChange = safetyhook::create_mid(0x4BE445, [](SafetyHookContext& ctx) {
				if (!yap_sprint_enable || yap_sprint_enable->value.integer == 0) {
					return;
				}

				bool isSprinting = yap_is_sprinting();

				if (isSprinting != sprint_timers::get_was_sprinting()) {
					ctx.ecx = 0x00002300;

					int* cgTime = (int*)0xF708DC;
					int* lastStanceChangeTime = (int*)0xF796C8;
					*lastStanceChangeTime = *cgTime;

					sprint_timers::set_was_sprinting(isSprinting);
				}

				int* mask = (int*)(ctx.esp + 0x4);

				if (sprint_timers::get_sprint_fatigue() != 1.f)
					*mask = 0x00002300;
				});



			//static auto CG_CheckPlayerStanceChange1 = safetyhook::create_mid(0x4BE445, [](SafetyHookContext& ctx) {



			//	});

			Memory::VP::InterceptCall(exe(0x50CACF), PM_WalkMove_og, PM_WalkMove);

			Memory::VP::InterceptCall(exe(0x4DFC88), PM_IDK4DE720_og, PM_IDK4DE720);

			Memory::VP::InjectHook(0x4A8142, UI_DrawHandlePic_stub);


			//Memory::VP::Nop(0x4A8142, 5);
			//static auto UI_DrawHandlePic_lazy_hack2 = safetyhook::create_mid(0x4A8142, [](SafetyHookContext& ctx) {

			//	float* x = (float*)&ctx.edx;
			//	float* y = (float*)&ctx.ecx;
			//	float* w = (float*)&ctx.eax;
			//	float* h = (float*)(ctx.esp + 0x8);
			//	vec4_t* color = (vec4_t*)(ctx.ebp);
			//	void* shader = (void*)*(int*)(ctx.esp + 0x14);

			//	vec4_t discolor = { 0.f,1.f,1.f,1.f };

			//	UI_DrawHandlePic_with_t(*x, *y, *w, *h, ctx.ebx, ctx.edi, 0, 0, 0, 0, &discolor,shader);


			//	});


			PM_UpdatePlayerWalkingFlagD = safetyhook::create_inline(exe(0x50BD00), PM_UpdatePlayerWalkingFlag_hook);
			Memory::VP::InterceptCall(0x40E164, setup_binds_og, setup_binds);
			Memory::VP::Nop(exe(0x004EC7D0), 2);

			static auto CG_ViewAddWeaponorsmth = safetyhook::create_mid(exe(0x4B5730), [](SafetyHookContext& ctx) {
				
				if (yap_is_sprinting()) {
					update_sprint_gun_dvars();
					Memory::VP::Patch<dvar_s**>(exe((0x4B5847 + 2)), &yap_sprint_gun_rot_p_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B5851 + 1)), &yap_sprint_gun_rot_y_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B585B + 2)), &yap_sprint_gun_rot_r_ptr);
				}
				else {
					Memory::VP::Patch<dvar_s**>(exe((0x4B5847 + 2)), (dvar_s**)0x01040C10);
					Memory::VP::Patch<dvar_s**>(exe((0x4B5851 + 1)), (dvar_s**)0x01040B9C);
					Memory::VP::Patch<dvar_s**>(exe((0x4B585B + 2)), (dvar_s**)0x00F569EC);
				}

				});

			static auto CG_ViewAddWeaponorsmth2 = safetyhook::create_mid(exe(0x4B5380), [](SafetyHookContext& ctx) {

				if (yap_is_sprinting()) {
					update_sprint_gun_dvars();

					Memory::VP::Patch<dvar_s**>(exe((0x4B54AC + 2)), &yap_sprint_gun_mov_f_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54B6 + 1)), &yap_sprint_gun_mov_r_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54C0 + 2)), &yap_sprint_gun_mov_u_ptr);


					Memory::VP::Patch<dvar_s**>(exe((0x4B5511 + 1)), &yap_sprint_gun_ofs_f_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B551C + 2)), &yap_sprint_gun_ofs_r_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B553F + 1)), &yap_sprint_gun_ofs_u_ptr);


					Memory::VP::Patch<dvar_s**>(exe((0x4B5561 + 2)), &yap_sprint_gun_ofs_f_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B556D + 1)), &yap_sprint_gun_ofs_r_ptr);
					Memory::VP::Patch<dvar_s**>(exe((0x4B558F + 2)), &yap_sprint_gun_ofs_u_ptr);
				}
				else {
					Memory::VP::Patch<dvar_s**>(exe((0x4B54AC + 2)), (dvar_s**)0x01040BEC);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54B6 + 1)), (dvar_s**)0x01040BDC);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54C0 + 2)), (dvar_s**)0x01043084);


					Memory::VP::Patch<dvar_s**>(exe((0x4B5511 + 1)), (dvar_s**)0x10430A4);
					Memory::VP::Patch<dvar_s**>(exe((0x4B551C + 2)), (dvar_s**)0x01040BAC);
					Memory::VP::Patch<dvar_s**>(exe((0x4B553F + 1)), (dvar_s**)0xF569B4);


					Memory::VP::Patch<dvar_s**>(exe((0x4B5561 + 2)), (dvar_s**)0x10430A4);
					Memory::VP::Patch<dvar_s**>(exe((0x4B556D + 1)), (dvar_s**)0x01040BAC);
					Memory::VP::Patch<dvar_s**>(exe((0x4B558F + 2)), (dvar_s**)0xF569B4);


				}

				});

			// Helper function to insert an item at a specific position
			auto insertMenuItem = [](menuDef_t* menu, itemDef_s* newItem, int insertIndex, int& keyBindStatusIndex) {
				// Shift all items after insertIndex forward by one
				for (int i = menu->itemCount; i > insertIndex; i--) {
					menu->items[i] = menu->items[i - 1];
				}

				// Insert the new item
				menu->items[insertIndex] = newItem;
				menu->itemCount++;

				// Update keyBindStatusIndex if it was shifted
				if (keyBindStatusIndex >= insertIndex) {
					keyBindStatusIndex++;
				}

				MENU_DEBUG_PRINT("Inserted item at index %d, itemCount now %d, keyBindStatus now at %d\n",
					insertIndex, menu->itemCount, keyBindStatusIndex);
				};

			static auto menu_parse_item = safetyhook::create_mid(exe(0x4D382A), [](SafetyHookContext& ctx) {
				static char abuffer[256];
				menuDef_t* menu = (menuDef_t*)(ctx.ebx);
				component_loader::post_menu_parse(menu);

				});


			Memory::VP::InjectHook(exe(0x004B4B04), CG_GetWeaponVerticalBobFactor_sprint);

			static auto bobsprint_hook = safetyhook::create_inline(0x4AE010, CG_GetViewGetHorizontalBob_sprint);
			static auto vertsprint_hook = safetyhook::create_inline(0x4ADF90, CG_GetVerticalBobFactor_sprint);

			Memory::VP::Nop(exe(0x004B4B4B), 3);

			static auto CG_HorzBob = safetyhook::create_mid(0x004B4B44, [](SafetyHookContext& ctx) {
				float value;
				if (ctx.ecx != 40) {
					if (yap_is_sprinting()) {
						value = yap_sprint_weaponBobAmplitudeSprinting->value.vec2->x * yap_sprint_gun_bob().x;
						FPU::FMUL(value);
						return;
					}

					FPU::FMUL(*(float*)(ctx.ebx + 8));
				}


				});

			static auto CL_UpdateCmdButton = safetyhook::create_mid(exe(0x409AF0), [](SafetyHookContext& ctx) {
				if (!yap_sprint_enable || yap_sprint_enable->value.integer == 0) {
					return;
				}
				uint32_t* buttons = (uint32_t*)(ctx.eax + 0x4);
				if (sprint.active || sprint.wasPressed) {
					yap_activate_sprint();
					sprint.wasPressed = false;
					*buttons |= CMD_SPRINT;


				}
				else
					yap_deactivate_sprint();
				if (dvars::developer && dvars::developer->value.integer)
				printf("is trying to sprint? %d\n", yap_is_trying_sprinting());

				});

			static auto cmdwalk_sprint = safetyhook::create_mid(exe(0x50821F), [](SafetyHookContext& ctx) {

				float& speed = *(float*)(ctx.esp + 0x8);

				if (yap_is_sprinting()) {
					speed *= yap_player_sprintSpeedScale->value.decimal;

					auto eWeapon =  GetCurrentEWeapon();

					if (eWeapon) {
						speed *= eWeapon->sprintSpeedScale;
					}

				}
				if (dvars::developer && dvars::developer->value.integer)
				printf("da speed %f\n", speed);

				});

			static auto GetMaxSpeed_hack1 = safetyhook::create_mid(exe(0x50AB38), GetMaxSpeedHack);
			static auto GetMaxSpeed_hack2 = safetyhook::create_mid(exe(0x50AA87), GetMaxSpeedHack);

			// Usage:
			static auto BobCycleSpeed = safetyhook::create_mid(exe(0x50ABC6), [](SafetyHookContext& ctx) {
				if (yap_is_sprinting() && yap_sprintCameraBob->value.decimal != 0.f) {
					X87Context x87;
					float maxspeed = *(float*)(ctx.esp + 0x10);
					float xyspeed = *(float*)(ctx.edi + 0x104);

					float newBob = xyspeed / maxspeed * yap_sprintCameraBob->value.decimal; static int lastRealTime = 0;

					x87.writeST(1, newBob);


					x87.apply();
				}
				});
			// not ideal and not what UO does, it does from cmwalk but for some reason that messes up bobcycle and makes it go extra speed whyyyyyy;
			//static auto gspeed_sprint = safetyhook::create_mid(exe(0x4E7CA8), [](SafetyHookContext& ctx) {

			//	//gclient_s
			//	playerState_t* ps = (playerState_t*)(ctx.ebp);

			//	if (yap_is_sprinting()) {
			//		float speed = (float)ps->speed;
			//		speed *= yap_player_sprintSpeedScale->value.decimal;
			//		ps->speed = (int)speed;
			//	}

			//	});

			static auto PM_Weapon_skip = safetyhook::create_mid(exe(0x4DFDD5), [](SafetyHookContext& ctx) {
				if (yap_is_sprinting()) {
					ctx.eax = 1;
				}

				});

			static auto PM_BeginWeaponReload_skip = safetyhook::create_mid(exe(0x4DE497), [](SafetyHookContext& ctx) {
				playerState_t* ps = (playerState_t*)(ctx.eax);
				if (yap_sprint_allow_start_reload->value.integer == 0 && ps->pm_flags & PMF_SPRINTING) {
					ctx.eip = (uintptr_t)&retptr;
				}

				});

		}

		void post_menu_parse(menuDef_t* menu) override {
			if (!exe(1))
				return;
			if (menu && menu->window.name && !strcmp(menu->window.name, "options_shoot")) {
				MENU_DEBUG_PRINT("name %s items %d\n", menu->window.name, menu->itemCount);
				int lastButtonIndex = -1;
				int lastBindIndex = -1;
				int keyBindStatusIndex = -1;

				auto insertMenuItem = [&](itemDef_s* newItem, int insertIndex) {
					MENU_DEBUG_PRINT("  [insertMenuItem] Inserting at index %d, current itemCount=%d\n", insertIndex, menu->itemCount);
					if (keyBindStatusIndex != -1) {
						MENU_DEBUG_PRINT("  [insertMenuItem] keyBindStatus BEFORE shift: rect.y=%f (at index %d)\n",
							menu->items[keyBindStatusIndex]->window.rect->y, keyBindStatusIndex);
					}

					for (int i = menu->itemCount; i > insertIndex; i--) {
						menu->items[i] = menu->items[i - 1];
					}

					if (keyBindStatusIndex != -1) {
						MENU_DEBUG_PRINT("  [insertMenuItem] keyBindStatus AFTER shift: rect.y=%f\n",
							menu->items[keyBindStatusIndex]->window.rect->y);
					}

					menu->items[insertIndex] = newItem;
					menu->itemCount++;

					if (keyBindStatusIndex >= insertIndex) {
						keyBindStatusIndex++;
					}

					MENU_DEBUG_PRINT("  [insertMenuItem] menu->items array pointer: %p\n", menu->items);
					MENU_DEBUG_PRINT("  [insertMenuItem] About to write to items[%d]\n", insertIndex);
					MENU_DEBUG_PRINT("  [insertMenuItem] items[%d] address: %p\n", insertIndex, &menu->items[insertIndex]);
					if (keyBindStatusIndex != -1) {
						MENU_DEBUG_PRINT("  [insertMenuItem] keyBindStatus AFTER insert: rect.y=%f (now at index %d)\n",
							menu->items[keyBindStatusIndex]->window.rect->y, keyBindStatusIndex);
					}

					MENU_DEBUG_PRINT("  [insertMenuItem] Done. itemCount now %d\n", menu->itemCount);
					};

				for (int i = 0; i < menu->itemCount; i++) {
					if (menu->items[i]->window.name && !strcmp(menu->items[i]->window.name, "keyBindStatus")) {
						keyBindStatusIndex = i;
						MENU_DEBUG_PRINT("FOUND keyBindStatus at index %d, rect.y=%f\n", i, menu->items[i]->window.rect->y);
					}

					if (menu->items[i]->type == ITEM_TYPE_BUTTON && menu->items[i]->text) {
						lastButtonIndex = i;
					}
					if (menu->items[i]->type == ITEM_TYPE_BIND && menu->items[i]->dvar) {
						lastBindIndex = i;
					}
				}

				int newCapacity = menu->itemCount + 4;
				itemDef_s** newItemsArray = (itemDef_s**)game::UI_Alloc(sizeof(itemDef_s*) * newCapacity, 4);
				memcpy(newItemsArray, menu->items, sizeof(itemDef_s*) * menu->itemCount);
				menu->items = newItemsArray;

				MENU_DEBUG_PRINT("\n=== BEFORE MODIFICATION ===\n");
				MENU_DEBUG_PRINT("lastButtonIndex=%d lastBindIndex=%d keyBindStatusIndex=%d\n", lastButtonIndex, lastBindIndex, keyBindStatusIndex);
				if (keyBindStatusIndex != -1) {
					MENU_DEBUG_PRINT("keyBindStatus: rect.y=%f\n", menu->items[keyBindStatusIndex]->window.rect->y);
				}

				itemDef_s* originalButtonTemplate = menu->items[lastButtonIndex];
				itemDef_s* originalBindTemplate = menu->items[lastBindIndex];

				if (lastButtonIndex != -1) {
					MENU_DEBUG_PRINT("\n--- Creating Sprint button ---\n");
					itemDef_s* sprintButton = (itemDef_s*)game::UI_Alloc(sizeof(itemDef_s), 4);
					memcpy(sprintButton, originalButtonTemplate, sizeof(itemDef_s));
					sprintButton->text = game::String_Alloc(game::Menu_SafeTranslateString("@MENU_SPRINT", "Sprint"));
					sprintButton->window.rect->y += 15.f;
					sprintButton->dvarTest = game::String_Alloc("yap_sprint_enable");
					sprintButton->enableDvar = game::String_Alloc("1 ; 2");
					sprintButton->dvarFlags = 0x4;
					MENU_DEBUG_PRINT("Allocated Sprint button, rect.y=%f\n", sprintButton->window.rect->y);

					insertMenuItem(sprintButton, lastButtonIndex + 1);
					lastButtonIndex++;
				}

				if (lastButtonIndex != -1) {
					MENU_DEBUG_PRINT("\n--- Creating Sprint/Hold Breath button ---\n");
					itemDef_s* sprintBreathButton = (itemDef_s*)game::UI_Alloc(sizeof(itemDef_s), 4);
					memcpy(sprintBreathButton, originalButtonTemplate, sizeof(itemDef_s));
					sprintBreathButton->text = game::String_Alloc(game::Menu_SafeTranslateString("@MENU_SPRINT_STEADY_SNIPER_RIFLE", "Sprint/Hold Breath"));
					sprintBreathButton->dvarTest = game::String_Alloc("yap_sprint_enable");
					sprintBreathButton->enableDvar = game::String_Alloc("1 ; 2");
					sprintBreathButton->dvarFlags = 0x4;
					sprintBreathButton->window.rect->y += 30.f;
					MENU_DEBUG_PRINT("Allocated Sprint/Breath button, rect.y=%f\n", sprintBreathButton->window.rect->y);
					MENU_DEBUG_PRINT("sprintBreathButton address: %p, window.rect address: %p\n",
						sprintBreathButton, &sprintBreathButton->window.rect[0]);
					MENU_DEBUG_PRINT("keyBindStatus address: %p, window.rect address: %p\n",
						menu->items[keyBindStatusIndex], &menu->items[keyBindStatusIndex]->window.rect[0]);
					MENU_DEBUG_PRINT("keyBindStatus rect.y BEFORE insertMenuItem: %f\n",
						menu->items[keyBindStatusIndex]->window.rect->y);

					insertMenuItem(sprintBreathButton, lastButtonIndex + 1);
				}

				if (lastBindIndex > lastButtonIndex - 2) {
					lastBindIndex += 2;
				}

				if (lastBindIndex != -1) {
					MENU_DEBUG_PRINT("\n--- Creating +sprint bind ---\n");
					itemDef_s* sprintBind = (itemDef_s*)game::UI_Alloc(sizeof(itemDef_s), 4);
					memcpy(sprintBind, originalBindTemplate, sizeof(itemDef_s));
					sprintBind->dvar = game::String_Alloc("+sprint");
					sprintBind->dvarTest = game::String_Alloc("yap_sprint_enable");
					sprintBind->enableDvar = game::String_Alloc("1 ; 2");
					sprintBind->dvarFlags = 0x4;
					sprintBind->window.rect->y += 15.f;
					MENU_DEBUG_PRINT("Allocated +sprint bind, rect.y=%f\n", sprintBind->window.rect->y);

					insertMenuItem(sprintBind, lastBindIndex + 1);
					lastBindIndex++;
				}

				if (lastBindIndex != -1) {
					MENU_DEBUG_PRINT("\n--- Creating +sprintbreath bind ---\n");
					itemDef_s* sprintBreathBind = (itemDef_s*)game::UI_Alloc(sizeof(itemDef_s), 4);
					memcpy(sprintBreathBind, originalBindTemplate, sizeof(itemDef_s));
					sprintBreathBind->dvar = game::String_Alloc("+sprintbreath");
					sprintBreathBind->window.rect->y += 30.f;
					sprintBreathBind->dvarTest = game::String_Alloc("yap_sprint_enable");
					sprintBreathBind->enableDvar = game::String_Alloc("1 ; 2");
					sprintBreathBind->dvarFlags = 0x4;
					MENU_DEBUG_PRINT("Allocated +sprintbreath bind, rect.y=%f\n", sprintBreathBind->window.rect->y);

					insertMenuItem(sprintBreathBind, lastBindIndex + 1);
				}

				MENU_DEBUG_PRINT("\n=== FINAL STATE ===\n");
				if (keyBindStatusIndex != -1) {
					MENU_DEBUG_PRINT("keyBindStatus FINAL: rect.y=%f (at index %d)\n",
						menu->items[keyBindStatusIndex]->window.rect->y, keyBindStatusIndex);
				}
			}

		}

	};

}
REGISTER_COMPONENT(sprint::component);

