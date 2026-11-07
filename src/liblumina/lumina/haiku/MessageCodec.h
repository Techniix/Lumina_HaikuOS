/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Conversions between lumina::Value and BMessage.
 *
 *  - PutJson/GetJson: lossless; a Value travels as JSON text in one field.
 *  - ValueToMessage/MessageToValue: typed mapping for interop with native
 *    code (object → BMessage fields; nested object → B_MESSAGE_TYPE;
 *    homogeneous arrays → repeated fields; numbers → double; booleans →
 *    bool). Mixed or nested arrays fall back to a JSON string field with a
 *    "json:" name prefix.
 */
#ifndef LUMINA_HAIKU_MESSAGE_CODEC_H
#define LUMINA_HAIKU_MESSAGE_CODEC_H

#include <Message.h>

#include <lumina/Value.h>

namespace lumina {
namespace haiku {

status_t PutJson(BMessage& message, const char* field, const Value& value);
status_t GetJson(const BMessage& message, const char* field, Value& value,
	int32 index = 0);

status_t ValueToMessage(const Value& object, BMessage& message);
status_t MessageToValue(const BMessage& message, Value& object);

// Convenience for replies.
void ReplyStatus(BMessage* request, status_t status, const char* error = NULL,
	BMessage* reply = NULL);

}	// namespace haiku
}	// namespace lumina

#endif
