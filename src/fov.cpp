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
namespace fov {
	uintptr_t CG_GetViewFov_ptr;
	uintptr_t someFOVthing_ptr;
	dvar_s* cg_fovscale;
	dvar_s* cg_fov_user;
	dvar_s* cg_fov;
	dvar_s* cg_fov_fix_lowfovads;
	dvar_s cg_fov_fake;
	dvar_s* cg_fov_fake_ptr = &cg_fov_fake;
	dvar_s* cg_fov_fix_lowfovads_default_fov;

	double CG_GetViewFov_hook() {

		cg_fov_fake.value.decimal = std::clamp((cg_fov->value.decimal * (cg_fov_user->value.vec2->x / cg_fov_user->value.vec2->y)),1.f,160.f);

		auto fov = cdecl_call<double>(CG_GetViewFov_ptr);
		if (cg_fovscale) {
			fov *= cg_fovscale->value.decimal;
		}
		return fov;
	}

	int someFOVthing() {
		cg_fov_fake.value.decimal = std::clamp((cg_fov->value.decimal * (cg_fov_user->value.vec2->x / cg_fov_user->value.vec2->y)), 1.f, 160.f);
		auto fov = cdecl_call<int>(someFOVthing_ptr);
		return fov;

	}

	void HandleWeaponADS_hack(float* current_weapon_fov) {
		if (cg_fov) {
			if (cg_fov_fix_lowfovads && cg_fov_fix_lowfovads->value.integer == 1) {
				if (cg_fov->value.decimal <= *current_weapon_fov) {
					float value = cg_fov->value.decimal / cg_fov_fix_lowfovads_default_fov->value.decimal;
					*current_weapon_fov *= value;
				}
			}
			else if (cg_fov_fix_lowfovads && cg_fov_fix_lowfovads->value.integer >= 2) {
				if (cg_fov->value.decimal <= cg_fov_fix_lowfovads_default_fov->value.decimal) {
					float value = cg_fov->value.decimal / cg_fov_fix_lowfovads_default_fov->value.decimal;
					*current_weapon_fov *= value;
				}
			}
		}

		//if (cg_fovscale_ads) {
		//	*current_weapon_fov *= std::clamp(cg_fovscale_ads->value, 0.1f, FLT_MAX);
		//}

	}

	class component final : public component_interface
	{
	public:
		void post_gfx() override {
			if (!cg_fovscale) {
				cg_fovscale = dvars::Dvar_RegisterFloat("cg_fovscale", 1.f, 0.01f, 2.f, DVAR_ARCHIVE,"Scale applied to the field of view");
				cg_fov_fix_lowfovads = dvars::Dvar_RegisterInt("cg_fov_fix_lowfovads", 1, 0, 2, DVAR_ARCHIVE);
				cg_fov_fix_lowfovads_default_fov = dvars::Dvar_RegisterFloat("cg_fov_fix_lowfovads_default_fov", 80.f, 1.f, 160.f, DVAR_ARCHIVE);
				Memory::VP::InterceptCall(exe(0x4AE8E1), CG_GetViewFov_ptr,CG_GetViewFov_hook);
				Memory::VP::InterceptCall(exe(0x4AF665), someFOVthing_ptr, someFOVthing);
				Memory::VP::Nop(exe(0x004AE7EF), 6);
				static auto WeaponADS_hack = safetyhook::create_mid(exe(0x004AE7EF), [](SafetyHookContext& ctx) {
					float current_weapon_fov_ads = *(float*)(ctx.ecx + 0x268);

					HandleWeaponADS_hack(&current_weapon_fov_ads);
					FPU::FLD(current_weapon_fov_ads);
					});

			}


		}

		void post_start() override {
			Memory::VP::Patch(exe(0x4A1195 + 1), DVAR_ARCHIVE | DVAR_SCRIPTINFO);

			Memory::VP::Patch<dvar_s**>(exe(0x4AE7D9 + 1), &cg_fov_fake_ptr); // no worry of bad read, its always trying to read value.decimal
			Memory::VP::Patch<dvar_s**>(exe(0x4AE965 + 2), &cg_fov_fake_ptr); // no worry of bad read, its always trying to read value.decimal

		}

		void post_cg_init() override {
			cg_fov = dvars::Dvar_FindVar("cg_fov");

			float default_value = cg_fov ? cg_fov->defaultValue.decimal : 80.f;
			cg_fov_user = dvars::Dvar_RegisterVec2("cg_fov_user", default_value, default_value, 1.f, 160.f, DVAR_ARCHIVE, "Field of View in degrees\nWorks on top of cg_fov allowing for proper scaling if scripts change cg_fov");

		}

	};


}
REGISTER_COMPONENT(fov::component);