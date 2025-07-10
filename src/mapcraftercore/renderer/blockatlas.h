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

#ifndef BLOCKATLAS_H_
#define BLOCKATLAS_H_

#include <boost/filesystem.hpp>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace fs = boost::filesystem;

namespace mapcrafter {

namespace mc {
class BlockState;
class BlockStateRegistry;
}  // namespace mc

namespace renderer {

class RGBAImage;

// TODO rename these maybe
enum FaceIndex : uint8_t {
	FACE_LEFT_INDEX = 0,
	FACE_RIGHT_INDEX = 1,
	FACE_UP_INDEX = 2,
};
static const uint8_t FACE_LEFT_COLOR  = ((float)255.0 / 6.0) * 1;
static const uint8_t FACE_RIGHT_COLOR = ((float)255.0 / 6.0) * 4;
static const uint8_t FACE_UP_COLOR    = ((float)255.0 / 6.0) * 2;

class BlockAtlas {
  private:
	static BlockAtlas _instance;
	BlockAtlas(){};

  public:
	static BlockAtlas& instance() {
		return _instance;
	}

	bool OpenDictionnary(fs::path path, std::string block_file);

	uint32_t const                         GetCount() { return this->block_count; };
	std::shared_ptr<const RGBAImage> const GetImage(uint32_t idx);

	void ShadeBlock(int idx, int uv_idx, float factor_left, float factor_right, float factor_up);

	uint32_t GetBlockWidth() const { return block_width; };
	uint32_t GetBlockHeight() const { return block_width; };

  private:
	std::vector<std::shared_ptr<RGBAImage> > block_ptrs;
	std::shared_ptr<RGBAImage>               unknown_block;
	std::unordered_set<uint16_t>             shaded_blocks;
	uint32_t                                 block_count;
	uint32_t                                 block_width;
	uint32_t                                 block_height;
};

}  // namespace renderer
}  // namespace mapcrafter

#endif /* BLOCKATLAS_H_ */
