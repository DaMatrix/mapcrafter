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

#include "../util/logging.h"
#include "../util/other.h"
#include "blockatlas.h"
#include "image.h"

namespace mapcrafter {
namespace renderer {

// Singleton pointer
BlockAtlas* BlockAtlas::instance_ptr = NULL;

/*
 * Load a picture and associated text file to populate the atlas with
 * all the necessary graphic blocks to
 * render all tiles.
 */
bool BlockAtlas::OpenDictionnary(fs::path path, std::string name) {
	this->block_count = 0;
	this->block_ptrs.clear();
	this->shaded_blocks.clear();

	fs::path info_file  = path / (name + ".txt");
	fs::path block_file = path / (name + ".png");

	if (!fs::is_regular_file(info_file)) {
		LOG(ERROR) << "Unable to load block images: Block info file " << info_file << " does not exist!";
		return false;
	}
	if (!fs::is_regular_file(block_file)) {
		LOG(ERROR) << "Unable to load block images: Block image file " << block_file << " does not exist!";
		return false;
	}

	bool     ok      = false;
	uint32_t columns = 96;
	block_width      = 32;
	block_height     = 32;

	std::string first_line;
	try {
		std::vector<std::string> parts;
		std::ifstream            in(info_file.string());
		std::getline(in, first_line);
		parts = util::split(util::trim(first_line), ' ');
		if (parts.size() == 3) {
			block_width  = util::as<uint32_t>(parts[0]);
			block_height = util::as<uint32_t>(parts[1]);
			columns      = util::as<uint32_t>(parts[2]);
			ok           = true;
		}
	} catch (std::invalid_argument& e) {
	}
	if (!ok) {
		LOG(ERROR) << "Invalid first line in block info file " << info_file << "!";
		LOG(ERROR) << "Line 1: '" << first_line << "'";
		return false;
	}

	RGBAImage blocks_atlas;
	try {
		blocks_atlas.readPNG(block_file.string());
	} catch (const std::exception& e) {
		LOG(ERROR) << "Unable to load block images: Block image file " << block_file << " not readable! " << e.what();
		return false;
	}

	// Cut the whole block atlas
	uint32_t blocks_x = blocks_atlas.getWidth() / block_width;
	uint32_t blocks_y = blocks_atlas.getHeight() / block_height;
	if (blocks_x > columns) {
		LOG(ERROR) << "Block atlas doesn't match image index file";
		return false;
	}
	this->block_count = blocks_x * blocks_y;
	this->block_ptrs.reserve(this->block_count);
	this->shaded_blocks.reserve(this->block_count);
	uint32_t x = 0, y = 0;
	while (y <= blocks_y) {
		std::shared_ptr<RGBAImage> ptr = std::make_shared<RGBAImage>();
		*ptr = blocks_atlas.clip(x * block_width, y * block_height, block_width, block_height);
		this->block_ptrs.emplace_back(ptr);
		x++;
		if (x >= blocks_x) {
			x = 0;
			y++;
		}
	}
	return true;
}

const RGBAImage& BlockAtlas::GetImage(uint32_t idx) {
	if (idx >= this->block_count) {
		throw std::runtime_error("Block atlas doesn't match image index file");
	}
	return *this->block_ptrs[idx];
}

void BlockAtlas::ShadeBlock(int idx, int uv_idx, float factor_left, float factor_right, float factor_up) {
	if (this->shaded_blocks.find(idx) != this->shaded_blocks.end()) {
		return;
	}
	shaded_blocks.insert(idx);

	RGBAImage&       block   = *this->block_ptrs[idx];
	const RGBAImage& uv_mask = *this->block_ptrs[uv_idx];

	assert(block.getWidth() == uv_mask.getWidth());
	assert(block.getHeight() == uv_mask.getHeight());

	for (int x = 0; x < block.getWidth(); x++) {
		for (int y = 0; y < block.getHeight(); y++) {
			uint32_t& pixel    = block.pixel(x, y);
			uint32_t  uv_pixel = uv_mask.pixel(x, y);
			if (rgba_alpha(uv_pixel) == 0) {
				continue;
			}

			uint8_t side = rgba_blue(uv_pixel);
			if (side == FACE_LEFT_INDEX) {
				pixel = rgba_multiply(pixel, factor_left, factor_left, factor_left);
			}
			if (side == FACE_RIGHT_INDEX) {
				pixel = rgba_multiply(pixel, factor_right, factor_right, factor_right);
			}
			if (side == FACE_UP_INDEX) {
				pixel = rgba_multiply(pixel, factor_up, factor_up, factor_up);
			}
		}
	}
}

}  // namespace renderer
}  // namespace mapcrafter
