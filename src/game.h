#pragma once
#include <helper.hpp>
#include "..\loader\component_loader.h"
#include <Hooking.Patterns.h>
#include "..\framework.h"
#include "MemoryMgr.h"
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
			uint32_t value = 0;
			Memory::VP::Read(0x00434815, value);

			 if (value == 0x3800F08F) {
				 ptr = mp_object;
			}

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

	WEAK symbol<void(const char* command_name, void(__cdecl* function)())> Cmd_AddCommand{ 0x41BB00,0x4212F0 };
	WEAK symbol<void(void* Block)> Z_Free_internal{ 0x4266A0,0x42C2C0 };
	WEAK symbol<const char*(const char* string)> String_Alloc{ 0x4D4570,0x547150 };

	WEAK symbol<void* (uint32_t size, uint32_t alignment)> Hunk_AllocAlignInternal{ 0x426F30,0x42CB70 };

	inline void* UI_Alloc(uint32_t size, uint32_t alignment) {
		return game::Hunk_AllocAlignInternal(size, alignment);
	}

	inline void Z_Free(void* Block) {
		Z_Free_internal(Block);
	}

	inline void Z_Free(const void* Block) {
		Z_Free((void*)Block);
	}
	extern const char* UI_SafeTranslateString(const char* string);

	inline const char* UI_SafeTranslateString(const char* string, const char* fallback, bool& found) 
	{

		auto result = UI_SafeTranslateString(string);

		if (fallback) {
			found = strcmp(result, string) != 0;

			if (!found) {
				result = UI_SafeTranslateString(fallback);
				if (strcmp(fallback, result)) {
					result = fallback;
				}
			}

		}
		return result;
	}

	// expects @ at the start, RETURN ISN'T ISN'T MEMORY SAFE!!
	inline const char* Menu_SafeTranslateString(const char* string, const char* fallback)
	{

		uintptr_t ptr = (uintptr_t)string;
		bool found = false;
		auto result = UI_SafeTranslateString((const char*)(ptr + 1), fallback, found);
		printf("found %d\n", found);
		if (found)
			return string;
		return fallback;
		
	}

    enum {
        K_TAB = 9,
        K_ENTER = 13,
        K_ESCAPE = 27,
        K_SPACE = 32,
        K_BACKSPACE = 127,
        K_UPARROW = 154,
        K_DOWNARROW = 155,
        K_LEFTARROW = 156,
        K_RIGHTARROW = 157,
        K_ALT = 158,
        K_CTRL = 159,
        K_SHIFT = 160,
        K_CAPSLOCK = 151,
        K_F1 = 167,
        K_F2 = 168,
        K_F3 = 169,
        K_F4 = 170,
        K_F5 = 171,
        K_F6 = 172,
        K_F7 = 173,
        K_F8 = 174,
        K_F9 = 175,
        K_F10 = 176,
        K_F11 = 177,
        K_F12 = 178,
        K_F13 = 179,
        K_F14 = 180,
        K_F15 = 181,
        K_INS = 161,
        K_DEL = 162,
        K_PGDN = 163,
        K_PGUP = 164,
        K_HOME = 165,
        K_END = 166,
        K_MOUSE1 = 200,
        K_MOUSE2 = 201,
        K_MOUSE3 = 202,
        K_MOUSE4 = 203,
        K_MOUSE5 = 204,
        K_MWHEELUP = 206,
        K_MWHEELDOWN = 205,
        K_AUX1 = 207,
        K_AUX2 = 208,
        K_AUX3 = 209,
        K_AUX4 = 210,
        K_AUX5 = 211,
        K_AUX6 = 212,
        K_AUX7 = 213,
        K_AUX8 = 214,
        K_AUX9 = 215,
        K_AUX10 = 216,
        K_AUX11 = 217,
        K_AUX12 = 218,
        K_AUX13 = 219,
        K_AUX14 = 220,
        K_AUX15 = 221,
        K_AUX16 = 222,
        K_KP_HOME = 182,
        K_KP_UPARROW = 183,
        K_KP_PGUP = 184,
        K_KP_LEFTARROW = 185,
        K_KP_5 = 186,
        K_KP_RIGHTARROW = 187,
        K_KP_END = 188,
        K_KP_DOWNARROW = 189,
        K_KP_PGDN = 190,
        K_KP_ENTER = 191,
        K_KP_INS = 192,
        K_KP_DEL = 193,
        K_KP_SLASH = 194,
        K_KP_MINUS = 195,
        K_KP_PLUS = 196,
        K_KP_NUMLOCK = 197,
        K_KP_STAR = 198,
        K_KP_EQUALS = 199,
        K_PAUSE = 153,
        K_SEMICOLON = 59,
        K_COMMAND = 150,
        K_181 = 128,
        K_191 = 129,
        K_223 = 130,
        K_224 = 131,
        K_225 = 132,
        K_228 = 133,
        K_229 = 134,
        K_230 = 135,
        K_231 = 136,
        K_232 = 137,
        K_233 = 138,
        K_236 = 139,
        K_241 = 140,
        K_242 = 141,
        K_243 = 142,
        K_246 = 143,
        K_248 = 144,
        K_249 = 145,
        K_250 = 146,
        K_252 = 147,
    };

    struct binding_s
    {
        const char* binding_command;
        int key1_default;
        int key2_default;
        int key1;
        int key2;
    };

	extern binding_s* GetModdedBindings(uint32_t& current_size);

	struct VariableStackBuffer
	{
		const char* pos;
		unsigned __int16 size;
		unsigned __int16 bufLen;
		unsigned __int16 localId;
		unsigned __int8 time;
		char buf[1];
	};

	union VariableUnion
	{
		int intValue;
		float floatValue;
		unsigned int stringValue;
		const float* vectorValue;
		const char* codePosValue;
		unsigned int pointerValue;
		VariableStackBuffer* stackValue;
		unsigned int entityOffset;
	};

	enum VariableType : __int32
	{
		VAR_UNDEFINED = 0x0,
		VAR_POINTER = 0x1,
		VAR_STRING = 0x2,
		VAR_ISTRING = 0x3,
		VAR_VECTOR = 0x4,
		VAR_FLOAT = 0x5,
		VAR_INTEGER = 0x6,
		VAR_CODEPOS = 0x7,
		VAR_PRECODEPOS = 0x8,
		VAR_FUNCTION = 0x9,
		VAR_STACK = 0xA,
		VAR_ANIMATION = 0xB,
		VAR_DEVELOPER_CODEPOS = 0xC,
		VAR_THREAD = 0xD,
		VAR_NOTIFY_THREAD = 0xE,
		VAR_TIME_THREAD = 0xF,
		VAR_CHILD_THREAD = 0x10,
		VAR_OBJECT = 0x11,
		VAR_DEAD_ENTITY = 0x12,
		VAR_ENTITY = 0x13,
		VAR_ARRAY = 0x14,
		VAR_DEAD_THREAD = 0x15,
		VAR_COUNT = 0x16,
		VAR_THREAD_LIST = 0x17,
		VAR_ENDON_LIST = 0x18,
	};


	struct VariableValue
	{
		VariableUnion u;
		VariableType type;
	};


	struct function_stack_t
	{
		const char* pos;
		unsigned int localId;
		unsigned int localVarCount;
		VariableValue* top;
		VariableValue* startTop;
	};


	struct function_frame_t
	{
		function_stack_t fs;
		VariableType topType;
	};


	struct scrVmPub_t
	{
		uint32_t *localVars;
		VariableValue* maxstack;
		uint32_t function_count;
		function_frame_t* function_frame;
		VariableValue* top;
		bool debugCode;
		bool abort_on_error;
		bool terminal_error;
		char pad0[1];
		uint32_t inparamcount;
		uint32_t outparamcount;
		function_frame_t function_frame_start[32];
		VariableValue stack[2048];
	};

	typedef unsigned __int16 scr_string_t;

	struct scr_entref_t
	{
		scr_string_t entnum;
		scr_string_t classnum;
	};

	typedef void(*xfunction_t)();
	typedef void (*xmethod_t)(scr_entref_t);

	typedef struct scr_function_s
	{
		struct scr_function_s* next;
		char* name;
		xfunction_t		function;
		bool		developer;
	} scr_function_t;

	typedef struct scr_method_s
	{
		struct scr_method_s* next;
		char* name;
		xmethod_t function;
		bool developer;
	} scr_method_t;

	inline uintptr_t Scr_GetFunc_addr = 0x46EB20;
	inline void* Scr_GetFunc(int index) {
		void* result;
		__asm {
			mov eax, index
			call Scr_GetFunc_addr
			mov result,eax
		}
		return result;
	}
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

WEAK game::symbol<int (const char *Format, ...)> Com_Printf{ 0x42C130,0x431EE0 };

WEAK game::symbol<void(int code,const char* Format, ...)> Com_Error{ 0x42C7F0 };

WEAK game::symbol<const char*(const char* Format, ...)> va{ 0x43F990 };

enum errorParm_e
{
	// Show message box, close window, show console window
	ERR_FATAL = 0x0,
	// Show ingame error
	ERR_DROP = 0x1,
	ERR_SERVERDISCONNECT = 0x2,
	ERR_DISCONNECT = 0x3,
	ERR_SCRIPT = 0x4,
	ERR_SCRIPT_DROP = 0x5,
	ERR_LOCALIZATION = 0x6,
	ERR_MAPLOADERRORSUMMARY = 0x7,
};

extern void retptr();
