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
dvar_s* developer;
struct kbutton_t
{
	uint64_t down;
	uint32_t downtime;
	int32_t msec;
	int8_t active;
	int8_t wasPressed;
};

namespace sprint {


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

	void __cdecl Cmd_AddCommand(const char* command_name, void(__cdecl* function)()) {
		if (developer && developer->value.integer)
		printf("command name %s func %p\n", command_name, function);

		cdecl_call(0x41BB00, command_name, function);
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

	uintptr_t setup_binds_og;
	int setup_binds() {

		Cmd_AddCommand("+sprint", IN_SprintDown);
		Cmd_AddCommand("-sprint", IN_SprintUp);

		Cmd_AddCommand("+sprintbreath", IN_BreathSprint_Down);
		Cmd_AddCommand("-sprintbreath", IN_BreathSprint_Up);

		return cdecl_call<int>(setup_binds_og);
	}

	dvar_s* yap_sprint_gun_rot_p;
	dvar_s* yap_sprint_gun_rot_r;
	dvar_s* yap_sprint_gun_rot_y;

	dvar_s* yap_sprint_gun_mov_f;
	dvar_s* yap_sprint_gun_mov_r;
	dvar_s* yap_sprint_gun_mov_u;

	dvar_s* yap_sprint_gun_ofs_f;
	dvar_s* yap_sprint_gun_ofs_r;
	dvar_s* yap_sprint_gun_ofs_u;

	dvar_s* yap_sprint_gun_bob_horz;

	dvar_s* yap_sprint_gun_bob_vert;

	dvar_s* yap_sprint_weaponBobAmplitudeSprinting;

	dvar_s* yap_sprint_internal_yet;

	dvar_s* yap_player_sprintSpeedScale;

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

	double __cdecl CG_GetWeaponVerticalBobFactor(float a1, float a2, float a3)
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

	class component final : public component_interface
	{
	public:

		void post_gfx() override {
			developer = dvars::Dvar_FindVar("developer");
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

			yap_sprint_weaponBobAmplitudeSprinting = dvars::Dvar_RegisterVec2("yap_sprint_weaponBobAmplitudeSprinting", 0.02f, 0.014f, 0.0f, 1.f, 0);

			yap_sprint_trying = dvars::Dvar_RegisterInt("yap_sprint_trying", 0, 0, 1, DVAR_ROM);
			yap_sprint_is_sprinting = dvars::Dvar_RegisterInt("yap_sprint_is_sprinting", 0, 0, 1, 0);

			yay_sprint_gun_always_read_real = dvars::Dvar_RegisterInt("yap_sprint_gun_always_read_real", 0, 0, 1, 0);

			yap_eweapon_semi_match = dvars::Dvar_RegisterInt("yap_eweapon_semi_match", 0, 0, 1, DVAR_ARCHIVE);

			yap_player_sprintSpeedScale = dvars::Dvar_RegisterFloat("yap_player_sprintSpeedScale", 1.0f, 0.f, FLT_MAX,0);

			update_sprint_gun_dvars();
			Cmd_AddCommand("reload_eweapons", loadEWeapons);

		}

		void post_cg_init() override {
			loadEWeapons();
		}

		void post_start() override {

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




			Memory::VP::InjectHook(exe(0x004B4B04), CG_GetWeaponVerticalBobFactor);

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

				if (sprint.active || sprint.wasPressed) {
					yap_activate_sprint();
					sprint.wasPressed = false;
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

			static auto PM_Weapon_skip = safetyhook::create_mid(exe(0x4DFDD5), [](SafetyHookContext& ctx) {
				if (yap_is_sprinting()) {
					ctx.eax = 1;
				}

				});

		}

	};

}
REGISTER_COMPONENT(sprint::component);

