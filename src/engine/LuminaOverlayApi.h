/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The C interface of app_server's optional overlay compositor (added by
 * Lumina's patch set, see drawing/OverlayCompositor.h). An identical copy
 * of this header ships with Lumina's Window Engine, which resolves
 * lumina_overlay_get_api() at run time with get_image_symbol(): on an
 * app_server without the patch the symbol is simply absent and the engine
 * keeps working without overlays.
 *
 * Compatibility rules: `version` only grows; new functions are appended to
 * lumina_overlay_api; lumina_overlay_params carries its own `size` and new
 * fields are appended.
 */
#ifndef LUMINA_OVERLAY_API_H
#define LUMINA_OVERLAY_API_H

#include <GraphicsDefs.h>
#include <Rect.h>
#include <SupportDefs.h>

class Desktop;
class Window;

#define LUMINA_OVERLAY_API_VERSION	1
#define LUMINA_OVERLAY_API_SYMBOL	"lumina_overlay_get_api"

struct lumina_overlay_params {
	uint32		size;				// sizeof(lumina_overlay_params)
	float		opacity;			// content opacity 0..1
	int32		blur_radius;		// backdrop blur, screen pixels
	int32		downsample;			// 1, 2 or 4
	rgb_color	tint;
	float		tint_amount;		// 0..1
	float		corner_radius;
	int32		shadow_radius;
	float		shadow_opacity;
	int32		shadow_offset_y;
};

struct lumina_overlay_api {
	uint32		version;			// LUMINA_OVERLAY_API_VERSION
	// Makes `window` an overlay with `params`, or a normal window again
	// (params == NULL). The caller may hold the desktop window lock (it is
	// recursive); the window's first child view must carry a 32-bit view
	// bitmap — that bitmap is the overlay's content.
	status_t	(*set_overlay)(Desktop* desktop, Window* window,
					const lumina_overlay_params* params);
	// The overlay's bitmap content changed inside `frame` (screen coords).
	void		(*damage)(Desktop* desktop, BRect frame);
	bool		(*is_overlay)(Window* window);
};

typedef const lumina_overlay_api* (*lumina_overlay_get_api_func)();

extern "C" __attribute__((visibility("default")))
const lumina_overlay_api* lumina_overlay_get_api();

#endif	// LUMINA_OVERLAY_API_H
