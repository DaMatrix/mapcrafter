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
class UVImage;

class BlockAtlas {
  private:
	static BlockAtlas _instance;
	BlockAtlas(){};

  public:
	static BlockAtlas& instance() {
		return _instance;
	}

	bool OpenDictionnary(fs::path path, std::string block_file);
	void MarkUvTextures(const std::unordered_set<uint32_t>& uv_indices);

	uint32_t GetCount() const noexcept { return this->images.size(); };
	const RGBAImage& GetImage(uint32_t idx) const;
	const UVImage& GetUVImage(uint32_t idx) const;

	void ShadeBlock(uint32_t idx, uint32_t uv_idx, float factor_left, float factor_right, float factor_up);

	uint32_t GetBlockWidth() const { return block_width; };
	uint32_t GetBlockHeight() const { return block_width; };

  private:
	std::vector<std::unique_ptr<RGBAImage>> images;
	std::vector<std::unique_ptr<UVImage>> uv_images;
	std::unordered_set<uint32_t> shaded_blocks;
	uint32_t block_width = 0;
	uint32_t block_height = 0;
};

}  // namespace renderer
}  // namespace mapcrafter

#endif /* BLOCKATLAS_H_ */
