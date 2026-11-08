/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Level 3 bridge: overlay windows through app_server's optional overlay
 * compositor (patches/haiku/0001-app_server-optional-overlay-compositor).
 *
 * The compositor is found at run time — get_image_symbol() on the
 * app_server image — never at link time, so this add-on loads on a stock
 * app_server exactly as before and just reports "compositor": false.
 */
#include "LuminaEngine.h"

#include <image.h>
#include <string.h>
#include <syslog.h>

#include "Desktop.h"
#include "ServerWindow.h"
#include "Window.h"

#include "LuminaOverlayApi.h"

#include <lumina/Json.h>
#include <lumina/haiku/Protocol.h>

using namespace lumina::haiku;

namespace lumina {
namespace engine {

namespace {

const lumina_overlay_api*
ResolveOverlayApi()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type != B_APP_IMAGE)
			continue;
		void* symbol = NULL;
		if (get_image_symbol(info.id, LUMINA_OVERLAY_API_SYMBOL,
				B_SYMBOL_TYPE_TEXT, &symbol) != B_OK || symbol == NULL)
			return NULL;
		const lumina_overlay_api* api
			= ((lumina_overlay_get_api_func)symbol)();
		if (api == NULL || api->version < 1 || api->set_overlay == NULL
			|| api->damage == NULL)
			return NULL;
		return api;
	}
	return NULL;
}

}	// namespace


void
LuminaEngine::_ResolveCompositor()
{
	fOverlayApi = ResolveOverlayApi();
	syslog(LOG_INFO, "lumina-engine: overlay compositor %s",
		fOverlayApi != NULL ? "available (patched app_server)"
			: "not available (stock app_server)");
}


void
LuminaEngine::_HandleOverlayCommand(const BString& op, const BMessage& command)
{
	if (fOverlayApi == NULL)
		return;
	int32 token = command.GetInt32(LUMINA_ENGINE_FIELD_TOKEN, -1);
	Window* window = _FindWindow(token);
	// Only Lumina's own windows may become overlays: an overlay takes no
	// screen area, so a foreign window turned into one would misbehave.
	if (window == NULL || !_IsLuminaClient(window))
		return;

	if (op == LUMINA_OP_OVERLAY_DAMAGE) {
		BRect frame;
		if (command.FindRect(LUMINA_ENGINE_FIELD_FRAME, &frame) != B_OK)
			frame = window->Frame();
		fOverlayApi->damage(fDesktop, frame & window->Frame());
		return;
	}
	if (op == LUMINA_OP_OVERLAY_OFF) {
		fOverlayApi->set_overlay(fDesktop, window, NULL);
		fOverlays.erase(token);
		return;
	}

	// LUMINA_OP_OVERLAY: JSON parameters, all optional.
	lumina_overlay_params params;
	memset(&params, 0, sizeof(params));
	params.size = sizeof(params);
	params.opacity = 1.0f;
	params.downsample = 2;
	Value doc;
	JsonError error;
	const char* json = command.GetString(LUMINA_ENGINE_FIELD_JSON, "{}");
	if (ParseJson(json, doc, &error) && doc.IsObject()) {
		auto number = [&](const char* key, double fallback) {
			const Value* v = doc.Find(key);
			return v != NULL ? v->AsNumber(fallback) : fallback;
		};
		params.opacity = float(std::min(std::max(number("opacity", 1), 0.0),
			1.0));
		params.blur_radius = int32(std::min(std::max(number("blur", 0), 0.0),
			64.0));
		int32 downsample = int32(number("downsample", 2));
		params.downsample = downsample == 1 || downsample == 4 ? downsample : 2;
		params.tint_amount = float(std::min(std::max(number("tintAmount", 0),
			0.0), 1.0));
		uint32 tint = uint32(number("tint", 0xdcdcdc));
		params.tint.red = uint8((tint >> 16) & 0xff);
		params.tint.green = uint8((tint >> 8) & 0xff);
		params.tint.blue = uint8(tint & 0xff);
		params.tint.alpha = 255;
		params.corner_radius = float(std::min(std::max(number("radius", 0),
			0.0), 64.0));
		params.shadow_radius = int32(std::min(std::max(number("shadow", 0),
			0.0), 64.0));
		params.shadow_opacity = float(std::min(std::max(
			number("shadowOpacity", 0.35), 0.0), 1.0));
		params.shadow_offset_y = int32(std::min(std::max(
			number("shadowOffsetY", 2), -32.0), 32.0));
	}
	if (fOverlayApi->set_overlay(fDesktop, window, &params) == B_OK)
		fOverlays.insert(token);
}


void
LuminaEngine::_ResetOverlays()
{
	if (fOverlayApi == NULL)
		return;
	for (int32 token : fOverlays) {
		Window* window = _FindWindow(token);
		if (window != NULL)
			fOverlayApi->set_overlay(fDesktop, window, NULL);
	}
	fOverlays.clear();
}

}	// namespace engine
}	// namespace lumina
