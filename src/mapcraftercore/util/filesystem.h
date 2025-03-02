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

#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace mapcrafter {
namespace util {

std::chrono::system_clock::time_point fsTimeToSystem(fs::file_time_type fs_time) noexcept;

fs::file_time_type systemTimeToFs(std::chrono::system_clock::time_point system_time) noexcept;

/**
 * Opens a binary file for reading.
 *
 * The returned stream will throw @c std::exception if an error occurs.
 *
 * @param path the file path
 * @return a @c std::ifstream for reading from the file
 * @throws std::exception if the operation fails
 */
std::ifstream openBinaryFileForRead(const fs::path& path);

/**
 * Opens a binary file for writing. If the file exists, it will be truncated.
 *
 * The returned stream will throw @c std::exception if an error occurs.
 *
 * @param path the file path
 * @return a @c std::ofstream for writing to the file
 * @throws std::exception if the operation fails
 */
std::ofstream openBinaryFileForWrite(const fs::path& path);

/**
 * Reads the binary contents of the given file.
 *
 * @param path the file path
 * @return the file data
 * @throws std::exception if the operation fails
 */
std::string readEntireFileToString(const fs::path& path);

/**
 * Reads the binary contents of the given file.
 *
 * @param path the file path
 * @return the file data
 * @throws std::exception if the operation fails
 */
std::vector<uint8_t> readEntireFileToVector(const fs::path& path);

/**
 * Writes the given data to the given file. If the file exists, its contents will be overwritten.
 *
 * @param path the file path
 * @param data the data to write
 * @throws std::exception if the operation fails
 */
void writeEntireFile(const fs::path& path, const std::string& data);

/**
 * Writes the given data to the given file. If the file exists, its contents will be overwritten.
 *
 * @param path the file path
 * @param data the data to write
 * @throws std::exception if the operation fails
 */
void writeEntireFile(const fs::path& path, const std::vector<uint8_t>& data);

/**
 * Writes the given data to the given file. If the file exists, its contents will be overwritten.
 *
 * @param path the file path
 * @param data the data to write
 * @param size the number of bytes to write
 * @throws std::exception if the operation fails
 */
void writeEntireFile(const fs::path& path, const void* data, size_t size);

/**
 * Emulate the behavior of boost's two-argument filesystem::absolute() function.
 *
 * https://www.boost.org/doc/libs/1_87_0/libs/filesystem/doc/reference.html#absolute
 */
fs::path absolute_with_base(const fs::path& path, const fs::path& base);

/**
 * Returns the home directory of the current user.
 *
 * Returns the value of the 'HOME' environment variable on every unix-like operating
 * system, returns the value of the 'APPDATA' environment variable on W***.
 */
fs::path findHomeDir();

/**
 * Returns the path to the currently running program.
 */
fs::path findExecutablePath();

/**
 * Returns the directory which contains the Mapcrafter executable.
 *
 * Usually returns the directory of the program currently running, but returns the parent
 * directory of the program directory if the program is another Mapcrafter tool such as
 * testconfig or testtextures and the program directory is called tools/.
 */
fs::path findExecutableMapcrafterDir(fs::path executable = findExecutablePath());

typedef std::vector<fs::path> PathList;

/**
 * Returns all possible Mapcrafter resource directories.
 */
PathList findResourceDirs(const fs::path& executable);

/**
 * Returns all possible Mapcrafter template directories.
 */
PathList findTemplateDirs(const fs::path& executable);

/**
 * Returns all possible Mapcrafter block directories.
 */
PathList findBlockDirs(const fs::path& executable);

/**
 * Returns all possible logging configuration files.
 */
PathList findLoggingConfigFiles(const fs::path& executable);

/**
 * Returns the first existing template directory.
 */
fs::path findTemplateDir();

/**
 * Returns the first existing block directory.
 */
fs::path findBlockDir();

/**
 * Returns the first existing logging configuration file.
 */
fs::path findLoggingConfigFile();

} /* namespace util */
} /* namespace mapcrafter */
#endif /* FILESYSTEM_H_ */
