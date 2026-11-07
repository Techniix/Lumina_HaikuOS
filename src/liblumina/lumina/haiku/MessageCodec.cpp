/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "MessageCodec.h"

#include <String.h>

#include <lumina/Json.h>

#include "Protocol.h"

namespace lumina {
namespace haiku {

status_t
PutJson(BMessage& message, const char* field, const Value& value)
{
	std::string text = WriteJson(value, JsonWriteOptions{false, 0});
	return message.AddString(field, text.c_str());
}


status_t
GetJson(const BMessage& message, const char* field, Value& value, int32 index)
{
	const char* text = NULL;
	status_t status = message.FindString(field, index, &text);
	if (status != B_OK)
		return status;
	JsonError error;
	if (!ParseJson(text, value, &error))
		return B_BAD_DATA;
	return B_OK;
}


static bool
IsHomogeneousScalarArray(const Value& array, Value::Type& type)
{
	if (array.Items().empty())
		return false;
	type = array.Items()[0].GetType();
	if (type == Value::Type::Array || type == Value::Type::Null)
		return false;
	for (const Value& item : array.Items()) {
		if (item.GetType() != type)
			return false;
	}
	return true;
}


static status_t
AddScalar(BMessage& message, const char* name, const Value& value)
{
	switch (value.GetType()) {
		case Value::Type::Bool:
			return message.AddBool(name, value.AsBool());
		case Value::Type::Number:
			return message.AddDouble(name, value.AsNumber());
		case Value::Type::String:
			return message.AddString(name, value.AsString().c_str());
		case Value::Type::Object:
		{
			BMessage child;
			status_t status = ValueToMessage(value, child);
			if (status != B_OK)
				return status;
			return message.AddMessage(name, &child);
		}
		default:
			return B_BAD_TYPE;
	}
}


status_t
ValueToMessage(const Value& object, BMessage& message)
{
	if (!object.IsObject())
		return B_BAD_VALUE;
	for (const Value::Member& m : object.Members()) {
		const char* name = m.first.c_str();
		const Value& value = m.second;
		status_t status = B_OK;
		Value::Type type;
		if (value.IsNull())
			continue;
		if (value.IsArray()) {
			if (IsHomogeneousScalarArray(value, type)) {
				for (const Value& item : value.Items()) {
					if ((status = AddScalar(message, name, item)) != B_OK)
						return status;
				}
			} else {
				BString jsonName("json:");
				jsonName << name;
				status = PutJson(message, jsonName.String(), value);
			}
		} else
			status = AddScalar(message, name, value);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


status_t
MessageToValue(const BMessage& message, Value& object)
{
	object = Value::MakeObject();
	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; message.GetInfo(B_ANY_TYPE, i, &name, &type, &count)
			== B_OK; i++) {
		BString field(name);
		if (field.StartsWith("json:")) {
			Value v;
			if (GetJson(message, name, v) == B_OK)
				object[field.String() + 5] = v;
			continue;
		}
		Value items = Value::MakeArray();
		for (int32 j = 0; j < count; j++) {
			Value v;
			switch (type) {
				case B_BOOL_TYPE:
					v = message.GetBool(name, j, false);
					break;
				case B_DOUBLE_TYPE:
					v = message.GetDouble(name, j, 0);
					break;
				case B_FLOAT_TYPE:
					v = double(message.GetFloat(name, j, 0));
					break;
				case B_INT32_TYPE:
					v = int64_t(message.GetInt32(name, j, 0));
					break;
				case B_INT64_TYPE:
					v = int64_t(message.GetInt64(name, j, 0));
					break;
				case B_STRING_TYPE:
					v = message.GetString(name, j, "");
					break;
				case B_MESSAGE_TYPE:
				{
					BMessage child;
					if (message.FindMessage(name, j, &child) == B_OK)
						MessageToValue(child, v);
					break;
				}
				default:
					continue;	// rects, pointers etc. are not config data
			}
			items.Push(v);
		}
		if (items.Size() == 1)
			object[name] = items.Items()[0];
		else if (items.Size() > 1)
			object[name] = items;
	}
	return B_OK;
}


void
ReplyStatus(BMessage* request, status_t status, const char* error,
	BMessage* reply)
{
	if (request == NULL || !request->IsSourceWaiting())
		return;
	BMessage local(B_REPLY);
	BMessage* out = reply != NULL ? reply : &local;
	out->AddInt32(LUMINA_FIELD_STATUS, status);
	if (error != NULL)
		out->AddString(LUMINA_FIELD_ERROR, error);
	request->SendReply(out);
}

}	// namespace haiku
}	// namespace lumina
