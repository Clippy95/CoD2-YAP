#pragma once
#include <helper.hpp>
#include "..\loader\component_loader.h"
#include <Hooking.Patterns.h>
#include "..\framework.h"
#define WEAK __declspec(selectany)

extern uintptr_t gfx_d3d_dll;
namespace game
{


	template <typename T>
	class symbol
	{
	public:
		symbol(const size_t sp_address, const size_t mp_address = 0, const ptrdiff_t offset = 0) :
			sp_object(reinterpret_cast<T*>(sp_address)),
			mp_object(reinterpret_cast<T*>(mp_address)),
			offset(offset)
		{
		}

		T* get() const
		{
			T* ptr = sp_object;
			uintptr_t base_address = 0;

			switch (offset)
			{
			case BASE_GFX:
				base_address = gfx_d3d_dll;
				break;
			default:
				return ptr;
			}

			return reinterpret_cast<T*>(base_address + (reinterpret_cast<uintptr_t>(ptr) - offset));
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}
	private:
		T* sp_object;
		T* mp_object;
		ptrdiff_t offset;
	};

	enum game_module {
		EXE,
		GFX,
		MAX_GAME_MODULE,
	};

	template <typename T>
	class symbolp
	{
	public:
		symbolp(std::initializer_list<std::string_view> patterns, const signed int offset_val = 0, game_module module = EXE, bool dereference = false)
			: patterns(patterns), offset_val(offset_val), module(module), dereference(dereference), object(nullptr)
		{
			register_as_component();
		}

		symbolp(std::string_view pattern_str, const signed int offset_val = 0, game_module module = EXE, bool dereference = false)
			: patterns({ pattern_str }), offset_val(offset_val), module(module), dereference(dereference), object(nullptr)
		{
			register_as_component();
		}

		T* get() const
		{
			return object;
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}

		//bool operator==(const T& other) const {
		//    return (this->get() == other);
		//}

	private:
		void register_as_component()
		{
			class symbolp_resolver : public component_interface
			{
			public:
				symbolp_resolver(symbolp* owner, game_module module) : owner(owner), module(module) {}

				void post_start() override
				{
					if (module == EXE) {
						owner->resolve();
					}
				}

				void post_gfx() override {
					if (module == GFX)
						owner->resolve();
				}

			private:
				symbolp* owner;
				game_module module;
			};

			auto resolver = std::make_unique<symbolp_resolver>(this, module);
			component_loader::register_component(std::move(resolver));
		}

		void resolve()
		{
			uintptr_t search_base = (uintptr_t)GetModuleHandle(NULL);

			switch (module)
			{
			case BASE_GFX:
				search_base = gfx_d3d_dll;
				break;
			}


			for (const auto& pattern_str : patterns)
			{
				hook::pattern pattern((HMODULE)search_base, pattern_str);

				if (!pattern.empty())
				{
					uintptr_t address = reinterpret_cast<uintptr_t>(pattern.get_first()) + offset_val;

					if (dereference)
					{
						object = *reinterpret_cast<T**>(address);
					}
					else
					{
						object = reinterpret_cast<T*>(address);
					}
					return;
				}
			}

			// All patterns failed - build error message
			object = nullptr;

			std::string error_msg = "symbolp: All patterns failed to resolve!\nPatterns tried:\n";
			for (size_t i = 0; i < patterns.size(); ++i)
			{
				error_msg += std::to_string(i + 1) + ": \"" + std::string(patterns[i]) + "\"\n";
			}

			MessageBoxA(NULL, error_msg.c_str(), MOD_NAME": Error", MB_ICONSTOP);
		}

		std::vector<std::string_view> patterns;
		signed int offset_val;
		game_module module;
		bool dereference;
		mutable T* object;
	};

}

namespace game {
	//void __cdecl game::Cmd_AddCommand(const char* command_name, void* function);

	//WEAK symbol<qhandle_t> whiteShader{ 0x48422E8 };

	WEAK symbol<void(const char* command_name, void(__cdecl* function)())> Cmd_AddCommand{ 0x41BB00 };



}

//struct playerstate_ext {
//	vec3_t Oldorigin;
//};
//
////playerstate_ext pstates_ext[MAX_CLIENTS]{};
//
//inline std::unordered_map<void*, playerstate_ext> g_psExtensions;
//
//inline playerstate_ext* get_ps_ext(void* ps) {
//	return &g_psExtensions[ps];
//}

WEAK game::symbol<int (const char *Format, ...)> Com_Printf{ 0x42C130 };

extern void retptr();
