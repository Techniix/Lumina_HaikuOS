/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The decorator half of the Lumina Window Engine add-on.
 *
 * In the "haiku" style (default, Haiku Classic) it IS app_server's
 * DefaultDecorator: no method is overridden, so window tabs, borders,
 * gradients and buttons are pixel-identical to stock Haiku by construction.
 * The add-on exists to host the DesktopListener (LuminaEngine); the
 * decorator merely has to be indistinguishable.
 */
#ifndef LUMINA_ENGINE_DECORATOR_H
#define LUMINA_ENGINE_DECORATOR_H

#include <InterfaceDefs.h>
#include <Window.h>

#include "DecorManager.h"
#include "DefaultDecorator.h"

namespace lumina {
namespace engine {

class LuminaEngine;

class LuminaDecorator : public DefaultDecorator {
public:
	LuminaDecorator(DesktopSettings& settings, BRect frame, Desktop* desktop);
	virtual ~LuminaDecorator();
};


class LuminaDecorAddOn : public DecorAddOn {
public:
	LuminaDecorAddOn(image_id id, const char* name);
	virtual ~LuminaDecorAddOn();

	virtual status_t InitCheck() const;

protected:
	virtual Decorator* _AllocateDecorator(DesktopSettings& settings,
		BRect rect, Desktop* desktop);

private:
	LuminaEngine*	fEngine;
};

}	// namespace engine
}	// namespace lumina

#endif
