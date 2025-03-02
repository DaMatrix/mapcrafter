/*
 * Copyright 2012-2016 Moritz Hilscher
 *
 * This file is part of Mapcrafter.
 *
 * Mapcrafter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mapcrafter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mapcrafter.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filesystem.h"

#include "../util.h"

#include <iostream>
#include <fstream>

#if defined(__APPLE__)
  #include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
  #include <sys/sysctl.h>
#elif defined(OS_WINDOWS)
  #include <windows.h>
#endif

namespace mapcrafter {
namespace util {

//TODO: this probably doesn't convert the timestamp correctly to the system clock, but we need C++20 to do that...

std::chrono::system_clock::time_point fsTimeToSystem(fs::file_time_type fs_time) noexcept {
	return fs_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now();
}

fs::file_time_type systemTimeToFs(std::chrono::system_clock::time_point system_time) noexcept {
	return system_time - std::chrono::system_clock::now() + fs::file_time_type::clock::now();
}

std::ifstream openBinaryFileForRead(const fs::path& path) {
	std::ifstream result;
	result.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	result.open(path.string(), std::ios_base::binary);
	return result;
}

std::ofstream openBinaryFileForWrite(const fs::path& path) {
	std::ofstream result;
	result.exceptions(std::ofstream::failbit | std::ofstream::badbit);
	result.open(path.string(), std::ios_base::binary);
	return result;
}

std::string readEntireFileToString(const fs::path& path) {
	size_t size = fs::file_size(path);
	std::string result(size, '\0');
	openBinaryFileForRead(path).read(&result[0], size);
	return result;
}

std::vector<uint8_t> readEntireFileToVector(const fs::path& path) {
	size_t size = fs::file_size(path);
	std::vector<uint8_t> result(size);
	openBinaryFileForRead(path).read(reinterpret_cast<char*>(result.data()), size);
	return result;
}

void writeEntireFile(const fs::path& path, const std::string& data) {
	writeEntireFile(path, data.data(), data.size());
}

void writeEntireFile(const fs::path& path, const std::vector<uint8_t>& data) {
	writeEntireFile(path, data.data(), data.size());
}

void writeEntireFile(const fs::path& path, const void* data, size_t size) {
	openBinaryFileForWrite(path).write(static_cast<const char*>(data), size);
}

fs::path absolute_with_base(const fs::path& path, const fs::path& base) {
	if (path.has_root_name()) {
		if (path.has_root_directory()) {
			return path;
		} else {
			return path.root_name() / absolute(base).root_directory() / absolute(base).relative_path() / path.relative_path();
		}
	} else {
		if (path.has_root_directory()) {
			return absolute(base).root_name() / path;
		} else {
			return absolute(base) / path;
		}
	}
}

fs::path findHomeDir() {
	char* path;
#if defined(OS_WINDOWS)
	path = getenv("APPDATA");
#else
	path = getenv("HOME");
#endif
	if (path != nullptr)
		return fs::path(path);
	return fs::path("");
}

// see also http://stackoverflow.com/questions/12468104/multi-os-get-executable-path
fs::path findExecutablePath() {
	char buf[1024];
#if defined(__APPLE__)
	uint32_t size = sizeof(buf);
	if (_NSGetExecutablePath(buf, &size) == 0) {
		char real_path[1024];
		if (realpath(buf, real_path)) {
			size_t len = strlen(real_path);
			return fs::path(std::string(real_path, len));
		}
	}
#elif defined(__FreeBSD__)
	int mib[4];
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	size_t size = sizeof(buf);
	sysctl(mib, 4, buf, &size, NULL, 0);
	return fs::path(std::string(buf));
#elif defined(unix) || defined(__unix) || defined(__unix__) || defined(__linux__)
	return std::filesystem::read_symlink("/proc/self/exe");
#elif defined(OS_WINDOWS)
	GetModuleFileName(NULL, buf, 1024);
	return fs::path(std::string(buf));
#else
	static_assert(0, "Unable to find the executable's path!");
#endif
	return fs::path("");
}

fs::path findExecutableMapcrafterDir(fs::path executable) {
	std::string filename = executable.filename().string();
	// TODO make it independent of name of the tool
	if ((filename == "testconfig"
			|| filename == "mapcrafter_markers"
			|| filename == "test") &&
			executable.parent_path().filename().string() == "tools")
		return executable.parent_path().parent_path();
	return executable.parent_path();
}

PathList findResourceDirs(const fs::path& executable) {
	fs::path mapcrafter_dir = findExecutableMapcrafterDir(executable);
	PathList resources = {
		mapcrafter_dir.parent_path() / "share" / "mapcrafter",
		mapcrafter_dir / "data",
	};
	fs::path home = findHomeDir();
	if (!home.empty())
		resources.insert(resources.begin(), home / ".mapcrafter");

	for (PathList::iterator it = resources.begin(); it != resources.end(); ) {
		if (!fs::is_directory(*it))
			resources.erase(it);
		else
			++it;
	}
	return resources;
}

PathList findTemplateDirs(const fs::path& executable) {
	PathList templates, resources = findResourceDirs(executable);
	for (PathList::iterator it = resources.begin(); it != resources.end(); ++it)
		if (fs::is_directory(*it / "template"))
			templates.push_back(*it / "template");
	return templates;
}

PathList findDirs(const fs::path& executable, std::string dir_name) {
	PathList resources = findResourceDirs(executable);
	PathList dirs;
	for (PathList::iterator it = resources.begin(); it != resources.end(); ++it) {
		if (fs::is_directory(*it / dir_name)) {
			dirs.push_back(*it / dir_name);
		}
	}
	return dirs;
}

PathList findBlockDirs(const fs::path& executable) {
	return findDirs(executable, "blocks");
}

PathList findLoggingConfigFiles(const fs::path& executable) {
	fs::path mapcrafter_dir = findExecutableMapcrafterDir(findExecutablePath());
	PathList configs = {
		mapcrafter_dir.parent_path().parent_path() / "etc" / "mapcrafter" / "logging.conf",
		mapcrafter_dir / "logging.conf",
		fs::current_path() / "logging.conf",
	};

	fs::path home = findHomeDir();
	if (!home.empty())
		configs.insert(configs.begin(), home / ".mapcrafter" / "logging.conf");

	for (PathList::iterator it = configs.begin(); it != configs.end(); ) {
		if (!fs::is_regular_file(*it))
			configs.erase(it);
		else
			++it;
	}
	return configs;
}

fs::path findTemplateDir() {
	PathList templates = findTemplateDirs(findExecutablePath());
	if (templates.size())
		return *templates.begin();
	return fs::path();
}

fs::path findBlockDir() {
	PathList dirs = findBlockDirs(findExecutablePath());
	if (dirs.size()) {
		return *dirs.begin();
	}
	return fs::path();
}

fs::path findLoggingConfigFile() {
	PathList configs = findLoggingConfigFiles(findExecutablePath());
	if (configs.size())
		return *configs.begin();
	return fs::path();
}

} /* namespace util */
} /* namespace mapcrafter */
