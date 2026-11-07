/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Value.h"

#include <cstdlib>

namespace lumina {

namespace {
const std::string kEmptyString;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;

bool
ParseIndex(const std::string& segment, size_t& index)
{
	if (segment.empty() || segment.size() > 9)
		return false;
	for (char c : segment) {
		if (c < '0' || c > '9')
			return false;
	}
	index = size_t(std::strtoul(segment.c_str(), nullptr, 10));
	return true;
}
}	// namespace


Value
Value::MakeArray(Array items)
{
	Value v;
	v.fType = Type::Array;
	v.fArray = std::move(items);
	return v;
}


Value
Value::MakeObject(Object members)
{
	Value v;
	v.fType = Type::Object;
	v.fObject = std::move(members);
	return v;
}


const std::string&
Value::AsString() const
{
	return IsString() ? fString : kEmptyString;
}


const Value::Array&
Value::Items() const
{
	return IsArray() ? fArray : kEmptyArray;
}


Value::Array&
Value::Items()
{
	if (!IsArray()) {
		*this = MakeArray();
	}
	return fArray;
}


size_t
Value::Size() const
{
	if (IsArray())
		return fArray.size();
	if (IsObject())
		return fObject.size();
	return 0;
}


void
Value::Push(Value v)
{
	Items().push_back(std::move(v));
}


const Value::Object&
Value::Members() const
{
	return IsObject() ? fObject : kEmptyObject;
}


Value::Object&
Value::Members()
{
	if (!IsObject())
		*this = MakeObject();
	return fObject;
}


const Value*
Value::Find(const std::string& key) const
{
	if (!IsObject())
		return nullptr;
	for (const Member& m : fObject) {
		if (m.first == key)
			return &m.second;
	}
	return nullptr;
}


Value*
Value::Find(const std::string& key)
{
	if (!IsObject())
		return nullptr;
	for (Member& m : fObject) {
		if (m.first == key)
			return &m.second;
	}
	return nullptr;
}


Value&
Value::operator[](const std::string& key)
{
	if (Value* existing = Find(key))
		return *existing;
	Members().emplace_back(key, Value());
	return fObject.back().second;
}


bool
Value::Erase(const std::string& key)
{
	if (!IsObject())
		return false;
	for (auto it = fObject.begin(); it != fObject.end(); ++it) {
		if (it->first == key) {
			fObject.erase(it);
			return true;
		}
	}
	return false;
}


std::vector<std::string>
SplitPath(const std::string& path)
{
	std::vector<std::string> segments;
	std::string current;
	for (char c : path) {
		if (c == '.') {
			segments.push_back(current);
			current.clear();
		} else
			current += c;
	}
	if (!current.empty() || !segments.empty())
		segments.push_back(current);
	return segments;
}


const Value*
Value::FindPath(const std::string& path) const
{
	if (path.empty())
		return this;
	const Value* node = this;
	for (const std::string& segment : SplitPath(path)) {
		size_t index;
		if (node->IsArray() && ParseIndex(segment, index)) {
			if (index >= node->fArray.size())
				return nullptr;
			node = &node->fArray[index];
		} else {
			node = node->Find(segment);
			if (node == nullptr)
				return nullptr;
		}
	}
	return node;
}


bool
Value::SetPath(const std::string& path, Value value)
{
	if (path.empty()) {
		*this = std::move(value);
		return true;
	}
	std::vector<std::string> segments = SplitPath(path);
	Value* node = this;
	for (size_t i = 0; i < segments.size(); i++) {
		const std::string& segment = segments[i];
		if (segment.empty())
			return false;
		bool last = i + 1 == segments.size();
		size_t index;
		if (node->IsArray() && ParseIndex(segment, index)) {
			if (index > node->fArray.size())
				return false;
			if (index == node->fArray.size())
				node->fArray.push_back(Value());
			node = &node->fArray[index];
		} else {
			if (!node->IsObject() && !node->IsNull())
				return false;
			node = &(*node)[segment];
		}
		if (last) {
			*node = std::move(value);
			return true;
		}
	}
	return false;
}


bool
Value::ErasePath(const std::string& path)
{
	std::vector<std::string> segments = SplitPath(path);
	if (segments.empty())
		return false;
	std::string leaf = segments.back();
	segments.pop_back();
	std::string parentPath;
	for (size_t i = 0; i < segments.size(); i++) {
		if (i > 0)
			parentPath += '.';
		parentPath += segments[i];
	}
	Value* parent = const_cast<Value*>(
		static_cast<const Value*>(this)->FindPath(parentPath));
	if (parent == nullptr)
		return false;
	size_t index;
	if (parent->IsArray() && ParseIndex(leaf, index)) {
		if (index >= parent->fArray.size())
			return false;
		parent->fArray.erase(parent->fArray.begin() + long(index));
		return true;
	}
	return parent->Erase(leaf);
}


void
Value::MergeFrom(const Value& overlay)
{
	if (!overlay.IsObject() || !IsObject()) {
		*this = overlay;
		return;
	}
	for (const Member& m : overlay.fObject) {
		Value* existing = Find(m.first);
		if (existing != nullptr && existing->IsObject() && m.second.IsObject())
			existing->MergeFrom(m.second);
		else
			(*this)[m.first] = m.second;
	}
}


bool
Value::operator==(const Value& other) const
{
	if (fType != other.fType)
		return false;
	switch (fType) {
		case Type::Null: return true;
		case Type::Bool: return fBool == other.fBool;
		case Type::Number: return fNumber == other.fNumber;
		case Type::String: return fString == other.fString;
		case Type::Array: return fArray == other.fArray;
		case Type::Object:
		{
			// Order-insensitive comparison for objects.
			if (fObject.size() != other.fObject.size())
				return false;
			for (const Member& m : fObject) {
				const Value* o = other.Find(m.first);
				if (o == nullptr || !(*o == m.second))
					return false;
			}
			return true;
		}
	}
	return false;
}


const char*
Value::TypeName(Type type)
{
	switch (type) {
		case Type::Null: return "null";
		case Type::Bool: return "bool";
		case Type::Number: return "number";
		case Type::String: return "string";
		case Type::Array: return "array";
		case Type::Object: return "object";
	}
	return "?";
}


void
CollectLeafPaths(const Value& root, std::vector<std::string>& out,
	const std::string& prefix)
{
	if (!root.IsObject()) {
		out.push_back(prefix);
		return;
	}
	for (const Value::Member& m : root.Members()) {
		std::string path = prefix.empty() ? m.first : prefix + "." + m.first;
		CollectLeafPaths(m.second, out, path);
	}
}

}	// namespace lumina
