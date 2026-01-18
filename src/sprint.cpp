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
struct kbutton_t
{
	uint64_t down;
	uint32_t downtime;
	int32_t msec;
	int8_t active;
	int8_t wasPressed;
};

namespace sprint {

	void __cdecl Cmd_AddCommand(const char* command_name, void(__cdecl* function)()) {

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
		printf("sprint down %p", &sprint);
		IN_KeyDown(&sprint);
	}

	void IN_SprintUp() {
		IN_KeyUp(&sprint);
	}

	uintptr_t setup_binds_og;
	int setup_binds() {

		Cmd_AddCommand("+sprint", IN_SprintDown);
		Cmd_AddCommand("-sprint", IN_SprintUp);

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

	dvar_s* yap_sprint_internal_yet;

	dvar_s* yap_sprint_gun_bob_horz;

	dvar_s* yap_sprint_gun_bob_vert;

	dvar_s* yap_sprint_weaponBobAmplitudeSprinting;

	dvar_s* yap_sprint_trying;

	dvar_s* yap_sprint_is_sprinting;
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

	vector3 yap_sprint_gun_mov() {
		return vector3{ yap_sprint_gun_mov_f->value.decimal,yap_sprint_gun_mov_r->value.decimal,yap_sprint_gun_mov_u->value.decimal };
	}

	vector3 yap_sprint_gun_rot() {
		return vector3{ yap_sprint_gun_rot_p->value.decimal,yap_sprint_gun_rot_r->value.decimal,yap_sprint_gun_rot_y->value.decimal };
	}

	vector2 yap_sprint_gun_bob() {
		return vector2{ yap_sprint_gun_bob_horz->value.decimal,yap_sprint_gun_bob_vert->value.decimal };
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
		}

		void post_start() override {

			Memory::VP::InterceptCall(0x40E164, setup_binds_og, setup_binds);
			Memory::VP::Nop(exe(0x004EC7D0), 2);

			static auto CG_ViewAddWeaponorsmth = safetyhook::create_mid(exe(0x4B5730), [](SafetyHookContext& ctx) {

				if (yap_is_sprinting()) {
					Memory::VP::Patch<dvar_s**>(exe((0x4B5847 + 2)), &yap_sprint_gun_rot_p);
					Memory::VP::Patch<dvar_s**>(exe((0x4B5851 + 1)), &yap_sprint_gun_rot_y);
					Memory::VP::Patch<dvar_s**>(exe((0x4B585B + 2)), &yap_sprint_gun_rot_r);
				}
				else {
					Memory::VP::Patch<dvar_s**>(exe((0x4B5847 + 2)), (dvar_s**)0x01040C10);
					Memory::VP::Patch<dvar_s**>(exe((0x4B5851 + 1)), (dvar_s**)0x01040B9C);
					Memory::VP::Patch<dvar_s**>(exe((0x4B585B + 2)), (dvar_s**)0x00F569EC);
				}

				});

			static auto CG_ViewAddWeaponorsmth2 = safetyhook::create_mid(exe(0x4B5380), [](SafetyHookContext& ctx) {

				if (yap_is_sprinting()) {
					Memory::VP::Patch<dvar_s**>(exe((0x4B54AC + 2)), &yap_sprint_gun_mov_f);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54B6 + 1)), &yap_sprint_gun_mov_r);
					Memory::VP::Patch<dvar_s**>(exe((0x4B54C0 + 2)), &yap_sprint_gun_mov_u);



					Memory::VP::Patch<dvar_s**>(exe((0x4B5511 + 1)), &yap_sprint_gun_ofs_f);
					Memory::VP::Patch<dvar_s**>(exe((0x4B551C + 2)), &yap_sprint_gun_ofs_r);
					Memory::VP::Patch<dvar_s**>(exe((0x4B553F + 1)), &yap_sprint_gun_ofs_u);



					Memory::VP::Patch<dvar_s**>(exe((0x4B5561 + 2)), &yap_sprint_gun_ofs_f);
					Memory::VP::Patch<dvar_s**>(exe((0x4B556D + 1)), &yap_sprint_gun_ofs_r);
					Memory::VP::Patch<dvar_s**>(exe((0x4B558F + 2)), &yap_sprint_gun_ofs_u);
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
				kbutton_t* holdbreath = (kbutton_t*)0x005D20D4;

				if (holdbreath->active || holdbreath->wasPressed)
					yap_activate_sprint();
				else
					yap_deactivate_sprint();


				});

		}

	};

}
REGISTER_COMPONENT(sprint::component);

