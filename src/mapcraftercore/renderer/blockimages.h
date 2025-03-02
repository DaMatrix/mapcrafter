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

#ifndef BLOCKIMAGES_H_
#define BLOCKIMAGES_H_

#include "blockatlas.h"
#include "image.h"
#include "../mc/pos.h"
#include "../util/simd.h" //HAVE_EXPLICIT_SIMD, HAVE_ATTRIBUTE_TARGET_AVX2
#include <generated/config.h> //AUTO_TARGET_CLONES

#include <boost/filesystem.hpp>
#include <array>
#include <memory> //std::unique_ptr
#include <string>
#include <cstdint>

namespace fs = boost::filesystem;

namespace mapcrafter {

namespace mc {
class BlockState;
class BlockStateRegistry;
}

namespace renderer {

class Biome;

enum class ColorMapType {
	GRASS,
	FOLIAGE,
	FOLIAGE_FLIPPED,
	WATER
};

struct ColorMap {
	ColorMap()
		: colors({0, 0, 0}) {}

	bool parse(const std::string& str);
	uint32_t getColor(float x, float y) const;

	// colors of colormap triangle points
	std::array<uint32_t, 3> colors;
};

/**
 * This is an interface for a class responsible for generating and managing the block
 * images which are required to render a map.
 */
class BlockImages {
public:
	virtual ~BlockImages();

	virtual int getBlockSize() const = 0;
};

typedef std::array<float, 4> CornerValues;

struct LightFnc {
	constexpr static unsigned NUM_SAMPLES = 256;

    std::array<NormalizedUInt8, NUM_SAMPLES> lookup_u8;
    std::array<uint32_t, NUM_SAMPLES> lookup_u32;

    LightFnc() = default;

    template<typename G>
    explicit LightFnc(G generator) {
        static_assert(
                std::is_same<decltype(generator(0.0f)), uint8_t>::value,
                "generator function must return a uint8_t");

	    // Pre-process the lighting functions
        for (unsigned x = 0; x < NUM_SAMPLES; x++) {
            uint8_t value = generator(static_cast<float>(x) / static_cast<float>(NUM_SAMPLES - 1));
            lookup_u8[x] = NormalizedUInt8(value);
            lookup_u32[x] = value;
        }
    }
};

#if HAVE_EXPLICIT_SIMD && __x86_64__ && !__AVX2__ && HAVE_ATTRIBUTE_TARGET_AVX2
__attribute__((target("avx2")))
void blockImageMultiply(RGBAImage& block, const RGBAImage& uv_mask,
		const CornerValues& factors_left, const CornerValues& factors_right, const CornerValues& factors_up,
		const LightFnc& light_fnc);
__attribute__((target("default")))
#endif
void blockImageMultiply(RGBAImage& block, const RGBAImage& uv_mask,
		const CornerValues& factors_left, const CornerValues& factors_right, const CornerValues& factors_up,
		const LightFnc& light_fnc);

AUTO_TARGET_CLONES void blockImageMultiplyScalarKeepAlpha(RGBAImage& block, NormalizedUInt8 factor);
AUTO_TARGET_CLONES void blockImageMultiplyScalarKeepAlphaExceptFace(RGBAImage& block, const RGBAImage& uv_mask,
		FaceIndex except_face, float factor);

void blockImageMultiplyKeepAlpha(RGBAImage& block, RGBAPixel color);
void blockImageMultiplyWithAlpha(RGBAImage& block, RGBAPixel color);

AUTO_TARGET_CLONES void blockImageMultiplyKeepAlphaInto(RGBAImage& dst, const RGBAImage& src, RGBAPixel color);
AUTO_TARGET_CLONES void blockImageMultiplyWithAlphaInto(RGBAImage& dst, const RGBAImage& src, RGBAPixel color);

AUTO_TARGET_CLONES void blockImageMultiplyKeepAlphaMasked(RGBAImage& block, const RGBAImage& mask, RGBAPixel color);

AUTO_TARGET_CLONES void blockImageTintHighContrast(RGBAImage& block, RGBAPixel color);
AUTO_TARGET_CLONES void blockImageTintHighContrast(RGBAImage& block, const RGBAImage& mask, FaceIndex face, RGBAPixel color);

AUTO_TARGET_CLONES void blockImageStripSidesInto(RGBAImage& dst, const RGBAImage& src, const RGBAImage& uv_mask,
	bool strip_up, bool strip_left, bool strip_right);

void blockImageBlendZBuffered(RGBAImage& block, const RGBAImage& uv_mask,
		const RGBAImage& top, const RGBAImage& top_uv_mask);
void blockImageShadowEdges(RGBAImage& block, const RGBAImage& uv_mask,
		uint8_t north, uint8_t south, uint8_t east, uint8_t west, uint8_t bottomleft, uint8_t bottomright);
AUTO_TARGET_CLONES bool blockImageIsTransparent(const RGBAImage& block, const RGBAImage& uv_mask);
AUTO_TARGET_CLONES std::array<bool, 3> blockImageGetSideMask(const RGBAImage& uv);

enum class LightingType {
	NONE,
	SIMPLE,
	SMOOTH,
	SMOOTH_TOP_REMAINING_SIMPLE,
	SMOOTH_BOTTOM,
};

struct BlockImage {
	// TODO
	// this needs some order and refactoring
	BlockImage()
		: lighting_specified(false) {}

	std::array<bool, 3> side_mask;
	bool is_transparent;
	bool is_empty;

	bool is_biome;
	bool is_masked_biome;
	ColorMapType biome_color;
	ColorMap biome_colormap;
	const RGBAImage* biome_mask;

	bool is_waterlogged;

	bool can_partial;

	bool lighting_specified;
	LightingType lighting_type;
	bool has_faulty_lighting;

	int shadow_edges;
	double_t weight_total;
	double_t weight_factor;

	const int variant_2_index(int32_t variant) const {
		double_t _dummy_;
		double_t weight = modf(double_t(abs(variant)) * weight_factor, &_dummy_);
		int i = 0;
		while (true) {
			weight -= images_weights[i];
			if (weight < 0) break;
			i ++;
		}
		assert(i<(int32_t)images_idx.size());
		return i;
	}

	const RGBAImage& image(int32_t idx) const {
		assert(idx<(int32_t)images_idx.size());
		return BlockAtlas::instance().GetImage(images_idx[idx]);
	}
	void image(std::vector<uint32_t>& indexes) {
		images_idx = indexes;
	}
	const RGBAImage& uv_image(int32_t idx) const {
		assert(idx<(int32_t)images_idx.size());
		return BlockAtlas::instance().GetImage(uv_images_idx[idx]);
	}
	void uv_image(std::vector<uint32_t>& indexes) {
		uv_images_idx = indexes;
	}
	void weight_image(std::vector<uint32_t>& weights, double_t factor) {
		images_weights = std::vector<double_t>(weights.size());
		for (size_t i = 0; i < weights.size(); i++)
		{
			images_weights[i] = factor * weights[i];
		}
		weight_factor = factor;
	}

	std::vector<uint32_t> images_idx;
	std::vector<uint32_t> uv_images_idx;
	std::vector<double_t> images_weights;
};

class RenderedBlockImages : public BlockImages {
public:
	// OLD METHODS
	virtual void setRotation(int rotation) {}
	virtual void setRenderSpecialBlocks(bool render_unknown_blocks,
			bool render_leaves_transparent) {}
	//virtual RGBAImage exportBlocks() const {}
	virtual bool isBlockTransparent(uint16_t id, uint16_t data) const { return false; };
	virtual bool hasBlock(uint16_t id, uint16_t) const { return true; };
	virtual const RGBAImage& getBlock(uint16_t id, uint16_t data, uint16_t extra_data = 0) const { return unknown_block.image(0); };
	virtual RGBAImage getBiomeBlock(uint16_t id, uint16_t data, const Biome& biome, uint16_t extra_data = 0) const { return unknown_block.image(0); };
	virtual int getMaxWaterPreblit() const { return 0; };
	//virtual int getBlockSize() const {};

	RenderedBlockImages(mc::BlockStateRegistry& block_registry);

	void setBlockSideDarkening(float darken_left, float darken_right);

	bool loadBlockImages(fs::path block_dir, std::string view, int rotation, int texture_size);
	virtual RGBAImage exportBlocks() const;

	const BlockImage& getBlockImage(uint16_t id) const;
	void prepareBiomeBlockImage(RGBAImage& image, const BlockImage& block, RGBAPixel color);

	virtual int getTextureSize() const;
	virtual int getBlockSize() const;
	virtual int getBlockWidth() const;
	virtual int getBlockHeight() const;

private:
	void prepareBlockImages();
	void runBenchmark();

	mc::BlockStateRegistry& block_registry;

	float darken_left, darken_right;

	int texture_size;
	int block_width, block_height;
	// Mapcrafter-local block ID -> BlockImage (image, uv_image, is_transparent, ...)
	std::vector<std::unique_ptr<BlockImage>> block_images;
	BlockImage unknown_block;
};

}
}

#endif /* BLOCKIMAGES_H_ */
