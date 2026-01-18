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

struct kbutton_t
{
	uint64_t down;
	uint32_t downtime;
	int32_t msec;
	int8_t active;
	int8_t wasPressed;
};

namespace sprint {

	void __cdecl Cmd_AddCommand(const char* command_name, void(__cdecl* function)()) {

		printf("command name %s func %p\n", command_name, function);

		cdecl_call(0x41BB00, command_name, function);
	}

	void ESICall(void* arg1,uintptr_t addr) {
		__asm {
			pushad
			mov esi,arg1
			call addr
			popad
		}
	}

	void IN_KeyUp(kbutton_t* key) {
		ESICall(key, 0x408BD0);
	}

	void IN_KeyDown(kbutton_t* key) {
		ESICall(key, 0x408B50);
	}
	kbutton_t sprint;
	void IN_SprintDown() {
		printf("sprint down %p", &sprint);
		IN_KeyDown(&sprint);
	}

	void IN_SprintUp() {
		IN_KeyUp(&sprint);
	}

	uintptr_t setup_binds_og;
	int setup_binds() {

		Cmd_AddCommand("+sprint", IN_SprintDown);
		Cmd_AddCommand("-sprint", IN_SprintUp);

		return cdecl_call<int>(setup_binds_og);
	}
	class component final : public component_interface
	{
	public:

		void post_start() override {

			Memory::VP::InterceptCall(0x40E164, setup_binds_og, setup_binds);

		}


	};

}
REGISTER_COMPONENT(sprint::component);

