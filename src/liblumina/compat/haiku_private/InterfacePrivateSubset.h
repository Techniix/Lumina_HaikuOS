/*
 * Subset of Haiku's headers/private/interface/InterfacePrivate.h.
 * Copyright Haiku, Inc. Distributed under the terms of the MIT License.
 */
#ifndef LUMINA_COMPAT_INTERFACE_PRIVATE_SUBSET_H
#define LUMINA_COMPAT_INTERFACE_PRIVATE_SUBSET_H

#include <SupportDefs.h>

class BString;

namespace BPrivate {
void		get_workspaces_layout(uint32* _columns, uint32* _rows);
void		set_workspaces_layout(uint32 columns, uint32 rows);
bool		get_control_look(BString& path);
status_t	set_control_look(const BString& path);
}	// namespace BPrivate

#endif
