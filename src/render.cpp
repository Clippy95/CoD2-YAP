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

namespace render {
	dvar_s* r_buf_dynamicIndexBuffer_mult;
	dvar_s* r_buf_dynamicVertexBuffer_mult;
	dvar_s* r_aspectratio_fix;
	uintptr_t UI_DrawHandlePic_cursor_ptr;
	int UI_DrawHandlePic_cursor(int a1, int a2, float width, float height, int a5, int a6) {

		if (r_aspectratio_fix && r_aspectratio_fix->value.integer >= 2) {
			float scale = (4.f / 3.f) / *(float*)gfx(0x101D4CB8);
			width *= scale;
		}
		return cdecl_call<int>(UI_DrawHandlePic_cursor_ptr, a1, a2, width, height, a5, a6);
	}

	class component final : public component_interface
	{
	public:

		void post_start() override {

			Memory::VP::InterceptCall(0x4C3A6E, UI_DrawHandlePic_cursor_ptr, UI_DrawHandlePic_cursor);

		}

		void post_gfx() override {
			if (!r_buf_dynamicIndexBuffer_mult) {
				r_buf_dynamicIndexBuffer_mult = dvars::Dvar_RegisterInt("r_buf_dynamicIndexBuffer_mult", 4, 1, 16, DVAR_ARCHIVE);
				r_buf_dynamicVertexBuffer_mult = dvars::Dvar_RegisterInt("r_buf_dynamicVertexBuffer_mult", 4, 1, 16, DVAR_ARCHIVE);

				r_aspectratio_fix = dvars::Dvar_RegisterInt("r_aspectRatio_fix", 2, 0, 2, DVAR_ARCHIVE);

			}
			CreateMidHook(gfx(0x10011B60), [](SafetyHookContext& ctx) {
				ctx.ecx *= r_buf_dynamicIndexBuffer_mult->value.integer;
				printf("dynamic index buffer return %p ecx 0x%X\n", *(int*)ctx.esp, ctx.ecx);
				


				});

			CreateMidHook(gfx(0x10011AD0), [](SafetyHookContext& ctx) {
				ctx.ecx *= r_buf_dynamicVertexBuffer_mult->value.integer;
				printf("dynamic vertex buffer return %p ecx 0x%X\n", *(int*)ctx.esp, ctx.ecx);



				});
			Memory::VP::Patch<float>(gfx(0x1000ACEF + 1), 0.f);

			CreateMidHook(gfx(0x10011710), [](SafetyHookContext& ctx) {
				if (r_aspectratio_fix && r_aspectratio_fix->value.integer) {

					uint32_t* res = (uint32_t*)gfx(0x101D4CA8);

					Memory::VP::Patch<float>(gfx(0x101D4CB8), (float)res[0] / (float)res[1]);
					}



				});
		}



	};

}
REGISTER_COMPONENT(render::component);

