/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "CursorThemes.h"

#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <Message.h>
#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>
#include <TranslationUtils.h>

#include <lumina/CursorTheme.h>
#include <lumina/Json.h>
#include <lumina/haiku/Paths.h>

namespace lumina {
namespace haiku {
namespace CursorThemes {

namespace {

bool
ReadFile(const std::string& path, std::string& out)
{
	BFile file(path.c_str(), B_READ_ONLY);
	off_t size;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK
		|| size > 1024 * 1024)
		return false;
	out.resize(size_t(size));
	return file.Read(&out[0], size_t(size)) == ssize_t(size);
}


time_t
ModificationTime(const std::string& path)
{
	BEntry entry(path.c_str());
	time_t when = 0;
	if (entry.GetModificationTime(&when) != B_OK)
		return 0;
	return when;
}


// Newest modification time of the manifest and the size directory.
time_t
ThemeTime(const std::string& dir, int size)
{
	time_t newest = ModificationTime(dir + "/theme.json");
	std::string sizeDir = dir + "/" + std::to_string(size);
	BDirectory directory(sizeDir.c_str());
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		time_t when;
		if (entry.GetModificationTime(&when) == B_OK && when > newest)
			newest = when;
	}
	return newest;
}

}	// namespace


std::vector<std::string>
SearchDirs()
{
	std::vector<std::string> dirs;
	dirs.push_back(Paths::SettingsDir() + "/cursors");
	BStringList paths;
	if (BPathFinder::FindPaths(B_FIND_PATH_DATA_DIRECTORY, "Lumina/cursors",
			B_FIND_PATH_EXISTING_ONLY, paths) == B_OK) {
		for (int32 i = 0; i < paths.CountStrings(); i++)
			dirs.push_back(paths.StringAt(i).String());
	}
	return dirs;
}


std::vector<std::string>
List()
{
	std::vector<std::string> names;
	for (const std::string& base : SearchDirs()) {
		BDirectory directory(base.c_str());
		BEntry entry;
		while (directory.GetNextEntry(&entry) == B_OK) {
			BPath path;
			if (!entry.IsDirectory() || entry.GetPath(&path) != B_OK)
				continue;
			BEntry manifest((std::string(path.Path()) + "/theme.json").c_str());
			if (!manifest.Exists())
				continue;
			std::string name = path.Leaf();
			bool known = false;
			for (const std::string& existing : names)
				known |= existing == name;
			if (!known)
				names.push_back(name);
		}
	}
	return names;
}


std::string
Find(const std::string& name)
{
	if (name.empty() || name.find('/') != std::string::npos
		|| name.find("..") != std::string::npos)
		return "";
	for (const std::string& base : SearchDirs()) {
		std::string dir = base + "/" + name;
		if (BEntry((dir + "/theme.json").c_str()).Exists())
			return dir;
	}
	return "";
}


float
CurrentScale()
{
	// Same expression as Desktop::Init(): bold font size / 12.
	return be_bold_font != NULL ? be_bold_font->Size() / 12.0f : 1.0f;
}


status_t
Build(const std::string& name, float scale, std::string& outPath,
	std::vector<std::string>& problems)
{
	std::string dir = Find(name);
	if (dir.empty()) {
		problems.push_back("cursor theme \"" + name + "\" not found");
		return B_ENTRY_NOT_FOUND;
	}
	std::string text;
	if (!ReadFile(dir + "/theme.json", text)) {
		problems.push_back("cannot read " + dir + "/theme.json");
		return B_IO_ERROR;
	}
	Value manifest;
	JsonError error;
	if (!ParseJson(text, manifest, &error)) {
		problems.push_back("theme.json: " + error.ToString());
		return B_BAD_DATA;
	}
	cursor::Theme theme;
	if (!cursor::ParseTheme(manifest, theme, problems))
		return B_BAD_DATA;
	int size = cursor::PickSize(theme, scale);

	std::string cacheDir = Paths::SettingsDir() + "/cache";
	create_directory(cacheDir.c_str(), 0755);
	outPath = cacheDir + "/cursors-" + name + "-" + std::to_string(size)
		+ ".cursorset";
	if (ModificationTime(outPath) > ThemeTime(dir, size))
		return B_OK;	// up to date

	BMessage set;
	set.AddString("name", theme.name.c_str());
	int built = 0;
	for (const cursor::Entry& entry : theme.entries) {
		const cursor::Role* role = cursor::FindRole(entry.role);
		std::string file = dir + "/" + cursor::RelativePath(entry, size);
		BBitmap* image = BTranslationUtils::GetBitmapFile(file.c_str());
		if (image == NULL) {
			problems.push_back("cannot read image " + file);
			continue;
		}
		BRect bounds = image->Bounds();
		if (bounds.IntegerWidth() + 1 != size
			|| bounds.IntegerHeight() + 1 != size) {
			problems.push_back(file + ": expected " + std::to_string(size)
				+ "x" + std::to_string(size) + " pixels");
			delete image;
			continue;
		}
		// Normalise to straight RGBA32 rows of size*4 bytes, premultiply.
		BBitmap rgba(BRect(0, 0, size - 1, size - 1), B_RGBA32);
		if (rgba.ImportBits(image) != B_OK) {
			problems.push_back(file + ": unsupported color space");
			delete image;
			continue;
		}
		delete image;
		std::vector<uint8> data(size_t(size) * size * 4);
		for (int y = 0; y < size; y++) {
			memcpy(&data[size_t(y) * size * 4],
				(uint8*)rgba.Bits() + size_t(y) * rgba.BytesPerRow(),
				size_t(size) * 4);
		}
		cursor::Premultiply(data.data(), size, size, size * 4);
		float hotX, hotY;
		cursor::ScaledHotspot(theme, entry, size, hotX, hotY);

		BMessage message;
		message.AddString("class", "cursor");
		message.AddRect("_frame", BRect(0, 0, size - 1, size - 1));
		message.AddInt32("_cspace", B_RGBA32);
		message.AddPoint("hotspot", BPoint(hotX, hotY));
		message.AddData("_data", B_RAW_TYPE, data.data(), data.size());
		set.AddMessage(role->setKey, &message);
		built++;
	}
	if (built == 0) {
		problems.push_back("no cursor of theme \"" + name + "\" could be built");
		return B_BAD_DATA;
	}
	std::string temporary = outPath + ".tmp";
	BFile out(temporary.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = out.InitCheck();
	if (status == B_OK)
		status = set.Flatten(&out);
	out.Unset();
	if (status == B_OK) {
		BEntry entry(temporary.c_str());
		status = entry.Rename(outPath.c_str(), true);
	}
	if (status != B_OK)
		problems.push_back("cannot write " + outPath);
	return status;
}

}	// namespace CursorThemes
}	// namespace haiku
}	// namespace lumina
