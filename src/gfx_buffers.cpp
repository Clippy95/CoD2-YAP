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

namespace gfx_buffers {
	dvar_s* r_buf_dynamicIndexBuffer_mult;
	dvar_s* r_buf_dynamicVertexBuffer_mult;
	class component final : public component_interface
	{
	public:

		void post_start() override {

		}

		void post_gfx() override {
			if (!r_buf_dynamicIndexBuffer_mult) {
				r_buf_dynamicIndexBuffer_mult = dvars::Dvar_RegisterInt("r_buf_dynamicIndexBuffer_mult", 4, 1, 16, DVAR_ARCHIVE);
				r_buf_dynamicVertexBuffer_mult = dvars::Dvar_RegisterInt("r_buf_dynamicVertexBuffer_mult", 4, 1, 16, DVAR_ARCHIVE);
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
		}


	};

}
REGISTER_COMPONENT(gfx_buffers::component);

