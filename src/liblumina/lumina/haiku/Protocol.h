/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Native IPC protocol: BMessage codes and field names shared by
 * lumina_server, the panel, the CLI, the mode switcher and the Window Engine.
 *
 * Conventions
 *  - Requests that expect an answer are sent with SendMessage(&msg, &reply,
 *    timeout); the reply's "status" (int32 status_t) and optional "error"
 *    (string) report the outcome.
 *  - Config values travel as JSON text in the "json" field so arbitrary
 *    trees survive unchanged; hot paths (window events) use typed fields.
 */
#ifndef LUMINA_HAIKU_PROTOCOL_H
#define LUMINA_HAIKU_PROTOCOL_H

#include <SupportDefs.h>

namespace lumina {
namespace haiku {

// Application signatures
#define LUMINA_SERVER_SIGNATURE		"application/x-vnd.Lumina-Server"
#define LUMINA_PANEL_SIGNATURE		"application/x-vnd.Lumina-Panel"
#define LUMINA_MODE_SIGNATURE		"application/x-vnd.Lumina-Mode"
#define LUMINA_CLI_SIGNATURE		"application/x-vnd.Lumina-CLI"
#define LUMINA_PROFILE_MIME_TYPE	"application/x-vnd.Lumina-Profile"

// launch_daemon job names
#define LUMINA_DESKBAR_JOB			"x-vnd.Be-TSKB"
#define LUMINA_SERVER_JOB			"x-vnd.Lumina-Server"
#define LUMINA_PANEL_JOB			"x-vnd.Lumina-Panel"

const int32 kProtocolVersion = 1;

enum {
	// client → server
	LUMINA_SUBSCRIBE		= 'Lsub',	// "topic" (string, repeatable)
	LUMINA_UNSUBSCRIBE		= 'Luns',
	LUMINA_CONFIG_GET		= 'Lget',	// "path" → reply "json"
	LUMINA_CONFIG_SET		= 'Lset',	// "path", "json"[, "preview" bool,
										//   "revert_after" int64 usecs]
	LUMINA_CONFIG_UNSET		= 'Lcun',	// "path"
	LUMINA_CONFIG_CONFIRM	= 'Lcnf',	// confirm a previewed change
	LUMINA_MODE_SET			= 'Lmod',	// "mode": stock|lumina,
										// ["personality"]
	LUMINA_STATUS_GET		= 'Lsta',	// → reply: mode, personality, engine…
	LUMINA_DIAG_GET			= 'Ldia',	// → reply: "log" strings, counters
	LUMINA_PROFILE_OP		= 'Lprf',	// "op": list|apply|save|duplicate|
										// remove|export|import, args
	LUMINA_WINDOW_LIST_GET	= 'Lwls',	// → reply: repeated window fields
	LUMINA_WINDOW_COMMAND	= 'Lwcm',	// forwarded to engine (see below)
	LUMINA_RESERVE_AREA		= 'Lres',	// "owner", "screen", "edge",
										// "thickness" (0 = release)
	LUMINA_RESTORE_STOCK	= 'Lrst',	// emergency: restore stock Haiku
	LUMINA_WORKSPACE_SWITCH	= 'Lwsw',	// "index" (with transition if enabled)
	LUMINA_PING				= 'Lpng',

	// server → subscribers
	LUMINA_CONFIG_CHANGED	= 'Lcfg',	// "path" (repeatable), "generation"
	LUMINA_WINDOWS_CHANGED	= 'Lwch',	// "op", "token", window fields
	LUMINA_MODE_CHANGED		= 'Lmch',	// "mode", "personality"
	LUMINA_ENGINE_STATUS	= 'Lest',	// "loaded", "abi", "reason"

	// engine ↔ client (through BWindow::Set/GetDecoratorSettings)
	LUMINA_ENGINE_COMMAND	= 'Lecm',
	LUMINA_ENGINE_EVENT		= 'Leev',
};

// Topics for LUMINA_SUBSCRIBE
#define LUMINA_TOPIC_CONFIG			"config"
#define LUMINA_TOPIC_WINDOWS		"windows"
#define LUMINA_TOPIC_MODE			"mode"
#define LUMINA_TOPIC_ENGINE			"engine"

// Common fields
#define LUMINA_FIELD_STATUS			"status"
#define LUMINA_FIELD_ERROR			"error"
#define LUMINA_FIELD_JSON			"json"
#define LUMINA_FIELD_PATH			"path"
#define LUMINA_FIELD_TOPIC			"topic"
#define LUMINA_FIELD_TARGET			"target"	// BMessenger to notify

// ---- Window Engine wire format -------------------------------------------
//
// Commands are delivered by calling BWindow::SetDecoratorSettings() on any
// window owned by the caller with a message whose `what` is
// LUMINA_ENGINE_COMMAND. app_server hands every decorator-settings message
// to each DesktopListener; the Lumina listener consumes ours and ignores the
// rest. Status is read back with BWindow::GetDecoratorSettings(), into which
// the listener adds the fields below. Both calls are public BWindow API.

const int32 kEngineProtocolVersion = 1;
#define LUMINA_ENGINE_LISTENER_ID	'Lumi'

#define LUMINA_ENGINE_FIELD_OP			"lumina:op"
#define LUMINA_ENGINE_FIELD_TOKEN		"lumina:token"
#define LUMINA_ENGINE_FIELD_FRAME		"lumina:frame"
#define LUMINA_ENGINE_FIELD_FROM		"lumina:from"
#define LUMINA_ENGINE_FIELD_DURATION	"lumina:duration"	// float seconds
#define LUMINA_ENGINE_FIELD_CURVE		"lumina:curve"		// string
#define LUMINA_ENGINE_FIELD_RESIZE		"lumina:resize"		// live|throttled|final
#define LUMINA_ENGINE_FIELD_FPS			"lumina:fps"
#define LUMINA_ENGINE_FIELD_TARGET		"lumina:target"		// BMessenger
#define LUMINA_ENGINE_FIELD_WORKSPACE	"lumina:workspace"
#define LUMINA_ENGINE_FIELD_JSON		"lumina:json"
#define LUMINA_FIELD_ANCHOR			"lumina:anchor"	// BRect: effect origin/target

// GetDecoratorSettings() status fields
#define LUMINA_ENGINE_STATUS_PRESENT	"lumina:engine"		// bool
#define LUMINA_ENGINE_STATUS_VERSION	"lumina:engine:version"	// int32
#define LUMINA_ENGINE_STATUS_ABI		"lumina:engine:abi"	// string
#define LUMINA_ENGINE_STATUS_RUNNING_ABI "lumina:engine:running_abi"
#define LUMINA_ENGINE_STATUS_ACTIVE		"lumina:engine:active"	// bool
#define LUMINA_ENGINE_STATUS_REASON		"lumina:engine:reason"
#define LUMINA_ENGINE_STATUS_ANIMATING	"lumina:engine:animating"	// int32
#define LUMINA_ENGINE_STATUS_STATS		"lumina:engine:stats"	// string JSON

// Engine ops (LUMINA_ENGINE_FIELD_OP)
#define LUMINA_OP_SUBSCRIBE		"subscribe"		// target messenger
#define LUMINA_OP_UNSUBSCRIBE	"unsubscribe"
#define LUMINA_OP_MOVE_RESIZE	"move-resize"	// token, frame (instant)
#define LUMINA_OP_ANIMATE		"animate"		// token, frame, duration, curve
#define LUMINA_OP_CANCEL		"cancel"		// token (0 = all)
#define LUMINA_OP_MINIMIZE		"minimize"		// token, bool lumina:value
#define LUMINA_OP_ACTIVATE		"activate"		// token
#define LUMINA_OP_WORKSPACE		"workspace"		// token, workspace
#define LUMINA_OP_CONFIGURE		"configure"		// json: snap/anim settings
#define LUMINA_OP_RESERVE		"reserve"		// json: reserved areas
#define LUMINA_OP_LIST			"list"			// re-send "added" for all

// Engine event ops (LUMINA_ENGINE_EVENT, field LUMINA_ENGINE_FIELD_OP)
#define LUMINA_EVENT_ADDED		"added"
#define LUMINA_EVENT_REMOVED	"removed"
#define LUMINA_EVENT_MOVED		"moved"
#define LUMINA_EVENT_RESIZED	"resized"
#define LUMINA_EVENT_ACTIVATED	"activated"
#define LUMINA_EVENT_WORKSPACES	"workspaces"
#define LUMINA_EVENT_MINIMIZED	"minimized"
#define LUMINA_EVENT_HIDDEN		"hidden"
#define LUMINA_EVENT_SNAPPED	"snapped"
#define LUMINA_EVENT_ANIM_DONE	"animation-done"
#define LUMINA_EVENT_DROPPED	"dropped"	// queue overflow notice

// Event payload fields
#define LUMINA_EV_TOKEN		"token"		// int32 server token
#define LUMINA_EV_TEAM		"team"		// int32
#define LUMINA_EV_FRAME		"frame"		// BRect
#define LUMINA_EV_TITLE		"title"
#define LUMINA_EV_SIG		"signature"
#define LUMINA_EV_WORKSPACES "workspaces"	// int32 bitmask
#define LUMINA_EV_FLAG		"flag"		// bool (minimized/hidden state)
#define LUMINA_EV_FEEL		"feel"		// int32
#define LUMINA_EV_LOOK		"look"		// int32
#define LUMINA_EV_TIME		"when"		// int64 system_time()
#define LUMINA_EV_ZONE		"zone"		// snapped zone name

}	// namespace haiku
}	// namespace lumina

#endif
