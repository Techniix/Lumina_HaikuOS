/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "LuminaDecorator.h"

#include <new>

#include "LuminaEngine.h"

namespace lumina {
namespace engine {

LuminaDecorator::LuminaDecorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	DefaultDecorator(settings, frame, desktop)
{
}


LuminaDecorator::~LuminaDecorator()
{
}


LuminaDecorAddOn::LuminaDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name),
	fEngine(new (std::nothrow) LuminaEngine())
{
	// app_server registers every listener in this list when the decorator is
	// selected and unregisters them when another decorator is selected.
	if (fEngine != NULL)
		fDesktopListeners.AddItem(fEngine);
}


LuminaDecorAddOn::~LuminaDecorAddOn()
{
	fDesktopListeners.RemoveItem(fEngine);
	delete fEngine;
}


status_t
LuminaDecorAddOn::InitCheck() const
{
	// Even without the engine (allocation failure) the decorator itself is
	// fully functional, so never fail here: failing would make app_server
	// reject the add-on and keep the previous decorator, which is fine, but
	// a working pass-through is the friendlier outcome.
	return B_OK;
}


Decorator*
LuminaDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) LuminaDecorator(settings, rect, desktop);
}

}	// namespace engine
}	// namespace lumina


extern "C" DecorAddOn*
instantiate_decor_addon(image_id id, const char* name)
{
	return new (std::nothrow) lumina::engine::LuminaDecorAddOn(id, name);
}
