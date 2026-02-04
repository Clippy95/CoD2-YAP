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
namespace gsc {

	std::unordered_map<std::string, int> ScriptMainHandles;
	std::unordered_map<std::string, int> ScriptInitHandles;

	std::unordered_map<std::string, game::scr_function_t> CustomScrFunctions;
	struct ReplacementInfo {
		const char* replacementPos;
		bool disableOnce;
		bool enabled;
	};
	std::unordered_map<const char*, ReplacementInfo> ReplacedFunctions;
	const char* ReplacedPos = nullptr;


	game::scrVmPub_t* gScrVmPub = (game::scrVmPub_t*)0xA01880;

	void AddFunction(const char* name, game::xfunction_t func, int type = 0)
	{
		game::scr_function_t toAdd;
		toAdd.function = func;
		toAdd.developer = type;

		CustomScrFunctions.insert_or_assign(name, toAdd);
	}
	SafetyHookInline Scr_GetFunctionD;
	game::xfunction_t Scr_GetFunction_Stub(const char** name, int* isDev)
	{
		auto got = CustomScrFunctions.find(*name);

		if (got == CustomScrFunctions.end())
			return Scr_GetFunctionD.unsafe_ccall<game::xfunction_t>(name, isDev);

		*isDev = got->second.developer;
		return got->second.function;
	}

	const char* GetCodePosForParam(int index)
	{
		if (static_cast<unsigned int>(index) >= gScrVmPub->outparamcount)
		{
			printf("errpr");
			return "";
		}

		const auto* value = &gScrVmPub->top[-index];

		if (value->type != game::VAR_FUNCTION)
		{
			printf("errpr2");
			return "";
		}

		return value->u.codePosValue;
	}

	void SetReplacedPos(const char* what, const char* with, bool enabled = true)
	{
		if (!*what || !*with)
		{
			Com_Printf("Invalid parameters passed to ReplacedFunctions\n");
			return;
		}

		if (ReplacedFunctions.contains(what))
		{
			// Update existing entry
			ReplacedFunctions[what].replacementPos = with;
			ReplacedFunctions[what].enabled = enabled;
		}
		else
		{
			ReplacedFunctions[what] = { with, false, enabled };
		}
	}

	void GetReplacedPos(const char* pos)
	{
		if (!pos)
		{
			return;
		}

		if (const auto itr = ReplacedFunctions.find(pos); itr != ReplacedFunctions.end())
		{
			if (!itr->second.enabled)
			{
				// Detour is disabled
				ReplacedPos = nullptr;
				return;
			}

			if (itr->second.disableOnce)
			{
				// Skip the detour this time
				itr->second.disableOnce = false;
				ReplacedPos = nullptr;
			}
			else
			{
				ReplacedPos = itr->second.replacementPos;
			}
		}
	}
	uintptr_t Scr_GetFunctionHandle_addr = 0x45DC30;

	WEAK game::symbol<int(const char* filename)>  Scr_LoadScript{ 0x45E060 };

	WEAK game::symbol<void(char** files,int numfiles)>  FS_SortFileList{ 0x41E8C0 };

	//WEAK game::symbol<int (int handle)>  Scr_ExecThread{ 0x46DA30 };
	WEAK game::symbol<int(uint16_t handle)>  Scr_FreeThread{ 0x464EA0 };

	uintptr_t Scr_ExecThread_addr = 0x46DA30;
	int Scr_ExecThread(int thread, int unknown = 0) {
		int result;
		__asm {
			pushad
			mov eax, thread
			push unknown
			call Scr_ExecThread_addr
			mov result, eax
			add esp, 4
			popad
		}
		return result;
	}

	//WEAK game::symbol<const char** (__fastcall*)(const char* paths, const char* extension, const char* filter0,const char* filter1,int* max_num_files)>  FS_ListFilteredFiles{ 0x41DFC0 };

	char** FS_ListFilteredFiles(const char* paths, const char* extension, const char* filter0, const char* filter1, int* max_num_files) {


		return fastcall_call<char**>(0x41DFC0, paths, extension, filter0, filter1, max_num_files);
	}

	int Scr_GetFunctionHandle(const char* filename, const char* function_name) {
		int result;
		__asm {
			pushad
			mov eax, filename
			push function_name
			call Scr_GetFunctionHandle_addr
			mov result, eax
			add esp,4
			popad
		}
		return result;
	}

	uintptr_t Scr_GetInt_addr = 0x46E4A0;
	int Scr_GetInt(int param) {
		int result;
		__asm {
			push eax
			mov eax, param
			call Scr_GetInt_addr
			mov result, eax
			pop eax
		}
		return result;
	}

	void LoadScript(const std::string& path)
	{
		if (!Scr_LoadScript(path.data()))
		{
			//Com_Printf(va("The script %s encountered an error during the loading process.\n", path.data()));
			return;
		}

		Com_Printf(("The script %s has been loaded successfully.\n", path.data()));

		const auto initHandle = Scr_GetFunctionHandle(path.data(), "init");
		if (initHandle != 0)
		{
			ScriptInitHandles.insert_or_assign(path, initHandle);
		}

		const auto mainHandle = Scr_GetFunctionHandle(path.data(), "main");
		if (mainHandle != 0)
		{
			ScriptMainHandles.insert_or_assign(path, mainHandle);
		}
	}

	void FS_FreeFileList(char** list) {
		int i;



		if (!list) {
			return;
		}

		for (i = 0; list[i]; i++) {
			game::Z_Free(list[i]);
		}

		game::Z_Free(list);
	}

	void LoadScriptsFromIWD() {
		ScriptMainHandles.clear();
		ScriptInitHandles.clear();

		auto mapname = dvars::Dvar_FindVar("mapname")->value.string;

		// Load from maps/custom
		int max_files = 0;
		auto files = FS_ListFilteredFiles(*(const char**)0x1CBAD00, "maps/custom", "gsc", NULL, &max_files);
		FS_SortFileList(files, max_files);

		for (int i = 0; i < max_files; i++) {
			std::string filepath(files[i]);

			// Check if file ends with .gsc
			if (filepath.length() < 4 || filepath.substr(filepath.length() - 4) != ".gsc") {
				continue;
			}

			std::string prefix = "maps/custom/";
			if (filepath.find(prefix) == 0) {
				std::string remainder = filepath.substr(prefix.length());

				// Find the first slash to check if it's in a subdirectory
				size_t slashPos = remainder.find('/');

				bool shouldLoad = false;

				if (slashPos == std::string::npos) {
					// No slash = file directly in maps/custom/, always load
					shouldLoad = true;
				}
				else {
					// File is in a subdirectory, check if directory matches mapname
					std::string dirname = remainder.substr(0, slashPos);
					if (dirname == mapname) {
						shouldLoad = true;
					}
				}

				if (shouldLoad) {
					// Remove .gsc extension
					std::string scriptPath = filepath.substr(0, filepath.length() - 4);
					printf("Loading script: %s\n", scriptPath.c_str());
					LoadScript(scriptPath);
				}
			}
		}
		FS_FreeFileList(files);
	}
	uintptr_t Path_AutoDisconnectPaths_addr;
	int Path_AutoDisconnectPaths_Scr_LoadLevel() {

		for (const auto& handle : ScriptMainHandles)
		{
			const auto thread = Scr_ExecThread(handle.second);
			Scr_FreeThread(static_cast<std::uint16_t>(thread));
		}
		auto result = cdecl_call<int>(Path_AutoDisconnectPaths_addr);
		return result;
	}
	int Scr_GetNumParams() {
		return gScrVmPub[0].outparamcount;
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override {
			if (!exe(1))
				return;

		}
		void post_start() override {
			if (!exe(1))
				return;

			//Memory::VP::InterceptCall(exe(0x4DA4FF), Path_AutoDisconnectPaths_addr, Path_AutoDisconnectPaths_Scr_LoadLevel);

			static auto G_LoadLevel_runframe = safetyhook::create_mid(exe(0x4DA524), [](SafetyHookContext& ctx) {

				for (const auto& handle : ScriptInitHandles)
				{
					const auto thread = Scr_ExecThread(handle.second);
					Scr_FreeThread(static_cast<std::uint16_t>(thread));
				}

				});

			static auto GScr_LoadScriptsForEntities = safetyhook::create_mid(0x524570, [](SafetyHookContext& ctx) {

				LoadScriptsFromIWD();

				});
			Scr_GetFunctionD = safetyhook::create_inline(exe(0x4F8B60), Scr_GetFunction_Stub);
			static auto SV_ShutdownGameVM = safetyhook::create_mid(exe(0x4489A0), [](SafetyHookContext& ctx) {

				int clearScripts = ctx.edi;

				if (clearScripts) {
					ReplacedFunctions.clear();
				}

				});

			gsc::AddFunction("detour_make", [] // gsc: detour_make(<original>, <detour>, [enabled])
				{
					const auto what = GetCodePosForParam(0);
					const auto with = GetCodePosForParam(1);

					bool enabled = true; // Default to enabled

					if (Scr_GetNumParams() >= 3)
					{
						enabled = Scr_GetInt(2) != 0;
					}

					SetReplacedPos(what, with, enabled);
				});


			gsc::AddFunction("detour_enable", [] // gsc: detour_enable(<original>)
				{
					const auto what = GetCodePosForParam(0);

					if (!*what)
					{
						Com_Printf("Invalid parameter passed to detour_enable\n");
						return;
					}

					if (const auto itr = ReplacedFunctions.find(what); itr != ReplacedFunctions.end())
					{
						itr->second.enabled = true;
					}
					else
					{
						Com_Printf("Function not found in ReplacedFunctions\n");
					}
				});

			gsc::AddFunction("detour_disable", [] // gsc: detour_disable(<original>)
				{
					const auto what = GetCodePosForParam(0);

					if (!*what)
					{
						Com_Printf("Invalid parameter passed to detour_disable\n");
						return;
					}

					if (const auto itr = ReplacedFunctions.find(what); itr != ReplacedFunctions.end())
					{
						itr->second.enabled = false;
					}
					else
					{
						Com_Printf("Function not found in ReplacedFunctions\n");
					}
				});

			gsc::AddFunction("detour_disableonce", [] // gsc: DisableDetourOnce(<function>)
				{
					const auto what = GetCodePosForParam(0);

					if (!*what)
					{
						Com_Printf("Invalid parameter passed to DisableDetourOnce\n");
						return;
					}

					if (const auto itr = ReplacedFunctions.find(what); itr != ReplacedFunctions.end())
					{
						itr->second.disableOnce = true;
					}
					else
					{
						Com_Printf("Function not found in ReplacedFunctions\n");
					}
				});

			static auto VM_ExecuteInternal_midhook = safetyhook::create_mid(exe(0x469451), [](SafetyHookContext& ctx) {

				const char* pos = (const char*)(ctx.esi);

				//auto pos = gScrVmPub->function_frame->fs.pos;

				GetReplacedPos(pos);
				if (ReplacedPos) {
					//printf("found a ReplcaedPos!!!\n");
					ctx.esi = (uintptr_t)ReplacedPos;
					ReplacedPos = nullptr;
				}

				});

		}


	};
}
REGISTER_COMPONENT(gsc::component);