/*
 * Copyright 2012-2014 Moritz Hilscher
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

#include "worldcrop.h"

#include "../util.h"

#include <cmath>
#include <sstream>

namespace mapcrafter {
namespace mc {

BlockMask::BlockMask() {
	// set all blocks to be shown by default
	block_states.resize(65536, BlockState::COMPLETLY_SHOWN);
	block_mask.set();
}

BlockMask::~BlockMask() {

}

void BlockMask::set(uint16_t id, bool shown) {
	//std::cout << "set(" << (int) id << " " << shown << ")" << std::endl;
	for (size_t i = 0; i < 16; i++)
		block_mask[16 * id + i] = shown;
	updateBlockState(id);
}

void BlockMask::set(uint16_t id, uint8_t data, bool shown) {
	//std::cout << "set(" << (int) id << ", " << (int) data << ", " << shown << ")" << std::endl;
	if (data >= 16)
		return;
	block_mask[16 * id + data] = shown;
	updateBlockState(id);
}

void BlockMask::set(uint16_t id, uint8_t data, uint8_t bitmask, bool shown) {
	// iterate through every possible block data values and check if (i % bitmask) == data
	for (uint8_t i = 0; i < 16; i++)
		if ((i & bitmask) == data) {
			block_mask[16 * id + i] = shown;
			//std::cout << (int) id << " " << (int) i << std::endl;
		}
	updateBlockState(id);
}

void BlockMask::setRange(uint16_t id1, uint16_t id2, bool shown) {
	//std::cout << "setRange(" << (int) id1 << ", " << (int) id2 << ", " << shown << ")" << std::endl;
	for (size_t id = id1; id <= id2; id++)
		set(id, shown);
}

void BlockMask::setAll(bool shown) {
	//std::cout << "setAll(" << shown << ")" << std::endl;
	if (shown) {
		block_mask.set();
		std::fill(block_states.begin(), block_states.end(), BlockState::COMPLETLY_SHOWN);
	} else {
		block_mask.reset();
		std::fill(block_states.begin(), block_states.end(), BlockState::COMPLETLY_HIDDEN);
	}
}

bool BlockMask::loadFromString(const std::string& str) {
	std::stringstream ss(util::trim(str));
	std::string part;
	while (ss >> part) {
		bool shown = part[0] != '!';
		if (!shown)
			part = part.substr(1);
		std::cout << part << " " << shown << std::endl;
		if (part.find('-') != std::string::npos) {
			std::string part1 = part.substr(0, part.find('-'));
			std::string part2 = part.substr(part.find('-') + 1);
			setRange(util::as<uint16_t>(part1), util::as<uint16_t>(part2), shown);
		} else if (part.find(':') != std::string::npos) {
			std::string part1 = part.substr(0, part.find(':'));
			std::string part2 = part.substr(part.find(':') + 1);
			set(util::as<uint16_t>(part1), util::as<uint16_t>(part2), shown);
		} else {
			if (part == "*")
				setAll(shown);
			else
				set(util::as<uint16_t>(part), shown);
		}
	}
	return true;
}

const BlockMask::BlockState BlockMask::getBlockState(uint16_t id) const {
	return block_states[id];
}

bool BlockMask::isHidden(uint16_t id, uint8_t data) const {
	if (data >= 16)
		return false;
	return !block_mask[16 * id + data];
}

void BlockMask::updateBlockState(uint16_t id) {
	// copy state of blocks to separate bitset to make checking them all easier
	std::bitset<16> block;
	for (size_t i = 0; i < 16; i++)
		block[i] = block_mask[16 * id + i];

	if (block.all())
		block_states[id] = BlockState::COMPLETLY_SHOWN;
	else if (block.none())
		block_states[id] = BlockState::COMPLETLY_HIDDEN;
	else
		block_states[id] = BlockState::PARTIALLY_HIDDEN_SHOWN;
}

WorldCrop::WorldCrop()
	: type(RECTANGULAR), radius(0), has_block_mask(false) {
}

WorldCrop::~WorldCrop() {
}

void WorldCrop::setMinY(int value) {
	bounds_y.setMin(value);
}

void WorldCrop::setMaxY(int value) {
	bounds_y.setMax(value);
}

void WorldCrop::setMinX(int value) {
	bounds_x.setMin(value);
	bounds_chunk_x.setMin(util::floordiv(value, 16));
	bounds_region_x.setMin(util::floordiv(value, 512));

	type = RECTANGULAR;
}

void WorldCrop::setMaxX(int value) {
	bounds_x.setMax(value);
	bounds_chunk_x.setMax(util::floordiv(value, 16));
	bounds_region_x.setMax(util::floordiv(value, 512));

	type = RECTANGULAR;
}

void WorldCrop::setMinZ(int value) {
	bounds_z.setMin(value);
	bounds_chunk_z.setMin(util::floordiv(value, 16));
	bounds_region_z.setMin(util::floordiv(value, 512));

	type = RECTANGULAR;
}

void WorldCrop::setMaxZ(int value) {
	bounds_z.setMax(value);
	bounds_chunk_z.setMax(util::floordiv(value, 16));
	bounds_region_z.setMax(util::floordiv(value, 512));

	type = RECTANGULAR;
}

void WorldCrop::setCenter(const BlockPos& pos) {
	center = pos;
	type = CIRCULAR;
}

void WorldCrop::setRadius(int radius) {
	this->radius = radius;
	type = CIRCULAR;
}

bool WorldCrop::isRegionContained(const mc::RegionPos& region) const {
	if (type == RECTANGULAR) {
		// rectangular crop:
		// just check if the region is contained in the calculated bounds
		return bounds_region_x.contains(region.x) && bounds_region_z.contains(region.z);
	} else if (type == CIRCULAR) {
		// circular crop:
		// check roughly whether at least one block of the region is included
		// use the midpoint of the region and determine the distance to the center
		BlockPos region_center(region.x * 512 + 256, region.z * 512 + 256, 0);
		int dx = region_center.x - center.x;
		int dz = region_center.z - center.z;
		// and check whether it is at most radius + size of a region blocks away
		return (radius+512)*(radius+512) >= dx*dx + dz*dz;
	}

	return true;
}

bool WorldCrop::isChunkContained(const mc::ChunkPos& chunk) const {
	if (type == RECTANGULAR) {
		// rectangular crop:
		// just check if the chunk is contained in the calculated chunk bounds
		return bounds_chunk_x.contains(chunk.x) && bounds_chunk_z.contains(chunk.z);
	} else if (type == CIRCULAR) {
		// circular crop:
		// do the same thing doing with regions
		BlockPos chunk_center(chunk.x * 16 + 8, chunk.z * 16 + 8, 0);
		int dx = chunk_center.x - center.x;
		int dz = chunk_center.z - center.z;
		return (radius+16)*(radius+16) >= dx*dx + dz*dz;
	}

	return true;
}

bool WorldCrop::isChunkCompletelyContained(const mc::ChunkPos& chunk) const {
	mc::BlockPos corner(chunk.x * 16, chunk.z * 16, 0);
	return isBlockContainedXZ(corner)
			&& isBlockContainedXZ(corner + mc::BlockPos(15, 0, 0))
			&& isBlockContainedXZ(corner + mc::BlockPos(0, 15, 0))
			&& isBlockContainedXZ(corner + mc::BlockPos(15, 15, 0));
}

bool WorldCrop::isBlockContainedXZ(const mc::BlockPos& block) const {
	if (type == RECTANGULAR) {
		// rectangular crop:
		// just check if the chunk is contained in the bounds
		return bounds_x.contains(block.x) && bounds_z.contains(block.z);
	} else if (type == CIRCULAR) {
		// circular crop:
		// also check distance to the center (like with regions and chunks)
		int dx = block.x - center.x;
		int dz = block.z - center.z;
		return radius*radius >= dx*dx + dz*dz;
	}

	return true;
}

bool WorldCrop::isBlockContainedY(const mc::BlockPos& block) const {
	return bounds_y.contains(block.y);
}

bool WorldCrop::hasBlockMask() const {
	return has_block_mask;
}

const BlockMask& WorldCrop::getBlockMask() const {
	return *block_mask;
}

void WorldCrop::initBlockMask() {
	has_block_mask = false;
	block_mask.reset(new BlockMask);
	block_mask->setAll(true);
	/*
	block_mask->setAll(true);
	block_mask->setRange(1, 3, false); // stone, dirt, grass
	block_mask->setRange(7, 9, false); // bedrock, water
	block_mask->setRange(12, 13, false); // sand, gravel
	block_mask->setRange(15, 16, false); // iron, coal
	block_mask->set(24, false); // sandstone
	*/

	/*
	block_mask->set(17, 0, false); // jungle wood
	block_mask->set(17, 4, false); // jungle wood
	block_mask->set(17, 8, false); // jungle wood
	block_mask->set(17, 4 | 8, false); // jungle wood
	*/

	/*
	//block_mask->set(17, 3, false); // jungle wood
	//block_mask->set(17, 3 | 4, false); // jungle wood
	//block_mask->set(17, 3 | 8, false); // jungle wood
	//block_mask->set(17, 3 | 4 | 8, false); // jungle wood
	//block_mask->set(18, 3, false); // jungle leaves
	//block_mask->set(18, 3 | 4, false); // jungle leaves
	//block_mask->set(18, 3 | 8, false); // jungle leaves
	//block_mask->set(18, 3 | 4 | 8, false); // jungle leaves

	block_mask->set(17, 3, util::binary<11>::value, false);
	block_mask->set(18, 3, util::binary<11>::value, false);

	block_mask->set(106, false); // vines
	*/

	/*
	block_mask->setRange(2, 3, false);
	block_mask->setRange(8, 9, false);
	block_mask->setRange(12, 13, false);
	block_mask->setRange(17, 18, false);
	block_mask->set(24, false);
	block_mask->set(31, false);
	block_mask->setRange(78, 79, false);
	block_mask->set(82, false);
	block_mask->set(106, false);
	block_mask->set(161, false);
	*/

	//block_mask->loadFromString("* !17:2 !17:6 !17:10 !17:14 !18:2 !18:6 !18:10 !18:14");
	//block_mask->loadFromString("* !2-3  !8-9 !12-13  !17-18 !24 !31   !78-79 !82 !106 !161");
}

}
}