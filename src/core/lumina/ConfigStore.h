/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Layered configuration store.
 *
 *   schema defaults  <  personality preset  <  theme  <  user profile
 *                    <  per-app (not merged here)  <  session/preview
 *
 * Reads resolve through the merged view. Writes go to one named layer and
 * are validated against the schema first. Every successful mutation bumps
 * a generation counter and yields the list of changed leaf paths, which the
 * server broadcasts to subscribers.
 */
#ifndef LUMINA_CORE_CONFIG_STORE_H
#define LUMINA_CORE_CONFIG_STORE_H

#include <cstdint>
#include <string>
#include <vector>

#include "ConfigSchema.h"
#include "Value.h"

namespace lumina {

class ConfigStore {
public:
	struct Layer {
		std::string	name;
		Value		data = Value::MakeObject();
		bool		readOnly = false;
	};

	explicit ConfigStore(const ConfigSchema& schema = ConfigSchema::Builtin());

	// Layers are ordered low → high priority. "defaults" always exists at
	// index 0 and is read-only.
	void AddLayer(const std::string& name, bool readOnly);
	bool HasLayer(const std::string& name) const;
	const Layer* GetLayer(const std::string& name) const;

	// Replaces a layer's contents (e.g. after reading a file). Invalid
	// values are dropped and reported. Returns the changed merged paths.
	std::vector<std::string> ReplaceLayer(const std::string& name, Value data,
		std::vector<ValidationIssue>* issues = nullptr);

	// Sets one value in a layer. Fails if the layer is read-only, unknown,
	// or the value doesn't validate.
	bool Set(const std::string& layer, const std::string& path,
		const Value& value, std::string* error = nullptr,
		std::vector<std::string>* changed = nullptr);

	// Removes a value from a layer (falls back to lower layers).
	bool Unset(const std::string& layer, const std::string& path,
		std::vector<std::string>* changed = nullptr);

	// Merged view.
	const Value& Merged() const { return fMerged; }
	Value Get(const std::string& path) const;
	bool GetBool(const std::string& path, bool fallback = false) const;
	double GetNumber(const std::string& path, double fallback = 0) const;
	std::string GetString(const std::string& path,
		const std::string& fallback = "") const;

	// Which layer supplies the effective value of `path` ("" if none).
	std::string SourceOf(const std::string& path) const;

	uint64_t Generation() const { return fGeneration; }
	const ConfigSchema& Schema() const { return fSchema; }

	// Diff of two documents as changed leaf paths (added/removed/modified).
	static std::vector<std::string> Diff(const Value& before,
		const Value& after);

private:
	Layer* _FindLayer(const std::string& name);
	std::vector<std::string> _Rebuild();

	const ConfigSchema&	fSchema;
	std::vector<Layer>	fLayers;
	Value				fMerged;
	uint64_t			fGeneration = 0;
};

}	// namespace lumina

#endif
