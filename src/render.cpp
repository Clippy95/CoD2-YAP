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
	int UI_DrawHandlePic_cursor(float x, float y, float width, float height, int a5, int a6) {
		if (r_aspectratio_fix && r_aspectratio_fix->value.integer >= 2) {
			float scale = (4.f / 3.f) / *(float*)gfx(0x101D4CB8);
			float original_width = width;
			width *= scale;

			x += (original_width - width) / 2.0f;
		}
		return cdecl_call<int>(UI_DrawHandlePic_cursor_ptr, x, y, width, height, a5, a6);
	}
	void SafeAreaisModified() {
		auto safeArea_horizontal = dvars::Dvar_FindVar("safeArea_horizontal");
		if (safeArea_horizontal)
			safeArea_horizontal->modified = true;

		auto safeArea_vertical = dvars::Dvar_FindVar("safeArea_vertical");

		if(safeArea_vertical)
			safeArea_horizontal->modified = true;
	}
	class component final : public component_interface
	{
	public:

		void post_start() override {
			SetProcessDPIAware();
			Memory::VP::InterceptCall(0x4C3A6E, UI_DrawHandlePic_cursor_ptr, UI_DrawHandlePic_cursor);
			Memory::VP::Nop(exe(0x0040FA04), 2);

		}
		void post_cg_init() override {
			SafeAreaisModified();
		}
		void post_gfx() override {
			SafeAreaisModified();
			if (!r_buf_dynamicIndexBuffer_mult) {
				r_buf_dynamicIndexBuffer_mult = dvars::Dvar_RegisterFloat("r_buf_dynamicIndexBuffer_mult", 4.f, 1.f, 16.f, DVAR_ARCHIVE);
				r_buf_dynamicVertexBuffer_mult = dvars::Dvar_RegisterFloat("r_buf_dynamicVertexBuffer_mult", 4.f, 1.f, 16.f, DVAR_ARCHIVE);

				r_aspectratio_fix = dvars::Dvar_RegisterInt("r_aspectRatio_fix", 2, 0, 2, DVAR_ARCHIVE);

			}
			CreateMidHook(gfx(0x10011B60), [](SafetyHookContext& ctx) {
				auto value = (float)ctx.ecx * r_buf_dynamicIndexBuffer_mult->value.decimal;
				ctx.ecx = (uint32_t)value;
				printf("dynamic index buffer return %p ecx %d\n", *(int*)ctx.esp, ctx.ecx);
				


				});

			CreateMidHook(gfx(0x10011AD0), [](SafetyHookContext& ctx) {
				auto value = (float)ctx.ecx * r_buf_dynamicVertexBuffer_mult->value.decimal;
				ctx.ecx = (uint32_t)value;
				printf("dynamic vertex buffer return %p ecx %d\n", *(int*)ctx.esp, ctx.ecx);



				});
			Memory::VP::Patch<float>(gfx(0x1000ACEF + 1), 0.f);

			CreateMidHook(gfx(0x10011710), [](SafetyHookContext& ctx) {
				if (r_aspectratio_fix && r_aspectratio_fix->value.integer) {

					uint32_t* res = (uint32_t*)gfx(0x101D4CA8);

					Memory::VP::Patch<float>(gfx(0x101D4CB8), (float)res[0] / (float)res[1]);
					}



				});

			auto windowstyle_addr = gfx(0x10012B16);

			static auto r_noborder = dvars::Dvar_RegisterInt("r_noborder", 0, 0, 1,DVAR_ARCHIVE);

			Memory::VP::Nop(windowstyle_addr, 5);
			CreateMidHook(windowstyle_addr, [](SafetyHookContext& ctx) {

				if (r_noborder->value.integer) {
					ctx.ebp = WS_VISIBLE | WS_POPUP;
				}
				else {
					ctx.ebp = WS_VISIBLE | WS_SYSMENU | WS_CAPTION;
				}

				});

			// r_lodScale
			Memory::VP::Patch<uint32_t>(gfx(0x1000AD0C + 1), DVAR_RENDERER | DVAR_ARCHIVE);
			// r_lodBias
			Memory::VP::Patch<uint32_t>(gfx(0x1000ACE5 + 1), DVAR_RENDERER | DVAR_ARCHIVE);

		}

		void post_menu_parse(menuDef_t* menu) override {
			for (int i = 0; i < menu->itemCount; i++) {
				auto item = menu->items[i];
				if (item && item->window.name) {
					auto rect = &item->window.rect[0];

					rectDef_s backimage2fade_og = { -332.f, -162.f, 896.f, 484.f };
					if (!strcmp(item->window.name, "backimage2fade") && rect->floatsEqual(backimage2fade_og)) {
						rect->x = -640.f;
						rect->y = -480.f;
						rect->w = 640.f;
						rect->h = 480.f;
						rect->horzAlign = 4;
						rect->vertAlign = 4;
					}

					rectDef_s background_og = { -128.f, 0.f, 896.f, 480.f };
					if (matchesAny(item->window.name, { "background_image", "main_back", "defaultbackdrop", "backdrop" })
						&& rect->floatsEqual(background_og)) {
						rect->x = -106.8f;
						rect->y = 0.f;
						rect->w = 853.5f;
						rect->h = 480.f;
						//rect->horzAlign = 4;
						//rect->vertAlign = 4;
					}

					rectDef_s fadebox_og = { 0.f, 0.f, 640.f, 480.f };
					if (!strcmp(item->window.name, "fadebox") && rect->floatsEqual(fadebox_og)) {
						rect->horzAlign = 4;
						rect->vertAlign = 4;
					}

					rectDef_s bg_gradient_og = { 0.f, 65.f, 416.f, 351.f };
					if (!strcmp(item->window.name, "background_gradient") && rect->floatsEqual(bg_gradient_og)) {
						rect->x = 0.f;
						rect->y = 0.f;
						rect->w = 640.f;
						rect->h = 480.f;
						rect->horzAlign = 4;
						rect->vertAlign = 4;
					}

					rectDef_s main_back_top_og = { 0.f, 0.f, 640.f, 320.f };
					if (!strcmp(item->window.name, "main_back_top") && rect->floatsEqual(main_back_top_og)) {
						rect->x = -106.8f;
						rect->y = 0.f;
						rect->w = 853.5f;
						rect->h = 320.f;
					}

					rectDef_s main_back_bottom_og = { 0.f, 320.f, 640.f, 160.f };
					if (!strcmp(item->window.name, "main_back_bottom") && rect->floatsEqual(main_back_bottom_og)) {
						rect->x = -106.8f;
						rect->y = 320.f;
						rect->w = 853.5f;
						rect->h = 160.f;
					}
				}
			}
		}

	};

}
REGISTER_COMPONENT(render::component);

