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
	dvar_s* cg_fovscale;
	double CG_GetViewFov_hook() {
		auto fov = cdecl_call<double>(CG_GetViewFov_ptr);
		if (cg_fovscale) {
			fov *= cg_fovscale->value.decimal;
		}
		return fov;
	}

	class component final : public component_interface
	{
	public:
		void post_gfx() override {
			if (!cg_fovscale) {
				cg_fovscale = dvars::Dvar_RegisterFloat("cg_fovscale", 1.f, 0.01f, 2.f, DVAR_ARCHIVE);

				Memory::VP::InterceptCall(exe(0x4AE8E1), CG_GetViewFov_ptr,CG_GetViewFov_hook);

			}


		}
	};


}
REGISTER_COMPONENT(fov::component);