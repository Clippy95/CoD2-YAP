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

#define CHANNEL_AUTO 0
#define CHANNEL_AUTO2D 1
#define CHANNEL_MENU 2
#define CHANNEL_BODY 3
#define CHANNEL_ITEM 4
#define CHANNEL_WEAPON 5
#define CHANNEL_VOICE 6
#define CHANNEL_LOCAL 7
#define CHANNEL_MUSIC 8
#define CHANNEL_ANNOUNCER 9
#define CHANNEL_SHELLSHOCK 10

enum g_snd_channelvol_array {
	AUTO,
	AUTO2D,
	MENU,
	WEAPON,
	VOICE,
	ITEM,
	BODY,
	LOCAL,
	MUSIC,
	ANNOUCER,
	SHELLSHOCK,
	MAX_CHANNELVOL_ARRAY
};

const char* channelNames[] = {
	"auto",
	"auto2d",
	"menu",
	"weapon",
	"voice",
	"item",
	"body",
	"local",
	"music",
	"announcer",
	"shellshock"
};

const int channelMap[MAX_CHANNELVOL_ARRAY] = {
	CHANNEL_AUTO,      // AUTO (0) -> 0
	CHANNEL_AUTO2D,    // AUTO2D (1) -> 1
	CHANNEL_MENU,      // MENU (2) -> 2
	CHANNEL_WEAPON,    // WEAPON (3) -> 5
	CHANNEL_VOICE,     // VOICE (4) -> 6
	CHANNEL_ITEM,      // ITEM (5) -> 4
	CHANNEL_BODY,      // BODY (6) -> 3
	CHANNEL_LOCAL,     // LOCAL (7) -> 7
	CHANNEL_MUSIC,     // MUSIC (8) -> 8
	CHANNEL_ANNOUNCER, // ANNOUCER (9) -> 9
	CHANNEL_SHELLSHOCK // SHELLSHOCK (10) -> 10
};

namespace sound {

	SafetyHookInline SND_UpdateD;
	dvar_s* snd_test_volume[11];
	dvar_s* snd_alias_hook;
	void soundmod() {
		snd_local_t* g_snd = (snd_local_t*)0x793A00;

		for(int i = 0; i < 11; i++) {
			g_snd->channelvol->channelvol[i].volume = snd_test_volume[i]->value.decimal;
		}

	}
	char SND_update_Hook() {
		auto result = SND_UpdateD.unsafe_ccall<char>();
		soundmod();
		return result;



	}
	SafetyHookInline SND_SetChannelVolumesD;
	void* __fastcall SND_SetChannelVolumes(float* channelvolume,void* garbage, int a1, int priority) {
		//for (int i = 0; i < 11; i++) {
		//	printf("channelvolume %f\n", channelvolume[i]);
		//}
		return SND_SetChannelVolumesD.unsafe_thiscall<void*>(channelvolume, a1, priority);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override {
			for (int i = 0; i < MAX_CHANNELVOL_ARRAY; i++) {
				auto string = std::string("snd_volume_") + channelNames[i];
				char* cstr = new char[string.length() + 1];
				std::strcpy(cstr, string.c_str());
				snd_test_volume[i] = dvars::Dvar_RegisterFloat(
					cstr,
					1.0f,
					0.0f,
					1.0f,
					0
				);
			}
			snd_alias_hook = dvars::Dvar_RegisterInt("snd_alias_hook", 2,0,3,0);
		}
		void post_start() override {

			if (exe(1)) {
				SND_UpdateD = safetyhook::create_inline(0x4445F0, SND_update_Hook);
				//SND_SetChannelVolumesD = safetyhook::create_inline(0x4431F0, SND_SetChannelVolumes);
				static auto lol = safetyhook::create_mid(0x004431F0, [](SafetyHookContext& ctx) {

					float* channelvolume = (float*)(ctx.ecx);
						for (int i = 0; i < 11; i++) {
							int channelIndex = channelMap[i];
							channelvolume[channelIndex] *= snd_test_volume[i]->value.decimal;
							//printf("channelvolume[%d]: %f\n", channelIndex, channelvolume[channelIndex]);
						}
					});

				static auto Com_SoundAliasChannelForName = safetyhook::create_mid(0x428DA0, [](SafetyHookContext& ctx) {

					if (!snd_alias_hook->value.integer)
						return;
					const char* sourceFile = *(const char**)(ctx.esp + 0x4);
					snd_alias_build_s* alias = *(snd_alias_build_s**)(ctx.esp + 0x8);

					auto aliasName = std::string_view(alias->szAliasName);
					auto soundFile = std::string_view(alias->szSoundFile);

					bool onlyBrokenMusic = snd_alias_hook->value.integer >= 2 ? !strcmp((const char*)ctx.ebx, "music") : true;

					if (onlyBrokenMusic && (aliasName.contains("weap") || aliasName.contains("weapon") || soundFile.contains("weapon")) /* <-- this one is overkill?*/) {
						printf("name %s\nalias %s\nsource %s\n", (const char*)ctx.ebx, alias->szAliasName, alias->szSoundFile);
						ctx.ebx = (uintptr_t)"item";
					}
					if(snd_alias_hook->value.integer == snd_alias_hook->limits.integer.max)
					printf("name %s\nalias %s\nsource %s\n", (const char*)ctx.ebx,alias->szAliasName, alias->szSoundFile);

					});

			}

		}


	};

}
REGISTER_COMPONENT(sound::component);