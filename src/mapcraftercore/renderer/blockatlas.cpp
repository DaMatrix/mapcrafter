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

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace mapcrafter {
namespace renderer {

BlockAtlas BlockAtlas::_instance = {};

/*
 * Load a picture and associated text file to populate the atlas with
 * all the necessary graphic blocks to
 * render all tiles.
 */
bool BlockAtlas::OpenDictionnary(fs::path path, std::string name) {
	this->images.clear();
	this->uv_images.clear();
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

	if (!blocks_atlas.readPNG(block_file.string())) {
		LOG(ERROR) << "Unable to load block images: Block image file " << block_file << " not readable!";
		return false;
	}

	// Cut the whole block atlas
	uint32_t blocks_x = blocks_atlas.getWidth() / block_width;
	uint32_t blocks_y = blocks_atlas.getHeight() / block_height;
	if (blocks_x > columns) {
		LOG(ERROR) << "Block atlas doesn't match image index file";
		return false;
	}
	auto block_count = blocks_x * blocks_y;
	this->images.reserve(block_count);
	this->shaded_blocks.reserve(block_count);
	uint32_t x = 0, y = 0;
	while (y <= blocks_y) {
		this->images.emplace_back(new RGBAImage(blocks_atlas.clip(x * block_width, y * block_height, block_width, block_height)));
		x++;
		if (x >= blocks_x) {
			x = 0;
			y++;
		}
	}
	return true;
}

void BlockAtlas::MarkUvTextures(const std::unordered_set<uint32_t>& uv_indices) {
	if (!this->uv_images.empty()) {
		throw std::runtime_error{"UV textures already processed!"};
	}

	this->uv_images.resize(this->images.size());
	for (auto uv_idx : uv_indices) {
		auto& raw_image = this->images.at(uv_idx);
		assert(raw_image != nullptr && "raw image was already consumed???");

		auto& uv_image = this->uv_images[uv_idx];
		assert(uv_image == nullptr && "UV image was already constructed???");

		uv_image.reset(new UVImage(*raw_image));
		raw_image.reset();
	}
}

const RGBAImage& BlockAtlas::GetImage(uint32_t idx) const {
	auto* ptr = this->images.at(idx).get();
	if (ptr == nullptr) {
		throw std::invalid_argument{"given index does not refer to a block image"};
	}
	return *ptr;
}

const UVImage& BlockAtlas::GetUVImage(uint32_t idx) const {
	auto* ptr = this->uv_images.at(idx).get();
	if (ptr == nullptr) {
		throw std::invalid_argument{"given index does not refer to a UV image"};
	}
	return *ptr;
}

void BlockAtlas::ShadeBlock(uint32_t idx, uint32_t uv_idx, float factor_left, float factor_right, float factor_up) {
	if (!this->shaded_blocks.insert(idx).second) {
		return;
	}

	assert(this->images.at(idx) != nullptr);
	RGBAImage& block = *this->images.at(idx);
	const UVImage& uv_mask = this->GetUVImage(uv_idx);

	assert(block.getWidth() == uv_mask.getWidth());
	assert(block.getHeight() == uv_mask.getHeight());

	std::transform(
			block.data.begin(), block.data.end(), uv_mask.data.begin(), block.data.begin(),
			[factor_left, factor_right, factor_up](RGBAPixel pixel, UVPixel uv_pixel) -> RGBAPixel {
				if (!uv_pixel.isFullyTransparent()) {
					switch (uv_pixel.getFace()) {
						case FACE_LEFT_INDEX:
							pixel = rgba_multiply(pixel, factor_left, factor_left, factor_left);
							break;
						case FACE_RIGHT_INDEX:
							pixel = rgba_multiply(pixel, factor_right, factor_right, factor_right);
							break;
						case FACE_UP_INDEX:
							pixel = rgba_multiply(pixel, factor_up, factor_up, factor_up);
							break;
					}
				}
				return pixel;
			});
}

}  // namespace renderer
}  // namespace mapcrafter
