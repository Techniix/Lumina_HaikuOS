/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Profiles ("personalities") are directories:
 *
 *   <root>/<name>/profile.lumina      manifest: name, description, author,
 *                                     schemaVersion, basedOn
 *   <root>/<name>/<section>.lumina    partial config trees, merged in
 *                                     alphabetical order
 *   <root>/<name>/assets/             wallpapers, icons, sounds
 *
 * Read-only roots (system personalities shipped in the package) come first;
 * the user root is writable. A user profile with the same name shadows a
 * system one ("customised Classic").
 *
 * Sharing uses a single self-contained JSON file (*.luminaprofile) with
 * embedded assets, so no archive tool is needed to exchange profiles.
 */
#ifndef LUMINA_CORE_PROFILE_H
#define LUMINA_CORE_PROFILE_H

#include <cstdint>
#include <string>
#include <vector>

#include "ConfigSchema.h"
#include "Value.h"

namespace lumina {

const int kCurrentSchemaVersion = 1;
const size_t kMaxBundleAssetBytes = 8 * 1024 * 1024;

struct ProfileInfo {
	std::string	name;			// directory name / identifier
	std::string	displayName;
	std::string	description;
	std::string	author;
	std::string	basedOn;
	std::string	path;
	int			schemaVersion = kCurrentSchemaVersion;
	bool		readOnly = false;
	bool		shadowsSystem = false;
};

struct ImportReport {
	bool						ok = false;
	std::string					error;
	std::string					name;
	std::vector<ValidationIssue> issues;
	std::vector<std::string>	needsApproval;	// e.g. script menus
	int							migratedFrom = 0;
};

// Upgrades `document` from `fromVersion` to kCurrentSchemaVersion in place.
bool MigrateConfig(Value& document, int fromVersion, std::string* error);

bool IsValidProfileName(const std::string& name);

std::string Base64Encode(const std::string& bytes);
bool Base64Decode(const std::string& text, std::string& bytes);
uint64_t Fnv1a64(const std::string& bytes);

// Atomic file helpers (write to temp, then rename).
bool ReadTextFile(const std::string& path, std::string& out);
bool WriteFileAtomic(const std::string& path, const std::string& contents,
	bool keepBackup = false);

class ProfileStore {
public:
	ProfileStore(std::vector<std::string> systemRoots, std::string userRoot,
		const ConfigSchema& schema = ConfigSchema::Builtin());

	std::vector<ProfileInfo> List() const;
	bool Find(const std::string& name, ProfileInfo& info) const;

	// Loads and merges all sections. Invalid values are removed and reported.
	bool Load(const std::string& name, Value& document,
		std::vector<ValidationIssue>* issues = nullptr,
		std::string* error = nullptr) const;

	// Saves to the user root, one file per top-level key.
	bool Save(const std::string& name, const Value& document,
		const ProfileInfo& manifest, std::string* error = nullptr);

	bool Duplicate(const std::string& source, const std::string& target,
		std::string* error = nullptr);
	bool Remove(const std::string& name, std::string* error = nullptr);

	// Bundles
	bool Export(const std::string& name, std::string& bundleText,
		std::string* error = nullptr) const;
	ImportReport Import(const std::string& bundleText,
		const std::string& overrideName = "");

	const std::string& UserRoot() const { return fUserRoot; }

private:
	std::vector<std::string> fSystemRoots;
	std::string				fUserRoot;
	const ConfigSchema&		fSchema;
};

// Paths inside a config document that may execute code; imported profiles
// must have these approved explicitly.
std::vector<std::string> FindExecutableSettings(const Value& document);

}	// namespace lumina

#endif
