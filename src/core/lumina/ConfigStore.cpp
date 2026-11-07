/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "ConfigStore.h"

#include <algorithm>
#include <set>

namespace lumina {

ConfigStore::ConfigStore(const ConfigSchema& schema)
	:
	fSchema(schema)
{
	Layer defaults;
	defaults.name = "defaults";
	defaults.data = schema.Defaults();
	defaults.readOnly = true;
	fLayers.push_back(std::move(defaults));
	_Rebuild();
}


void
ConfigStore::AddLayer(const std::string& name, bool readOnly)
{
	if (HasLayer(name))
		return;
	Layer layer;
	layer.name = name;
	layer.readOnly = readOnly;
	fLayers.push_back(std::move(layer));
}


bool
ConfigStore::HasLayer(const std::string& name) const
{
	return GetLayer(name) != nullptr;
}


const ConfigStore::Layer*
ConfigStore::GetLayer(const std::string& name) const
{
	for (const Layer& layer : fLayers) {
		if (layer.name == name)
			return &layer;
	}
	return nullptr;
}


ConfigStore::Layer*
ConfigStore::_FindLayer(const std::string& name)
{
	for (Layer& layer : fLayers) {
		if (layer.name == name)
			return &layer;
	}
	return nullptr;
}


std::vector<std::string>
ConfigStore::ReplaceLayer(const std::string& name, Value data,
	std::vector<ValidationIssue>* issues)
{
	Layer* layer = _FindLayer(name);
	if (layer == nullptr || name == "defaults")
		return {};
	if (!data.IsObject())
		data = Value::MakeObject();
	std::vector<ValidationIssue> removed = fSchema.Sanitize(data);
	if (issues != nullptr) {
		*issues = fSchema.Validate(data);	// remaining warnings
		issues->insert(issues->begin(), removed.begin(), removed.end());
	}
	layer->data = std::move(data);
	return _Rebuild();
}


bool
ConfigStore::Set(const std::string& layerName, const std::string& path,
	const Value& value, std::string* error, std::vector<std::string>* changed)
{
	Layer* layer = _FindLayer(layerName);
	if (layer == nullptr) {
		if (error)
			*error = "no such layer '" + layerName + "'";
		return false;
	}
	if (layer->readOnly) {
		if (error)
			*error = "layer '" + layerName + "' is read-only";
		return false;
	}
	std::string message;
	if (!fSchema.ValidateValue(path, value, &message)) {
		if (error)
			*error = path + ": " + message;
		return false;
	}
	if (!layer->data.SetPath(path, value)) {
		if (error)
			*error = path + ": cannot set (conflicting structure)";
		return false;
	}
	std::vector<std::string> diff = _Rebuild();
	if (changed)
		*changed = diff;
	return true;
}


bool
ConfigStore::Unset(const std::string& layerName, const std::string& path,
	std::vector<std::string>* changed)
{
	Layer* layer = _FindLayer(layerName);
	if (layer == nullptr || layer->readOnly)
		return false;
	if (!layer->data.ErasePath(path))
		return false;
	std::vector<std::string> diff = _Rebuild();
	if (changed)
		*changed = diff;
	return true;
}


Value
ConfigStore::Get(const std::string& path) const
{
	const Value* v = fMerged.FindPath(path);
	if (v != nullptr)
		return *v;
	// Element defaults for wildcard entries ("panels.*.thickness").
	if (const SchemaEntry* entry = fSchema.Lookup(path))
		return entry->defaultValue;
	return Value();
}


bool
ConfigStore::GetBool(const std::string& path, bool fallback) const
{
	Value v = Get(path);
	return v.IsBool() ? v.AsBool() : fallback;
}


double
ConfigStore::GetNumber(const std::string& path, double fallback) const
{
	Value v = Get(path);
	return v.IsNumber() ? v.AsNumber() : fallback;
}


std::string
ConfigStore::GetString(const std::string& path, const std::string& fallback) const
{
	Value v = Get(path);
	return v.IsString() ? v.AsString() : fallback;
}


std::string
ConfigStore::SourceOf(const std::string& path) const
{
	for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
		if (it->data.FindPath(path) != nullptr)
			return it->name;
	}
	return "";
}


std::vector<std::string>
ConfigStore::_Rebuild()
{
	Value merged = Value::MakeObject();
	for (const Layer& layer : fLayers)
		merged.MergeFrom(layer.data);
	std::vector<std::string> diff = Diff(fMerged, merged);
	fMerged = std::move(merged);
	if (!diff.empty())
		fGeneration++;
	return diff;
}


static void
DiffInto(const Value& a, const Value& b, const std::string& prefix,
	std::set<std::string>& out)
{
	if (a.IsObject() && b.IsObject()) {
		for (const Value::Member& m : a.Members()) {
			std::string path = prefix.empty() ? m.first : prefix + "." + m.first;
			const Value* other = b.Find(m.first);
			if (other == nullptr) {
				std::vector<std::string> leaves;
				CollectLeafPaths(m.second, leaves, path);
				out.insert(leaves.begin(), leaves.end());
			} else
				DiffInto(m.second, *other, path, out);
		}
		for (const Value::Member& m : b.Members()) {
			if (a.Find(m.first) != nullptr)
				continue;
			std::string path = prefix.empty() ? m.first : prefix + "." + m.first;
			std::vector<std::string> leaves;
			CollectLeafPaths(m.second, leaves, path);
			out.insert(leaves.begin(), leaves.end());
		}
		return;
	}
	if (!(a == b))
		out.insert(prefix);
}


std::vector<std::string>
ConfigStore::Diff(const Value& before, const Value& after)
{
	std::set<std::string> paths;
	DiffInto(before, after, "", paths);
	return std::vector<std::string>(paths.begin(), paths.end());
}

}	// namespace lumina
