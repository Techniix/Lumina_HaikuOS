/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Profile.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "Json.h"

namespace fs = std::filesystem;

namespace lumina {

namespace {
const char* kManifestFile = "profile.lumina";
const char* kSuffix = ".lumina";
const char* kBundleFormat = "lumina-profile";
const int kBundleFormatVersion = 1;
}


bool
MigrateConfig(Value& document, int fromVersion, std::string* error)
{
	if (fromVersion > kCurrentSchemaVersion) {
		if (error)
			*error = "profile was made by a newer Lumina (schema "
				+ std::to_string(fromVersion) + ")";
		return false;
	}
	if (fromVersion < 1) {
		if (error)
			*error = "unknown schema version";
		return false;
	}
	// Migration steps go here: if (fromVersion < 2) { ...; fromVersion = 2; }
	(void)document;
	return true;
}


bool
IsValidProfileName(const std::string& name)
{
	if (name.empty() || name.size() > 64 || name[0] == '.')
		return false;
	for (char c : name) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
		if (!ok)
			return false;
	}
	return true;
}


static const char kBase64Chars[]
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


std::string
Base64Encode(const std::string& bytes)
{
	std::string out;
	out.reserve((bytes.size() + 2) / 3 * 4);
	size_t i = 0;
	while (i + 2 < bytes.size()) {
		uint32_t n = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8)
			| uint8_t(bytes[i + 2]);
		out += kBase64Chars[(n >> 18) & 63];
		out += kBase64Chars[(n >> 12) & 63];
		out += kBase64Chars[(n >> 6) & 63];
		out += kBase64Chars[n & 63];
		i += 3;
	}
	size_t rest = bytes.size() - i;
	if (rest == 1) {
		uint32_t n = uint8_t(bytes[i]) << 16;
		out += kBase64Chars[(n >> 18) & 63];
		out += kBase64Chars[(n >> 12) & 63];
		out += "==";
	} else if (rest == 2) {
		uint32_t n = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8);
		out += kBase64Chars[(n >> 18) & 63];
		out += kBase64Chars[(n >> 12) & 63];
		out += kBase64Chars[(n >> 6) & 63];
		out += '=';
	}
	return out;
}


bool
Base64Decode(const std::string& text, std::string& bytes)
{
	auto decode = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};
	bytes.clear();
	uint32_t buffer = 0;
	int bits = 0;
	size_t padding = 0;
	for (char c : text) {
		if (c == '\n' || c == '\r' || c == ' ')
			continue;
		if (c == '=') {
			padding++;
			continue;
		}
		if (padding > 0)
			return false;	// data after padding
		int value = decode(c);
		if (value < 0)
			return false;
		buffer = (buffer << 6) | uint32_t(value);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			bytes += char((buffer >> bits) & 0xff);
		}
	}
	return padding <= 2;
}


uint64_t
Fnv1a64(const std::string& bytes)
{
	uint64_t hash = 0xcbf29ce484222325ULL;
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 0x100000001b3ULL;
	}
	return hash;
}


static std::string
HexU64(uint64_t value)
{
	char buffer[20];
	std::snprintf(buffer, sizeof(buffer), "%016llx",
		static_cast<unsigned long long>(value));
	return buffer;
}


bool
ReadTextFile(const std::string& path, std::string& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	std::ostringstream buffer;
	buffer << in.rdbuf();
	out = buffer.str();
	return true;
}


bool
WriteFileAtomic(const std::string& path, const std::string& contents,
	bool keepBackup)
{
	std::error_code ec;
	fs::path target(path);
	if (target.has_parent_path())
		fs::create_directories(target.parent_path(), ec);
	std::string temp = path + ".tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out.write(contents.data(), std::streamsize(contents.size()));
		out.flush();
		if (!out)
			return false;
	}
	if (keepBackup && fs::exists(target, ec))
		fs::copy_file(target, path + ".bak",
			fs::copy_options::overwrite_existing, ec);
	fs::rename(temp, target, ec);
	return !ec;
}


static bool
ReadManifest(const fs::path& dir, ProfileInfo& info)
{
	std::string text;
	if (!ReadTextFile((dir / kManifestFile).string(), text))
		return false;
	Value manifest;
	if (!ParseJson(text, manifest) || !manifest.IsObject())
		return false;
	info.name = dir.filename().string();
	info.path = dir.string();
	info.displayName = manifest.Find("displayName")
		? manifest.Find("displayName")->AsString(info.name) : info.name;
	if (const Value* v = manifest.Find("description"))
		info.description = v->AsString();
	if (const Value* v = manifest.Find("author"))
		info.author = v->AsString();
	if (const Value* v = manifest.Find("basedOn"))
		info.basedOn = v->AsString();
	if (const Value* v = manifest.Find("schemaVersion"))
		info.schemaVersion = int(v->AsInt(kCurrentSchemaVersion));
	return true;
}


static Value
ManifestToValue(const ProfileInfo& info)
{
	Value manifest = Value::MakeObject();
	manifest["displayName"] = info.displayName.empty()
		? info.name : info.displayName;
	manifest["description"] = info.description;
	manifest["author"] = info.author;
	if (!info.basedOn.empty())
		manifest["basedOn"] = info.basedOn;
	manifest["schemaVersion"] = kCurrentSchemaVersion;
	return manifest;
}


ProfileStore::ProfileStore(std::vector<std::string> systemRoots,
	std::string userRoot, const ConfigSchema& schema)
	:
	fSystemRoots(std::move(systemRoots)),
	fUserRoot(std::move(userRoot)),
	fSchema(schema)
{
}


std::vector<ProfileInfo>
ProfileStore::List() const
{
	std::vector<ProfileInfo> result;
	std::error_code ec;
	auto scan = [&](const std::string& root, bool readOnly) {
		if (root.empty() || !fs::is_directory(root, ec))
			return;
		for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
			if (!entry.is_directory(ec))
				continue;
			ProfileInfo info;
			if (!ReadManifest(entry.path(), info))
				continue;
			info.readOnly = readOnly;
			auto existing = std::find_if(result.begin(), result.end(),
				[&](const ProfileInfo& p) { return p.name == info.name; });
			if (existing != result.end()) {
				info.shadowsSystem = existing->readOnly && !readOnly;
				*existing = info;
			} else
				result.push_back(info);
		}
	};
	for (const std::string& root : fSystemRoots)
		scan(root, true);
	scan(fUserRoot, false);
	std::sort(result.begin(), result.end(),
		[](const ProfileInfo& a, const ProfileInfo& b) { return a.name < b.name; });
	return result;
}


bool
ProfileStore::Find(const std::string& name, ProfileInfo& info) const
{
	for (const ProfileInfo& p : List()) {
		if (p.name == name) {
			info = p;
			return true;
		}
	}
	return false;
}


bool
ProfileStore::Load(const std::string& name, Value& document,
	std::vector<ValidationIssue>* issues, std::string* error) const
{
	ProfileInfo info;
	if (!Find(name, info)) {
		if (error)
			*error = "no profile named '" + name + "'";
		return false;
	}
	std::error_code ec;
	std::vector<fs::path> sections;
	for (const fs::directory_entry& entry
			: fs::directory_iterator(info.path, ec)) {
		std::string file = entry.path().filename().string();
		if (file == kManifestFile || !entry.is_regular_file(ec))
			continue;
		if (file.size() > 7 && file.compare(file.size() - 7, 7, kSuffix) == 0)
			sections.push_back(entry.path());
	}
	std::sort(sections.begin(), sections.end());

	Value merged = Value::MakeObject();
	for (const fs::path& section : sections) {
		std::string text;
		Value part;
		JsonError jsonError;
		if (!ReadTextFile(section.string(), text)
			|| !ParseJson(text, part, &jsonError) || !part.IsObject()) {
			// Quarantine and continue: a broken file must never take the
			// whole profile (and thus the desktop) down.
			if (issues) {
				issues->push_back({section.filename().string(),
					"unreadable section skipped: " + jsonError.ToString(),
					true});
			}
			continue;
		}
		merged.MergeFrom(part);
	}
	std::string migrateError;
	if (!MigrateConfig(merged, info.schemaVersion, &migrateError)) {
		if (error)
			*error = migrateError;
		return false;
	}
	std::vector<ValidationIssue> removed = fSchema.Sanitize(merged);
	if (issues)
		issues->insert(issues->end(), removed.begin(), removed.end());
	document = std::move(merged);
	return true;
}


bool
ProfileStore::Save(const std::string& name, const Value& document,
	const ProfileInfo& manifest, std::string* error)
{
	if (!IsValidProfileName(name)) {
		if (error)
			*error = "invalid profile name '" + name + "'";
		return false;
	}
	for (const ValidationIssue& issue : fSchema.Validate(document)) {
		if (issue.fatal) {
			if (error)
				*error = issue.path + ": " + issue.message;
			return false;
		}
	}
	fs::path dir = fs::path(fUserRoot) / name;
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		if (error)
			*error = "cannot create " + dir.string() + ": " + ec.message();
		return false;
	}
	ProfileInfo info = manifest;
	info.name = name;
	if (!WriteFileAtomic((dir / kManifestFile).string(),
			WriteJson(ManifestToValue(info)))) {
		if (error)
			*error = "cannot write manifest";
		return false;
	}
	// Remove stale sections, then write one file per top-level key.
	for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
		std::string file = entry.path().filename().string();
		if (file != kManifestFile && file.size() > 7
			&& file.compare(file.size() - 7, 7, kSuffix) == 0
			&& !document.Has(file.substr(0, file.size() - 7))) {
			fs::remove(entry.path(), ec);
		}
	}
	for (const Value::Member& m : document.Members()) {
		Value part = Value::MakeObject();
		part[m.first] = m.second;
		if (!WriteFileAtomic((dir / (m.first + kSuffix)).string(),
				WriteJson(part), true)) {
			if (error)
				*error = "cannot write section " + m.first;
			return false;
		}
	}
	return true;
}


bool
ProfileStore::Duplicate(const std::string& source, const std::string& target,
	std::string* error)
{
	Value document;
	if (!Load(source, document, nullptr, error))
		return false;
	ProfileInfo info;
	Find(source, info);
	info.basedOn = source;
	info.displayName = target;
	if (!Save(target, document, info, error))
		return false;
	std::error_code ec;
	fs::path assets = fs::path(info.path) / "assets";
	if (fs::is_directory(assets, ec)) {
		fs::copy(assets, fs::path(fUserRoot) / target / "assets",
			fs::copy_options::recursive | fs::copy_options::overwrite_existing,
			ec);
	}
	return true;
}


bool
ProfileStore::Remove(const std::string& name, std::string* error)
{
	if (!IsValidProfileName(name)) {
		if (error)
			*error = "invalid name";
		return false;
	}
	fs::path dir = fs::path(fUserRoot) / name;
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) {
		if (error)
			*error = "not a user profile (system profiles are read-only)";
		return false;
	}
	fs::remove_all(dir, ec);
	return !ec;
}


bool
ProfileStore::Export(const std::string& name, std::string& bundleText,
	std::string* error) const
{
	ProfileInfo info;
	Value document;
	if (!Find(name, info) || !Load(name, document, nullptr, error)) {
		if (error && error->empty())
			*error = "no profile named '" + name + "'";
		return false;
	}
	Value bundle = Value::MakeObject();
	bundle["format"] = kBundleFormat;
	bundle["formatVersion"] = kBundleFormatVersion;
	bundle["name"] = name;
	bundle["manifest"] = ManifestToValue(info);
	bundle["config"] = document;

	Value assets = Value::MakeObject();
	size_t total = 0;
	std::error_code ec;
	fs::path assetDir = fs::path(info.path) / "assets";
	if (fs::is_directory(assetDir, ec)) {
		for (const fs::directory_entry& entry
				: fs::recursive_directory_iterator(assetDir, ec)) {
			if (!entry.is_regular_file(ec))
				continue;
			std::string bytes;
			if (!ReadTextFile(entry.path().string(), bytes))
				continue;
			total += bytes.size();
			if (total > kMaxBundleAssetBytes) {
				if (error)
					*error = "assets exceed bundle limit";
				return false;
			}
			std::string relative
				= fs::relative(entry.path(), assetDir, ec).generic_string();
			assets[relative] = Base64Encode(bytes);
		}
	}
	bundle["assets"] = assets;
	std::string payload = WriteJson(bundle["config"], {false, 0})
		+ WriteJson(assets, {false, 0});
	bundle["checksum"] = HexU64(Fnv1a64(payload));
	bundleText = WriteJson(bundle);
	return true;
}


std::vector<std::string>
FindExecutableSettings(const Value& document)
{
	std::vector<std::string> found;
	const Value* panels = document.Find("panels");
	if (panels != nullptr) {
		for (size_t i = 0; i < panels->Items().size(); i++) {
			const Value* applets = panels->Items()[i].Find("applets");
			if (applets == nullptr)
				continue;
			for (size_t j = 0; j < applets->Items().size(); j++) {
				const std::string& applet = applets->Items()[j].AsString();
				if (applet.compare(0, 9, "jsonmenu:") == 0
					|| applet.compare(0, 7, "script:") == 0) {
					found.push_back("panels." + std::to_string(i) + ".applets."
						+ std::to_string(j));
				}
			}
		}
	}
	return found;
}


ImportReport
ProfileStore::Import(const std::string& bundleText,
	const std::string& overrideName)
{
	ImportReport report;
	Value bundle;
	JsonError jsonError;
	if (!ParseJson(bundleText, bundle, &jsonError) || !bundle.IsObject()) {
		report.error = "not a Lumina profile: " + jsonError.ToString();
		return report;
	}
	if (bundle.Find("format") == nullptr
		|| bundle.Find("format")->AsString() != kBundleFormat) {
		report.error = "not a Lumina profile (wrong format tag)";
		return report;
	}
	if (bundle.Find("formatVersion") == nullptr
		|| bundle.Find("formatVersion")->AsInt() > kBundleFormatVersion) {
		report.error = "bundle format is newer than this Lumina";
		return report;
	}
	Value config = bundle.Find("config") ? *bundle.Find("config")
		: Value::MakeObject();
	Value assets = bundle.Find("assets") ? *bundle.Find("assets")
		: Value::MakeObject();
	if (const Value* checksum = bundle.Find("checksum")) {
		std::string payload = WriteJson(config, {false, 0})
			+ WriteJson(assets, {false, 0});
		if (checksum->AsString() != HexU64(Fnv1a64(payload))) {
			report.error = "checksum mismatch (file damaged or edited)";
			return report;
		}
	}

	ProfileInfo info;
	const Value* manifest = bundle.Find("manifest");
	if (manifest != nullptr) {
		if (const Value* v = manifest->Find("displayName"))
			info.displayName = v->AsString();
		if (const Value* v = manifest->Find("description"))
			info.description = v->AsString();
		if (const Value* v = manifest->Find("author"))
			info.author = v->AsString();
		if (const Value* v = manifest->Find("schemaVersion"))
			info.schemaVersion = int(v->AsInt(kCurrentSchemaVersion));
	}
	std::string name = overrideName.empty()
		? (bundle.Find("name") ? bundle.Find("name")->AsString() : "")
		: overrideName;
	if (!IsValidProfileName(name)) {
		report.error = "invalid profile name '" + name + "'";
		return report;
	}
	if (info.schemaVersion != kCurrentSchemaVersion)
		report.migratedFrom = info.schemaVersion;
	if (!MigrateConfig(config, info.schemaVersion, &report.error))
		return report;

	report.issues = fSchema.Sanitize(config);
	report.needsApproval = FindExecutableSettings(config);
	// Executable entries are disabled until the user approves them.
	for (auto it = report.needsApproval.rbegin();
			it != report.needsApproval.rend(); ++it) {
		config.ErasePath(*it);
	}
	Value pending = Value::MakeArray();
	for (const std::string& path : report.needsApproval)
		pending.Push(path);
	if (!report.needsApproval.empty()) {
		info.description += info.description.empty() ? "" : " ";
		info.description += "(imported; script entries awaiting approval)";
	}

	if (!Save(name, config, info, &report.error))
		return report;

	// Assets: refuse path traversal.
	fs::path assetDir = fs::path(fUserRoot) / name / "assets";
	for (const Value::Member& m : assets.Members()) {
		fs::path relative(m.first);
		bool safe = !relative.is_absolute() && !m.first.empty();
		for (const fs::path& part : relative) {
			if (part == "..")
				safe = false;
		}
		if (!safe) {
			report.issues.push_back({"assets/" + m.first,
				"unsafe asset path rejected", true});
			continue;
		}
		std::string bytes;
		if (!Base64Decode(m.second.AsString(), bytes)) {
			report.issues.push_back({"assets/" + m.first,
				"undecodable asset skipped", true});
			continue;
		}
		WriteFileAtomic((assetDir / relative).string(), bytes);
	}
	report.name = name;
	report.ok = true;
	return report;
}

}	// namespace lumina
