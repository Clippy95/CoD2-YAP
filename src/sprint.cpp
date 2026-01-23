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
dvar_s* developer;

#define CMD_SPRINT (1 << 31)
#define PMF_SPRINTING CMD_SPRINT

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

	void set_sprinting(bool sprinting) {
		yap_sprint_is_sprinting->value.integer = sprinting ? 1 : 0;
		yap_sprint_is_sprinting->latchedValue.integer = sprinting ? 1 : 0;
		yap_sprint_is_sprinting->modified = true;

		if (developer && developer->value.integer) {
			printf("Sprint state changed to: %d\n", sprinting);
		}
	}


	bool can_sprint() {
		return g_sprintState.SprintFaituge >= yap_sprint_fatigue_min_threshold->value.decimal;
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

	dvar_s* yap_eweapon_semi_match;

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
			if(developer && developer->value.integer)
			printf("name %s\n", *(const char**)GetCurrentWeapon());
			return *(const char**)(GetCurrentWeapon());
		}


		return NULL;
	}

	const eWeaponDef* GetCurrentEWeapon() {
		return GetEWeapon(GetCurrentWeaponName());
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
			if (!overwrite && g_eWeaponDefs.find(weaponName) != g_eWeaponDefs.end()) {
				Com_Printf("Skipping '%s' (already loaded with higher priority)\n", weaponName.c_str());
				continue;
			}

			try {
				std::ifstream file(entry.path());
				nlohmann::json j = nlohmann::json::parse(file);

				eWeaponDef weaponDef;
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

				g_eWeaponDefs[weaponName] = weaponDef;
				Com_Printf("Loaded eWeapon '%s' - sprintBobH: %.3f, sprintBobV: %.3f, sprintSpeedScale: %.3f\n",
					weaponName.c_str(),
					weaponDef.vSprintBob[0],
					weaponDef.vSprintBob[1],
					weaponDef.sprintSpeedScale);
			}
			catch (const std::exception& e) {
				Com_Printf("Failed to parse %s: %s\n",
					entry.path().string().c_str(), e.what());
			}
		}
	}

	void loadEWeapons() {

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
		if (developer && developer->value.integer)
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
			mov ecx, command_name
			mov esi, string
			call GetKeyBindingLocal_addr
			mov result,eax
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
		//auto result = GetKeyBindingLocalizedString((const char*)ecx_og, (char (*)[128])esi_og);

		//if (!result)
			auto result = GetKeyBindingLocalizedString("+" s_SPRINT_AND_BREATH, (char (*)[128])esi_og);

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
		//printf("ps pm %p %p\n", pm->ps,pm);
		auto flags = pm->ps->pm_flags;

		bool tryingToMove = yay_sprint_mode->value.integer ? pm->cmd.forwardmove > 0 : (pm->cmd.forwardmove || pm->cmd.rightmove);

		bool wantsToSprint = tryingToMove && (pm->cmd.buttons & CMD_SPRINT);

		// Check ADS state - thing[46] is ADS fraction (0.0 = not ADS, 1.0 = full ADS)
		float* thing = (float*)pm->ps;
		float adsFraction = thing[46];
		bool isADS = adsFraction >= 0.1f;

		bool AllowedToSprint = !isADS && !(flags & PMF_CROUCH) && !(flags & PMF_PRONE) && !(flags & PMF_FRAG) && !(flags & PMF_MANTLE) && !(flags & PMF_LADDER);

		AllowedToSprint = AllowedToSprint && (pm->ps->speed >= (int)((float)dvars::Dvar_FindVar("g_speed")->defaultValue.integer * 0.85f));

		if (wantsToSprint && can_sprint() && AllowedToSprint) {
			// Activate sprinting only if not in ADS
			pm->ps->pm_flags |= PMF_SPRINTING;
			set_sprinting(true);
			g_sprintState.preventFatigueRegen = false; // Allow regen when successfully sprinting
		}
		else {
			// Deactivate sprinting (ADS, no fatigue, or not wanting to sprint)
			pm->ps->pm_flags &= ~PMF_SPRINTING;
			set_sprinting(false);

			// If player wants to sprint but can't (exhausted), prevent fatigue regen
			if (wantsToSprint && !can_sprint() && AllowedToSprint) {
				g_sprintState.preventFatigueRegen = true;
			}
			else {
				// Player let go of sprint or is in ADS, allow regen
				g_sprintState.preventFatigueRegen = false;
			}
		}

		if (developer && developer->value.integer) {
			printf("pm_flags 0x%X adsFrac: %.3f %d, wantsSprint: %d, canSprint: %d, isADS: %d, preventRegen: %d\n",
				pm->ps->pm_flags, adsFraction, pm->ps->commandTime, wantsToSprint, can_sprint(), isADS, g_sprintState.preventFatigueRegen);
		}
	}

	// in UO this is only done from g side but in CoD2+ everythings been merged hmmm (well expect for linux server)
	void PM_UpdateFatigue(pmove_t* pm) {
		if (!pm || !pm->ps)
			return;

		static int lastRealTime = 0;

		// How would this work in a server/MP lol this sprint is really only meant for SP... atm
		int realTime = *(int*)0xF708DC; // cgameGlob->time

		if (lastRealTime == 0) {
			lastRealTime = realTime;
			return;
		}

		if (realTime == lastRealTime)
			return;

		int frameDeltaMs = realTime - lastRealTime;
		lastRealTime = realTime;

		if (frameDeltaMs <= 0 || frameDeltaMs > 1000)
			return;

		float frameTimeSec = frameDeltaMs * 0.001f;
		float threshold = 4.5f;

		vector2& velocity_2d = *(vector2*)&pm->ps->velocity;
		float speedSquared = velocity_2d.length();
		bool is_moving = yap_sprint_fatigue_drainonmove->value.integer ? speedSquared > threshold : true;

		if (yap_is_sprinting()) {
			if (is_moving){
				g_sprintState.lastSprintTime = realTime;

			float drainAmount = frameTimeSec * yap_sprint_fatigue_drain_rate->value.decimal;
			g_sprintState.SprintFaituge -= drainAmount;

			if (g_sprintState.SprintFaituge < 0.0f) {
				g_sprintState.SprintFaituge = 0.0f;
				set_sprinting(false);
				pm->ps->pm_flags &= ~PMF_SPRINTING;
			}
			g_sprintState.preventFatigueRegen = false;
	}
		}
		else {
			float timeSinceLastSprint = (realTime - g_sprintState.lastSprintTime) * 0.001f;

			if (!g_sprintState.preventFatigueRegen &&
				timeSinceLastSprint >= yap_sprint_fatigue_regen_delay->value.decimal) {

				g_sprintState.SprintFaituge += frameTimeSec * yap_sprint_fatigue_regen_rate->value.decimal;
				if (g_sprintState.SprintFaituge > 1.0f) {
					g_sprintState.SprintFaituge = 1.0f;
				}
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

		float fatiguePercent = g_sprintState.SprintFaituge; // 0.0 to 1.0

		if (yay_sprint_display_icon && yay_sprint_display_icon->value.integer && yap_is_sprinting()) {
			shader = (void*)stance_sprint_shader;
		}

		UI_DrawHandlePic_with_t(x, y, w, h, ebx_og, edi_og, 1.f, 1.f, 1.f, 1.f, &dem_color, shader);
		// this down here is the white one
		float filledHeight = h * fatiguePercent;
		float emptyHeight = h * (1.0f - fatiguePercent);

		// Start at y + emptyHeight to position at bottom, fill upwards
		// s0=0, t0=1-fatiguePercent (skip top portion of texture), s1=1, t1=1
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
		}


		auto result = cdecl_call<int>(PM_IDK4DE720_og, pm, a2);
		printf("anim %d state %d delay %d time %d\n", ps->weapAnim, ps->weaponstate, ps->weaponDelay,ps->weaponTime);

		return result;

	}

	class component final : public component_interface
	{
	public:

		void post_unpack() override {
			developer = dvars::Dvar_FindVar("developer");
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

			yap_sprint_internal_yet = dvars::Dvar_RegisterInt("yap_sprint_internal_yet", 0, 0, 0, DVAR_ROM);

			yap_sprint_weaponBobAmplitudeSprinting = dvars::Dvar_RegisterVec2("yap_sprint_weaponBobAmplitudeSprinting", 0.02f, 0.014f, 0.0f, 1.f, DVAR_ARCHIVE);
			yap_sprint_bobAmplitudeSprinting = dvars::Dvar_RegisterVec2("yap_sprint_bobAmplitudeSprinting", 0.02f, 0.014f, 0.0f, 1.f, DVAR_ARCHIVE);
			yap_sprint_trying = dvars::Dvar_RegisterInt("yap_sprint_trying", 0, 0, 1, DVAR_ROM);
			yap_sprint_is_sprinting = dvars::Dvar_RegisterInt("yap_sprint_is_sprinting", 0, 0, 1, 0);

			yay_sprint_gun_always_read_real = dvars::Dvar_RegisterInt("yap_sprint_gun_always_read_real", 0, 0, 1, 0);

			yay_sprint_mode = dvars::Dvar_RegisterInt("yap_sprint_mode", 0, 0, 1, DVAR_ARCHIVE,"0 = Allow sprinting omnidirectionally like in United Offensive\n1 = Only allow sprinting when at least moving forward like IW3+");

			yap_eweapon_semi_match = dvars::Dvar_RegisterInt("yap_eweapon_semi_match", 0, 0, 1, DVAR_ARCHIVE);

			yap_player_sprintSpeedScale = dvars::Dvar_RegisterFloat("yap_player_sprintSpeedScale", 1.6f, 0.f, FLT_MAX,DVAR_ARCHIVE,"The scale applied to the player speed when sprinting");

			yap_player_sprintStrafeSpeedScale = dvars::Dvar_RegisterFloat("yap_player_sprintStrafeSpeedScale", 0.667, 0.01f, 1.f, DVAR_ARCHIVE, "The speed at which you can strafe while sprinting");

			//yap_sprint_reload_cancel = dvars::Dvar_RegisterInt("yap_sprint_reload_cancel", 0, 0, 1, DVAR_ARCHIVE, "Cancels reloading when starting to sprint");

			yap_sprint_bind_holdbreath = dvars::Dvar_RegisterInt("yap_sprint_bind_holdbreath", 1, 0, 1, DVAR_ARCHIVE);

			yap_sprintCameraBob = dvars::Dvar_RegisterFloat("yap_sprintCameraBob", 0.f, 0.f, 2.f, DVAR_ARCHIVE, "The speed the camera bobs while you sprint");

			yap_sprint_allow_start_reload = dvars::Dvar_RegisterInt("yap_sprint_allow_start_reload", 0, 0, 1, DVAR_ARCHIVE, "Turning this on allows you to initiate a reload while sprinting.");

			update_sprint_gun_dvars();
			game::Cmd_AddCommand("reload_eweapons", loadEWeapons);
			Memory::VP::Nop(0x4D485C, 6);
			Memory::VP::InjectHook(0x4D485C, CL_DrawStretchPic_sprint_stub_lazy, Memory::VP::HookType::Call);
		}

		void post_cg_init() override {
			loadEWeapons();
		}

		void post_start() override {

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
			//Memory::VP::InjectHook(0x4A9EE7, GetKeyBindingLocalizedString_meleebreath_stub);
			Memory::VP::InjectHook(0x4CDB5B, I_stricmp_new_custom_bindings);
			static auto CG_CheckPlayerStanceChange = safetyhook::create_mid(0x4BE445, [](SafetyHookContext& ctx) {

				bool isSprinting = yap_is_sprinting();

				// Check if sprint state changed
				if (isSprinting != g_sprintState.wasSprinting) {
					// flash the stance baby
					ctx.ecx = 0x00002300;

					// Update lastStanceChangeTime to trigger the white flash
					int* cgTime = (int*)0xF708DC;
					int* lastStanceChangeTime = (int*)0xF796C8;
					*lastStanceChangeTime = *cgTime;

					// Update state
					g_sprintState.wasSprinting = isSprinting;
				}

				int* mask = (int*)(ctx.esp + 0x4);

				if (g_sprintState.SprintFaituge != 1.f)
					*mask = 0x00002300;

				});


			//static auto CG_CheckPlayerStanceChange1 = safetyhook::create_mid(0x4BE445, [](SafetyHookContext& ctx) {



			//	});

			Memory::VP::InterceptCall(exe(0x50CACF), PM_WalkMove_og, PM_WalkMove);

			//Memory::VP::InterceptCall(exe(0x4DFC88), PM_IDK4DE720_og, PM_IDK4DE720);

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
				uint32_t* buttons = (uint32_t*)(ctx.eax + 0x4);
				if (sprint.active || sprint.wasPressed) {
					yap_activate_sprint();
					sprint.wasPressed = false;
					*buttons |= CMD_SPRINT;


				}
				else
					yap_deactivate_sprint();
				if (developer && developer->value.integer)
				printf("is trying to sprint? %d\n", yap_is_trying_sprinting());

				});

			static auto cmdwalk_sprint = safetyhook::create_mid(exe(0x50821F), [](SafetyHookContext& ctx) {

				float& speed = *(float*)(ctx.esp + 0x8);

				if (yap_is_sprinting()) {
					speed *= yap_player_sprintSpeedScale->value.decimal;
				}
				if (developer && developer->value.integer)
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

					float newBob = xyspeed / maxspeed * yap_sprintCameraBob->value.decimal;

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

	};

}
REGISTER_COMPONENT(sprint::component);

