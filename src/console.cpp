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
#include "dvar_descriptions.h"
#include <unordered_set>
namespace console {

	struct ConDrawInputGlob
	{
		uint32_t matchCount;
		char inputText[4];
		char inputTextLen[4];
		int8_t hasExactMatch;
		char pad0[8];
		float x;
		float y;
		float leftX;
		float fontHeight;
	};
	ConDrawInputGlob* conDraw = (ConDrawInputGlob*)0x05AC208;

	vec4_t whiteColor = { 8.0f, 8.0f, 8.0f, 1.0f };
	uintptr_t ConDrawInput_TextLimitChars_addr = exe(0x405510);
	void ConDrawInput_TextLimitChars(float* color, const char* text, int num_chars = 0x7FFFFFFF) {
		__asm {
			mov eax, color
			push num_chars
			push text
			call ConDrawInput_TextLimitChars_addr
			add esp, 8
		}
	}


	const char* Dvar_GetDescription(const dvar_s* dvar) {
		if (!dvar || !dvar->name)
			return nullptr;

		// Check runtime map first (allows overrides)
		auto it = dvars::descriptions_runtime.find(dvar->name);
		if (it != dvars::descriptions_runtime.end())
			return it->second.c_str();

		// Fallback to const map (from Xbox 360 dump)
		auto it2 = dvar_descriptions.find(dvar->name);
		if (it2 != dvar_descriptions.end())
			return it2->second.c_str();

		return nullptr;
	}

	int32_t ConDrawInput_GetDvarDescriptionLines(const dvar_s* dvar)
	{
		const char* description = Dvar_GetDescription(dvar);

		if (!description || description[0] == '\0')
			return 0;

		int32_t linecount = 1;

		for (size_t index = 0; description[index] != '\0'; ++index)
		{
			if (description[index] == '\n')
				++linecount;
		}

		return linecount;
	}

	uintptr_t ConDrawInput_Box_og;
	SAFETYHOOK_NOINLINE void ConDrawInput_Box_domain(dvar_s *thisdvar,int lines, float* color) {


		int desc_lines = ConDrawInput_GetDvarDescriptionLines(thisdvar);
		cdecl_call<void>(ConDrawInput_Box_og, desc_lines + lines, color);
		const char* desc = Dvar_GetDescription(thisdvar);
		if (desc) {
			ConDrawInput_TextLimitChars((float*)&whiteColor[0], desc);

			for (int i = 0; i < desc_lines; ++i)
			{
				conDraw->y += conDraw->fontHeight;
				conDraw->x = conDraw->leftX;
			}

		}
	}

	void ConDrawInput_Box_domain_le_proper(int lines, float* color) {

		dvar_s* thisdvar;
		__asm mov thisdvar, esi

		ConDrawInput_Box_domain(thisdvar, lines, color);

	}

	class component final : public component_interface
	{
	public:

		void post_start() override {
			if (!exe(1))
				return;
			// from COD2x
			if (exe(1)) {
				Memory::VP::Patch<uint8_t>(0x406218 + 2, 60);
			    Memory::VP::Patch<uint8_t>(0x405C81 + 1, 40);
				Memory::VP::Patch<uint8_t>(0x40582C + 1, 40);

				Memory::VP::Patch<float>(0x5921E0, 340.f);
			}

			Memory::VP::InterceptCall(0x405E9B, ConDrawInput_Box_og, ConDrawInput_Box_domain_le_proper);
			Memory::VP::Patch<uint8_t>(exe(0x405C43 + 2), 3);
			static auto consoel_flags = safetyhook::create_mid(exe(0x405E43), [](SafetyHookContext& ctx) {
				dvar_s* thisdvar = (dvar_s*)(ctx.esi);
				uint16_t flags = thisdvar->flags;

				std::string flagsString = "Flags: ";
				if (flags & DVAR_ARCHIVE) flagsString += "Archive, ";
				if (flags & DVAR_USERINFO) flagsString += "UserInfo, ";
				if (flags & DVAR_SERVERINFO) flagsString += "ServerInfo, ";
				if (flags & DVAR_SYSTEMINFO) flagsString += "SystemInfo, ";
				if (flags & DVAR_NOWRITE) flagsString += "NoWrite, ";
				if (flags & DVAR_LATCH) flagsString += "Latch, ";
				if (flags & DVAR_ROM) flagsString += "Rom, ";
				if (flags & DVAR_CHEAT) flagsString += "Cheat, ";
				if (flags & DVAR_CONFIG) flagsString += "Config, ";
				if (flags & DVAR_SAVED) flagsString += "Saved, ";
				if (flags & DVAR_SCRIPTINFO) flagsString += "ScriptInfo, ";
				if (flags & DVAR_CHANGEABLE_RESET) flagsString += "ChangeableReset, ";
				if (flags & DVAR_RENDERER) flagsString += "Renderer, ";
				if (flags & DVAR_EXTERNAL) flagsString += "External, ";
				if (flags & DVAR_AUTOEXEC) flagsString += "AutoExec, ";


				if (flagsString.size() > 7) { // length of "Flags: "
					flagsString.pop_back(); // remove space
					flagsString.pop_back(); // remove comma
				}

				conDraw->y += conDraw->fontHeight;
				conDraw->x = conDraw->leftX;
				ConDrawInput_TextLimitChars((float*)&whiteColor[0], flagsString.c_str(), flagsString.length());



				});


		}

		//void post_unpack() override {

		//}


	};
}
REGISTER_COMPONENT(console::component);

