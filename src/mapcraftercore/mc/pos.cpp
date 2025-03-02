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

#include <sys/param.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "../util.h"
#include "chunk.h"
#include "pos.h"
#include "region.h"

namespace mapcrafter {
namespace mc {

RegionPos::RegionPos() : x(0), z(0) {
}

RegionPos::RegionPos(int x, int z) : x(x), z(z) {
}

bool RegionPos::operator==(const RegionPos& other) const {
	return x == other.x && z == other.z;
}

bool RegionPos::operator!=(const RegionPos& other) const {
	return !operator==(other);
}

bool RegionPos::operator<(const RegionPos& other) const {
	if (x == other.x) return z < other.z;
	return x > other.x;
}

RegionPos RegionPos::byFilename(const std::string& filename) {
	auto name = fs::path(filename).filename();

	int x, z;
	if (sscanf(name.c_str(), "r.%d.%d.mca", &x, &z) != 2)
		throw std::runtime_error("Invalid filename " + name.string() + "!");
	return RegionPos(x, z);
}

ChunkPos::ChunkPos() : x(0), z(0) {
}

ChunkPos::ChunkPos(int x, int z) : x(x), z(z) {
}

ChunkPos::ChunkPos(const BlockPos& block) {
	x = block.x >> 4;
	z = block.z >> 4;
}

int ChunkPos::getLocalX() const {
	return x & int(31);
}
int ChunkPos::getLocalZ() const {
	return z & int(31);
}

RegionPos ChunkPos::getRegion() const {
	return RegionPos(x >> 5, z >> 5);
}

bool ChunkPos::operator==(const ChunkPos& other) const {
	return x == other.x && z == other.z;
}

bool ChunkPos::operator!=(const ChunkPos& other) const {
	return !operator==(other);
}

bool ChunkPos::operator<(const ChunkPos& other) const {
	if (x == other.x) return z < other.z;
	return x > other.x;
}

BlockPos::BlockPos() : x(0), z(0), y(0) {
}

BlockPos::BlockPos(int x, int z, int y) : x(x), z(z), y(y) {
}

BlockDir BlockPos::operator-(const BlockPos& p2) const {
	BlockDir d(this->x - p2.x, this->z - p2.z, this->y - p2.y);
	return d;
}

BlockPos& BlockPos::operator+=(const BlockDir& dir) {
	x += dir.x;
	z += dir.z;
	y += dir.y;
	return *this;
}

BlockPos& BlockPos::operator-=(const BlockDir& dir) {
	x -= dir.x;
	z -= dir.z;
	y -= dir.y;
	return *this;
}

BlockPos BlockPos::operator+(const BlockDir& dir) const {
	BlockPos p(this->x + dir.x, this->z + dir.z, this->y + dir.y);
	return p;
}

BlockPos BlockPos::operator-(const BlockDir& dir) const {
	BlockPos p(this->x - dir.x, this->z - dir.z, this->y - dir.y);
	return p;
}

bool BlockPos::operator==(const BlockPos& other) const {
	return x == other.x && z == other.z && y == other.y;
}

bool BlockPos::operator!=(const BlockPos& other) const {
	return !operator==(other);
}

BlockDir::BlockDir() : x(0), z(0), y(0) {
}

BlockDir::BlockDir(int x, int z, int y) : x(x), z(z), y(y) {
}

BlockDir& BlockDir::operator+=(const BlockDir& p) {
	x += p.x;
	z += p.z;
	y += p.y;
	return *this;
}

BlockDir& BlockDir::operator-=(const BlockDir& p) {
	x -= p.x;
	z -= p.z;
	y -= p.y;
	return *this;
}

BlockDir BlockDir::operator+(const BlockDir& p2) const {
	BlockDir p = *this;
	return p += p2;
}

BlockDir BlockDir::operator-(const BlockDir& p2) const {
	BlockDir p = *this;
	return p -= p2;
}

BlockPos BlockDir::operator+(const BlockPos& pos) const {
	BlockPos p(pos.x + this->x, pos.z + this->z, pos.y + this->y);
	return p;
}

BlockPos BlockDir::operator-(const BlockPos& pos) const {
	BlockPos p(pos.x - this->x, pos.z - this->z, pos.y - this->y);
	return p;
}

bool BlockDir::operator==(const BlockDir& other) const {
	return x == other.x && z == other.z && y == other.y;
}

bool BlockDir::operator!=(const BlockDir& other) const {
	return !operator==(other);
}

extern const mc::BlockDir DIR_NORTH(0, -1, 0);
extern const mc::BlockDir DIR_SOUTH(0, 1, 0);
extern const mc::BlockDir DIR_EAST(1, 0, 0);
extern const mc::BlockDir DIR_WEST(-1, 0, 0);
extern const mc::BlockDir DIR_TOP(0, 0, 1);
extern const mc::BlockDir DIR_BOTTOM(0, 0, -1);

LocalBlockPos::LocalBlockPos() : x(0), z(0), y(0) {
}

LocalBlockPos::LocalBlockPos(int x, int z, int y) : x(x), z(z), y(y) {
}

LocalBlockPos::LocalBlockPos(const BlockPos& pos) : x(pos.x & 15), z(pos.z & 15), y(pos.y) {
}

BlockPos LocalBlockPos::toGlobalPos(const ChunkPos& chunk) const {
	return BlockPos(x + chunk.x * 16, z + chunk.z * 16, y);
}

std::ostream& operator<<(std::ostream& stream, const RegionPos& region) {
	stream << region.x << ":" << region.z;
	return stream;
}

std::ostream& operator<<(std::ostream& stream, const ChunkPos& chunk) {
	stream << chunk.x << ":" << chunk.z;
	return stream;
}

std::ostream& operator<<(std::ostream& stream, const BlockPos& block) {
	stream << block.x << ":" << block.z << ":" << block.y;
	return stream;
}

std::ostream& operator<<(std::ostream& stream, const LocalBlockPos& block) {
	stream << block.x << ":" << block.z << ":" << block.y;
	return stream;
}

}  // namespace mc
}  // namespace mapcrafter
