#pragma once
#include "..\framework.h"
#include <helper.hpp>
#include <string>

class component_interface
{
public:
	virtual ~component_interface()
	{
	}

	virtual void post_start()
	{
	}

	virtual void post_load()
	{
	}

	virtual void pre_destroy()
	{
	}

	virtual void post_unpack()
	{
	}

	virtual void post_gfx()
	{
	}

	virtual void post_cg_init()
	{
	}

	virtual void after_item_parse(itemDef_s* item)
	{
	}

	virtual void* load_import([[maybe_unused]] const std::string& library, [[maybe_unused]] const std::string& function)
	{
		return nullptr;
	}

	virtual bool is_supported()
	{
		return true;
	}
};