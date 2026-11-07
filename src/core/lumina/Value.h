/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Value: a small, dependency-free document tree (null / bool / number /
 * string / array / object). Objects preserve insertion order so that
 * configuration files round-trip without reordering.
 */
#ifndef LUMINA_CORE_VALUE_H
#define LUMINA_CORE_VALUE_H

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace lumina {

class Value {
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	using Array = std::vector<Value>;
	using Member = std::pair<std::string, Value>;
	using Object = std::vector<Member>;

	Value() = default;
	Value(std::nullptr_t) {}
	Value(bool b) : fType(Type::Bool), fBool(b) {}
	Value(int n) : fType(Type::Number), fNumber(n) {}
	Value(int64_t n) : fType(Type::Number), fNumber(double(n)) {}
	Value(double n) : fType(Type::Number), fNumber(n) {}
	Value(const char* s) : fType(Type::String), fString(s ? s : "") {}
	Value(std::string s) : fType(Type::String), fString(std::move(s)) {}

	static Value MakeArray(Array items = {});
	static Value MakeObject(Object members = {});

	Type GetType() const { return fType; }
	bool IsNull() const { return fType == Type::Null; }
	bool IsBool() const { return fType == Type::Bool; }
	bool IsNumber() const { return fType == Type::Number; }
	bool IsString() const { return fType == Type::String; }
	bool IsArray() const { return fType == Type::Array; }
	bool IsObject() const { return fType == Type::Object; }

	bool AsBool(bool fallback = false) const
		{ return IsBool() ? fBool : fallback; }
	double AsNumber(double fallback = 0) const
		{ return IsNumber() ? fNumber : fallback; }
	int64_t AsInt(int64_t fallback = 0) const
		{ return IsNumber() ? int64_t(fNumber) : fallback; }
	const std::string& AsString() const;
	std::string AsString(const std::string& fallback) const
		{ return IsString() ? fString : fallback; }

	// Array access
	const Array& Items() const;
	Array& Items();
	size_t Size() const;
	void Push(Value v);

	// Object access
	const Object& Members() const;
	Object& Members();
	const Value* Find(const std::string& key) const;
	Value* Find(const std::string& key);
	Value& operator[](const std::string& key);	// inserts (converts to object)
	bool Erase(const std::string& key);
	bool Has(const std::string& key) const { return Find(key) != nullptr; }

	// Dotted-path access: "panels.0.height". Numeric segments index arrays.
	const Value* FindPath(const std::string& path) const;
	// Creates intermediate objects as needed. Returns false if a non-object
	// intermediate (other than an array with a valid index) is in the way.
	bool SetPath(const std::string& path, Value value);
	bool ErasePath(const std::string& path);

	// Deep merge: objects merge recursively, everything else is replaced.
	void MergeFrom(const Value& overlay);

	bool operator==(const Value& other) const;
	bool operator!=(const Value& other) const { return !(*this == other); }

	static const char* TypeName(Type type);

private:
	Type		fType = Type::Null;
	bool		fBool = false;
	double		fNumber = 0;
	std::string	fString;
	Array		fArray;
	Object		fObject;
};

// Splits "a.b.0.c" into segments.
std::vector<std::string> SplitPath(const std::string& path);

// Lists the dotted paths of every leaf (non-object) value in `root`.
void CollectLeafPaths(const Value& root, std::vector<std::string>& out,
	const std::string& prefix = "");

}	// namespace lumina

#endif
