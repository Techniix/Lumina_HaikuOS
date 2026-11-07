/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef LUMINA_PANEL_PROTOCOL_H
#define LUMINA_PANEL_PROTOCOL_H

#include <SupportDefs.h>

namespace lumina {
namespace panel {

enum {
	kMsgLaunchRef			= 'PLch',	// "refs"
	kMsgOpenModePreflet		= 'PMod',
	kMsgPollWindows			= 'PPol',
	kMsgRefreshSkin			= 'PSkn',
	kMsgReloadConfig		= 'PCfg',
};

}	// namespace panel
}	// namespace lumina

#endif
