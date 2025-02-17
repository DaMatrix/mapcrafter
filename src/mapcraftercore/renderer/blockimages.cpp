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

#include "blockimages.h"

#include "biomes.h"
#include "../util.h"
#include "../mc/blockstate.h"
#include "../mc/chunk.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <vector>

namespace mapcrafter {
namespace util {

template <>
renderer::ColorMapType as<renderer::ColorMapType>(const std::string& str) {
	if (str == "foliage") {
		return renderer::ColorMapType::FOLIAGE;
	} else if (str == "foliage_flipped") {
		return renderer::ColorMapType::FOLIAGE_FLIPPED;
	} else if (str == "grass") {
		return renderer::ColorMapType::GRASS;
	} else if (str == "water") {
		return renderer::ColorMapType::WATER;
	} else {
		throw std::invalid_argument("Must be 'foliage', 'foliage_flipped', 'grass' or 'water'!");
	}
}

template <>
renderer::LightingType as<renderer::LightingType>(const std::string& str) {
	if (str == "none") {
		return renderer::LightingType::NONE;
	} else if (str == "simple") {
		return renderer::LightingType::SIMPLE;
	} else if (str == "smooth") {
		return renderer::LightingType::SMOOTH;
	} else if (str == "smooth_bottom") {
		return renderer::LightingType::SMOOTH_BOTTOM;
	} else {
		throw std::invalid_argument("Must be 'none', 'simple' or 'smooth'!");
	}
}

}
}

namespace mapcrafter {
namespace renderer {

bool ColorMap::parse(const std::string& str) {
	std::vector<std::string> parts = util::split(str, '|');
	if (parts.size() != 3) {
		return false;
	}

	for (size_t i = 0; i < 3; i++) {
		std::string part = parts[i];
		if (part.size() != 9 || part[0] != '#' || !util::isHexNumber(part.substr(1))) {
			return false;
		}
		colors[i] = util::parseHexNumber(part.substr(1));
	}

	return true;
}

uint32_t ColorMap::getColor(float x, float y) const {
	float r = 0, g = 0, b = 0, a = 0;
	// factors are barycentric coordinates
	// colors are colors of the colormap triangle points
	float factors[] = {
		x - y,
		1.0f - x,
		y
	};

	for (size_t i = 0; i < 3; i++) {
		r += (float) rgba_red(colors[i]) * factors[i];
		g += (float) rgba_green(colors[i]) * factors[i];
		b += (float) rgba_blue(colors[i]) * factors[i];
		a += (float) rgba_alpha(colors[i]) * factors[i];
	}

	return rgba(r, g, b, a);
}

BlockImages::~BlockImages() {
}

void blockImageTest(RGBAImage& block, const RGBAImage& uv_mask) {
	assert(block.isSameSize(uv_mask));

	for (int x = 0; x < block.getWidth(); x++) {
		for (int y = 0; y < block.getHeight(); y++) {
			uint32_t& pixel = block.pixel(x, y);
			uint32_t uv_pixel = uv_mask.pixel(x, y);
			if (rgba_alpha(uv_pixel) == 0) {
				continue;
			}

			uint8_t side = rgba_blue(uv_pixel);
			if (side == FACE_LEFT_INDEX) {
				pixel = rgba(255, 0, 0);
			}
			if (side == FACE_RIGHT_INDEX) {
				pixel = rgba(0, 255, 0);
			}
			if (side == FACE_UP_INDEX) {
				pixel = rgba(0, 0, 255);;
			}
		}
	}
}

void blockImageMultiplyExcept(RGBAImage& block, const RGBAImage& uv_mask,
		uint8_t except_face, float factor) {
	assert(block.isSameSize(uv_mask));

	for (int x = 0; x < block.getWidth(); x++) {
		for (int y = 0; y < block.getHeight(); y++) {
			uint32_t& pixel = block.pixel(x, y);
			uint32_t uv_pixel = uv_mask.pixel(x, y);
			if (rgba_alpha(uv_pixel) == 0) {
				continue;
			}

			uint8_t side = rgba_blue(uv_pixel);
			if (side != except_face) {
				pixel = rgba_multiply(pixel, factor, factor, factor);
			}
		}
	}
}

namespace {

inline uint32_t mix(uint32_t x, uint32_t y, uint32_t a) {
	// >> 8 = / 256, serves as approximation for division by 255
	return ((x * (255-a)) + (y * a)) >> 8;
}

}

static std::array<uint32_t, 3> blockImageMultiply_PreprocessFactors(
        const CornerValues& factors_left, const CornerValues& factors_right, const CornerValues& factors_up) {
    std::array<uint32_t, 3> f{};
	for (int i = 0; i < 4; i++) {
		f[FACE_LEFT_INDEX] |= uint8_t(std::min(255.0f, (factors_left[i] * 255u))) << (i * 8);
		f[FACE_RIGHT_INDEX] |= uint8_t(std::min(255.0f, (factors_right[i] * 255u))) << (i * 8);
		f[FACE_UP_INDEX] |= uint8_t(std::min(255.0f, (factors_up[i] * 255u))) << (i * 8);
	}
    return f;
}

static void blockImageMultiply_scalar(
        RGBAPixel* block, const RGBAPixel* uv_mask, int i, int n,
        const std::array<uint32_t, 3>& f, const LightFnc& light_fnc) {
	for (; i < n; i++) {
        uint32_t pixel = block[i];
        uint32_t uv_pixel = uv_mask[i];
        if (uv_pixel != 0) {
            uint32_t u = rgba_red(uv_pixel);
            uint32_t v = rgba_green(uv_pixel);
            uint32_t side = rgba_blue(uv_pixel);

            uint32_t fv = f[side];

            // jetzt sogar 34.17
            // und mit noch mehr rgba_multiply sogar 35.28
            uint32_t ab = mix((fv >> (0 * 8)) & 0xFF, (fv >> (1 * 8)) & 0xFF, u); // divide255((255-u) * f[0], u * f[1]);
            uint32_t cd = mix((fv >> (2 * 8)) & 0xFF, (fv >> (3 * 8)) & 0xFF, u); // divide255((255-u) * f[2], u * f[3]);
            uint32_t x = mix(ab, cd, v); // divide255((255-v) * ab, v * cd);

            // apply light function
            x = light_fnc.lookup_u8[x];

            pixel = rgba_multiply_scalar(pixel, x);
        }
        block[i] = pixel;
	}
}

#if HAVE_EXPLICIT_SIMD && __x86_64__ && (__AVX2__ || HAVE_ATTRIBUTE_TARGET_AVX2)

#if !__AVX2__
#define ATTRIBUTE_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define ATTRIBUTE_TARGET_AVX2
#endif

ATTRIBUTE_TARGET_AVX2
static int blockImageMultiply_AVX2(
        RGBAPixel* block, const RGBAPixel* uv_mask, int i, int n,
        const std::array<uint32_t, 3>& f, const LightFnc& light_fnc) {
    using uint32x8 = simd::vec<uint32_t, 8>;

    auto mix = [](uint32x8 x, uint32x8 y, uint32x8 a) ATTRIBUTE_TARGET_AVX2 -> uint32x8 {
        // >> 8 = / 256, serves as approximation for division by 255
        return ((x * (255 - a)) + (y * a)) >> 8;
    };

    auto rgba_multiply_scalar = [](uint32x8 value, uint32x8 factor) ATTRIBUTE_TARGET_AVX2 -> uint32x8 {
        uint32x8 g = ((((value & 0xff00) + 0x0100) * factor) >> 8) & 0xff00;
        uint32x8 br = ((((value & 0xff00ff) + 0x010001) * factor) >> 8) & 0xff00ff;
        uint32x8 a = value & 0xff000000;
        return a | g | br;
    };

    uint32x8 f_lookup = { f[0], f[1], f[2], 0, 0, 0, 0, 0 };

    for (; i < n - 7; i += 8) {
        uint32x8 pixel = (uint32x8) _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&block[i]));
        uint32x8 uv_pixel = (uint32x8) _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&uv_mask[i]));

        if (_mm256_testz_si256((__m256i) uv_pixel, (__m256i) uv_pixel)) { //all pixels are zero
            continue;
        }

        uint32x8 u = uv_pixel & 0xFF;
        uint32x8 v = (uv_pixel >> 8) & 0xFF;
        uint32x8 side = (uv_pixel >> 16) & 0xFF;

        uint32x8 fv = (uint32x8) _mm256_permutevar8x32_epi32((__m256i) f_lookup, (__m256i) side);
        uint32x8 ab = mix((fv >> (0 * 8)) & 0xFF, (fv >> (1 * 8)) & 0xFF, u); // divide255((255-u) * f[0], u * f[1]);
        uint32x8 cd = mix((fv >> (2 * 8)) & 0xFF, (fv >> (3 * 8)) & 0xFF, u); // divide255((255-u) * f[2], u * f[3]);
        uint32x8 x = mix(ab, cd, v); // divide255((255-v) * ab, v * cd);

        // apply light function
        x = (uint32x8) _mm256_i32gather_epi32(reinterpret_cast<const int*>(light_fnc.lookup_u32.data()), (__m256i) x, 4);

        pixel = uv_pixel != 0 ? rgba_multiply_scalar(pixel, x) : pixel;

#ifndef NDEBUG
        //call the scalar function on this block of 8 values and check that the result matches
        blockImageMultiply_scalar(block + i, uv_mask + i, 0, 8, f, light_fnc);
        assert(std::memcmp(block + i, &pixel, sizeof(pixel)) == 0);
#endif

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&block[i]), (__m256i) pixel);
	}

    return i;
}

ATTRIBUTE_TARGET_AVX2
void blockImageMultiply(RGBAImage& block, const RGBAImage& uv_mask,
		const CornerValues& factors_left, const CornerValues& factors_right, const CornerValues& factors_up,
		const LightFnc& light_fnc) {
	assert(block.isSameSize(uv_mask));

    std::array<uint32_t, 3> f = blockImageMultiply_PreprocessFactors(factors_left, factors_right, factors_up);

	int n = block.getWidth() * block.getHeight();
    int i = 0;

    //vectorized element processing
    i = blockImageMultiply_AVX2(&block.data[0], &uv_mask.data[0], i, n, f, light_fnc);

    //process remaining elements
    blockImageMultiply_scalar(&block.data[0], &uv_mask.data[0], i, n, f, light_fnc);
}
#endif

#if !HAVE_EXPLICIT_SIMD || !__x86_64__ || !__AVX2__
#if HAVE_EXPLICIT_SIMD && __x86_64__ && !__AVX2__ && HAVE_ATTRIBUTE_TARGET_AVX2
__attribute__((target("default")))
#endif
void blockImageMultiply(RGBAImage& block, const RGBAImage& uv_mask,
		const CornerValues& factors_left, const CornerValues& factors_right, const CornerValues& factors_up,
		const LightFnc& light_fnc) {
	assert(block.isSameSize(uv_mask));

    std::array<uint32_t, 3> f = blockImageMultiply_PreprocessFactors(factors_left, factors_right, factors_up);

	int n = block.getWidth() * block.getHeight();

    blockImageMultiply_scalar(&block.data[0], &uv_mask.data[0], 0, n, f, light_fnc);
}
#endif

void blockImageMultiply(RGBAImage &block, uint8_t factor) {
	//trivially vectorizable loop
	std::transform(
		block.begin(), block.end(), block.begin(),
		[factor](RGBAPixel pixel) -> RGBAPixel {
			return rgba_multiply_scalar(pixel, factor);
		});
}

void blockImageTint(RGBAImage &block, const RGBAImage &mask, uint32_t color) {
	assert(block.isSameSize(mask));

	std::transform(
		block.begin(), block.end(), mask.begin(), block.begin(),
		[color](RGBAPixel pixel, RGBAPixel mask_pixel) -> RGBAPixel {
			if (rgba_alpha(mask_pixel)) {
				// The mask is not supposed to be transfered directly
				// but to be blend in with block pixel
				// This will avoid white pixels on edges of the mask
				RGBAPixel colored_mask_pixel = rgba_multiply(mask_pixel, color);
				blend(pixel, colored_mask_pixel);
			}
			return pixel;
		});
}

void blockImageTint(RGBAImage &block, uint32_t color) {
	//trivially vectorizable loop
	std::transform(
		block.begin(), block.end(), block.begin(),
		[color](RGBAPixel pixel) -> RGBAPixel {
			return rgba_alpha(pixel) ? rgba_multiply(pixel, color) : pixel;
		});
}

void blockImageTintHighContrast(RGBAImage& block, uint32_t color) {
	// get luminance of recolor:
	// "10*r + 3*g + b" should actually be "3*r + 10*g + b"
	// it was a typo, but doesn't look bad either
	int luminance = (10 * rgba_red(color) + 3 * rgba_green(color) + rgba_blue(color)) / 14;

	float alpha_factor = 3; // 3 is similar to alpha=85
	// something like that would be possible too, but overlays won't look exactly like
	// overlays with that alpha value, so don't use it for now
	// alpha_factor = (float) 255.0 / rgba_alpha(color);

	// try to do luminance-neutral additive/subtractive color
	// instead of alpha blending (for better contrast)
	// so first subtract luminance from each component
	int nr = (rgba_red(color) - luminance) / alpha_factor;
	int ng = (rgba_green(color) - luminance) / alpha_factor;
	int nb = (rgba_blue(color) - luminance) / alpha_factor;

	//trivially vectorizable loop
	std::transform(
		block.begin(), block.end(), block.begin(),
		[nr, ng, nb](RGBAPixel pixel) -> RGBAPixel {
			return rgba_alpha(pixel)
				       ? rgba_add_clamp(pixel, nr, ng, nb, 0)
				       : pixel;
		});
}

void blockImageTintHighContrast(RGBAImage& block, const RGBAImage& mask, uint8_t face, uint32_t color) {
	assert(block.isSameSize(mask));

	// same as above
	int luminance = (10 * rgba_red(color) + 3 * rgba_green(color) + rgba_blue(color)) / 14;
	float alpha_factor = 3;
	int nr = (rgba_red(color) - luminance) / alpha_factor;
	int ng = (rgba_green(color) - luminance) / alpha_factor;
	int nb = (rgba_blue(color) - luminance) / alpha_factor;

	//trivially vectorizable loop
	std::transform(
		block.begin(), block.end(), mask.begin(), block.begin(),
		[face, nr, ng, nb](RGBAPixel pixel, RGBAPixel mask_pixel) -> RGBAPixel {
			return rgba_blue(mask_pixel) == face
				       ? rgba_add_clamp(pixel, nr, ng, nb, 0)
				       : pixel;
		});
}

void blockImageBlendZBuffered(RGBAImage& block, const RGBAImage& uv_mask,
		const RGBAImage& top, const RGBAImage& top_uv_mask) {
	assert(block.isSameSize(uv_mask));
	assert(block.isSameSize(top));
	assert(block.isSameSize(top_uv_mask));

	size_t n = block.getPixelCount();
	for (size_t i = 0; i < n; i++) {
		RGBAPixel& pixel = block.data[i];
		const RGBAPixel& uv_pixel = uv_mask.data[i];
		const RGBAPixel& top_pixel = top.data[i];
		const RGBAPixel& top_uv_pixel = top_uv_mask.data[i];

		// basically what we want to do is:
		// compare uv-coords of block vs. waterlog pixels
		// if the uv-coords are the same and both textures pointing up, don't show water here

		// use the Z value of each pixels to blend or not the top pixel
		if (rgba_alpha(uv_pixel) < rgba_alpha(top_uv_pixel)) {
			blend(pixel, top_pixel);
		} else {
			// The top pixel is behind the block one, so use the alpha of
			// the destination pixel to blend the top pixel behind
			RGBAPixel tmp_pix = pixel;
			pixel = top_pixel;
			blend(pixel, tmp_pix);
		}
	}
}

void blockImageShadowEdges(RGBAImage& block, const RGBAImage& uv_mask,
		uint8_t north, uint8_t south, uint8_t east, uint8_t west, uint8_t bottomleft, uint8_t bottomright) {
	assert(block.isSameSize(uv_mask));

	size_t n = block.getWidth() * block.getHeight();
	for (size_t i = 0; i < n; i++) {
		RGBAPixel& pixel = block.data[i];
		const RGBAPixel& uv_pixel = uv_mask.data[i];

		// TODO
		// not really optimized yet, and quite dirty code
		float u = (float) rgba_red(uv_pixel) / 255;
		float v = (float) rgba_green(uv_pixel) / 255;
		uint8_t face = rgba_blue(uv_pixel);

		uint8_t alpha = 0;
		#define setalpha(x) (alpha = std::max(alpha, (uint8_t) (x)))
		auto genalpha = [&alpha, &face](int mask_face, int edge, float uv) {
			// explanation of edge influence:
			// edge=0: no edge
			// edge=1: edge with threshold 2px
			// edge=2: edge with threshold 3px
			// edge=3: edge with threshold 3px, a bit darker (for stronger visual on leaves etc.)
			float t = (float) (1 + std::min(2, edge)) / 16.0;
			if (edge && face == mask_face && uv < t) {
				float strong = 48;
				float weak = 24;
				if (edge > 2) {
					strong = 96;
					weak = 48;
				}
				if (uv < t / 2.0) {
					setalpha(strong);
				} else {
					float a = (uv-t/2.0) / (t/2.0);
					setalpha((float) (1-a) * weak + a*16.0);
				}
			}
		};

		genalpha(FACE_UP_INDEX, north, v);
		genalpha(FACE_UP_INDEX, south, 1.0 - v);
		genalpha(FACE_UP_INDEX, east, 1.0 - u);
		genalpha(FACE_UP_INDEX, west, u);

		genalpha(FACE_LEFT_INDEX, bottomleft, 1.0 - v);
		genalpha(FACE_RIGHT_INDEX, bottomright, 1.0 - v);

		#undef setalpha

		pixel = rgba_multiply_scalar(pixel, 255 - alpha);
	}
}

bool blockImageIsTransparent(const RGBAImage& block, const RGBAImage& uv_mask) {
	assert(block.isSameSize(uv_mask));

	//this could be vectorized if we had access to std::any_of with c++17's std::execution::unseq
	auto block_it = block.begin();
	auto block_end = block.end();
	auto uv_it = uv_mask.begin();
	for (; block_it != block_end; ++block_it, ++uv_it) {
		auto pixel = *block_it;
		auto uv_pixel = *uv_it;

		if (rgba_alpha(uv_pixel) == 0 && rgba_alpha(pixel) != 255) {
			return true;
		}
	}
	return false;
}

std::array<bool, 3> blockImageGetSideMask(const RGBAImage& uv) {
	//first, reduce the side mask into a bitfield (this can be vectorized).
	//  this works because FACE_[LEFT|RIGHT|UP]_INDEX are 0/1/2 respectively, so we only have to do a quick shift.
	int side_mask_bits = 0;
	for (RGBAPixel pixel : uv) {
		if (rgba_alpha(pixel)) {
			auto face = rgba_blue(pixel);
			assert(face >= 0 && face < 3 && "Face index out-of-bounds!");
			side_mask_bits |= 1 << face;
		}
	}

	//extract the individual bits
	return {
		(side_mask_bits & (1 << FACE_LEFT_INDEX)) != 0,
		(side_mask_bits & (1 << FACE_RIGHT_INDEX)) != 0,
		(side_mask_bits & (1 << FACE_UP_INDEX)) != 0,
	};
}

RenderedBlockImages::RenderedBlockImages(mc::BlockStateRegistry& block_registry)
	: block_registry(block_registry), darken_left(1.0), darken_right(1.0) {
}

void RenderedBlockImages::setBlockSideDarkening(float darken_left, float darken_right) {
	this->darken_left = darken_left;
	this->darken_right = darken_right;
}

bool RenderedBlockImages::loadBlockImages(fs::path path, std::string view, int rotation, int texture_size) {
	LOG(INFO) << "I will load block images from " << path << " now";

	if (!fs::is_directory(path)) {
		LOG(ERROR) << "Unable to load block images: " << path << " is not a directory!";
		return false;
	}

	std::string name = view + "_" + util::str(rotation) + "_" + util::str(texture_size);

	BlockAtlas::instance().OpenDictionnary(path,name);

	fs::path info_file = path / (name + ".txt");

	if (!fs::is_regular_file(info_file)) {
		LOG(ERROR) << "Unable to load block images: Block info file " << info_file
			<< " does not exist!";
		return false;
	}

	block_width = BlockAtlas::instance().GetBlockWidth();
	block_height = BlockAtlas::instance().GetBlockHeight();;
	block_images.reserve(BlockAtlas::instance().GetCount() * 2);

	std::ifstream in(info_file.string());
	// Skip the first line
	{
		std::string first_line;
		std::getline(in, first_line);
	}

    std::set<uint32_t> all_image_uv_indices;

	int lineno = 2;
	for (std::string line; std::getline(in, line); lineno++) {
		line = util::trim(line);
		if (line.size() == 0) {
			continue;
		}

		std::vector<std::string> parts = util::split(line, ' ');
		if (parts.size() != 3) {
			LOG(ERROR) << "Invalid line in block info file '" << info_file << "'!";
			LOG(ERROR) << "Line " << lineno << ": '" << line << "'";
			return false;
		}

		std::string& block_name = parts[0];
		std::string& variant = parts[1];
		std::map<std::string, std::string> block_info = util::parseProperties(parts[2]);

		std::vector<std::string> colors = util::split(block_info["color"],':');
		std::vector<std::string> uvs = util::split(block_info["uv"],':');
		std::vector<std::string> weights = util::split(block_info["weight"],':');
		std::size_t variantCnt = colors.size();
		if (weights.size()==0) weights = std::vector<std::string>(variantCnt,"1");
		assert(uvs.size() == variantCnt && weights.size() == variantCnt && "Block info file corrupted");
		std::vector<uint32_t> image_index(variantCnt), image_uv_index(variantCnt), image_weight(variantCnt);
		uint32_t total_weight = 0;
		double_t weight_factor = 1.0;
		for (std::size_t cnt=0; cnt<variantCnt; cnt++) {
			image_index[cnt] = util::as<int>(colors[cnt]);
			image_uv_index[cnt] = util::as<int>(uvs[cnt]);
			int weight = util::as<int>(weights[cnt]);
			image_weight[cnt] = weight;
			total_weight += weight;
		}
		weight_factor = double_t(1.0) / double_t(total_weight);

		mc::BlockState block_state = mc::BlockState::parse(block_name, variant);
		BlockImage& block = *new BlockImage();;
		block.image(image_index);
		block.uv_image(image_uv_index);
		block.weight_image(image_weight, weight_factor);

        all_image_uv_indices.insert(image_uv_index.begin(), image_uv_index.end());

		block.is_biome = block_info.count("biome_type");
		if (block.is_biome) {
			block.is_masked_biome = block_info["biome_type"] == "masked";
			block.biome_color = util::as<ColorMapType>(block_info["biome_colors"]);
			if (block_info.count("biome_colormap")) {
				if (!block.biome_colormap.parse(block_info["biome_colormap"])) {
					LOG(WARNING) << "Unable to parse colormap '" << block_info["biome_colormap"] << "'.";
				}
			}
		}
		if (block_info.count("lighting_type")) {
			block.lighting_specified = true;
			block.lighting_type = util::as<LightingType>(block_info["lighting_type"]);
		}
		block.has_faulty_lighting = block_info.count("faulty_lighting");

		block.can_partial = block_info.count("partial") ? block_info["partial"] == "true" : false;

		block.shadow_edges = -1;
		if (block_info.count("shadow_edges")) {
			block.shadow_edges = util::as<int>(block_info["shadow_edges"]);
		}

		auto set_image_idx = [this](int id, BlockImage* block ) -> void {
			uint16_t id_next = id + 1;
			if (block_images.size() < id_next) {
				block_images.resize(id + 1);
			}
			block_images[id].reset(block);
		};

		block.is_waterlogged = block_info.count("inherently_waterlogged") != 0;
		if (!block.is_waterlogged) {
			if (block_info.count("is_waterloggable") != 0) {
				int water_id;
				mc::BlockState waterlogged = block_state;
				BlockImage& water_block = *new BlockImage();
				water_block = block;
				water_block.is_waterlogged = true;
				waterlogged.setProperty("waterlogged", "true");
				water_id = block_registry.getBlockID(waterlogged);
				set_image_idx(water_id, &water_block);
				block_state.setProperty("waterlogged", "false");
			}
		}

		// Save the blockimage
		uint16_t id = block_registry.getBlockID(block_state);
		set_image_idx(id, &block);

		const std::map<std::string, std::string>& properties = block_state.getProperties();
		for (std::map<std::string, std::string>::const_iterator it = properties.begin(); it != properties.end(); ++it) {
			block_registry.addKnownProperty(block_state.getName(), it->first);
		}

		//std::cout << block_name << " " << variant << std::endl;
	}
	in.close();

    for (uint32_t image_uv_index : all_image_uv_indices) {
        const RGBAImage& image = BlockAtlas::instance().GetImage(image_uv_index);
        int n = image.getWidth() * image.getHeight();

        for (int i = 0; i < n; i++) {
            auto& pixel = image.data[i];
            if (rgba_alpha(pixel) == 0) {
                if (pixel != 0) {
                    throw std::invalid_argument("uv texture contains non-zero transparent pixel!");
                }
            } else {
                uint8_t face;
                switch (rgba_blue(pixel)) {
                    case FACE_LEFT_COLOR:
                        face = FACE_LEFT_INDEX;
                        break;
                    case FACE_RIGHT_COLOR:
                        face = FACE_RIGHT_INDEX;
                        break;
                    case FACE_UP_COLOR:
                        face = FACE_UP_INDEX;
                        break;
                    default:
                        throw std::invalid_argument("uv texture contains invalid face index!");
                }
                pixel = rgba(rgba_red(pixel), rgba_green(pixel), face, rgba_alpha(pixel));
            }
        }
    }

	prepareBlockImages();
	//runBenchmark();

	return true;
}

RGBAImage RenderedBlockImages::exportBlocks() const {
	/*
	std::vector<RGBAImage> blocks;

	for (auto it = block_images.begin(); it != block_images.end(); ++it) {
		blocks.push_back(it->second.image);
	}

	if (blocks.size() == 0) {
		return RGBAImage(0, 0);
	}

	int width = 16;
	int height = std::ceil((double) blocks.size() / width);
	int block_size = blocks[0].getWidth();
	RGBAImage image(width * block_size, height * block_size);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int offset = y * width + x;
			if ((size_t) offset >= blocks.size())
				break;
			image.alphaBlit(blocks.at(offset), x * block_size, y * block_size);
		}
	}

	return image;
	*/
	return RGBAImage(1, 1);
}

const BlockImage& RenderedBlockImages::getBlockImage(uint16_t id) const {
	if (block_images.size() <= id) {
		const mc::BlockState& block_state = block_registry.getBlockState(id);

		if (!block_state.hasProperty("waterlogged")) {
			mc::BlockState test = mc::BlockState::parse(block_state.getName(), block_state.getVariantDescription());
			test.setProperty("waterlogged", "false");
			return getBlockImage(block_registry.getBlockID(test));
		}
		LOG(INFO) << "Unknown block " << block_state.getName() << " " << block_state.getVariantDescription();

		return unknown_block;
	}
	return *block_images[id];
}

void RenderedBlockImages::prepareBiomeBlockImage(RGBAImage& image, const BlockImage& block, uint32_t color) {

	if (block.is_masked_biome) {
		blockImageTint(image, *block.biome_mask, color);
	} else {
		blockImageTint(image, color);
	}
}

int RenderedBlockImages::getTextureSize() const {
	return texture_size;
}

int RenderedBlockImages::getBlockSize() const {
	return block_width;
}

int RenderedBlockImages::getBlockWidth() const {
	return block_width;
}

int RenderedBlockImages::getBlockHeight() const {
	return block_height;
}

void RenderedBlockImages::prepareBlockImages() {
	const uint16_t solid_id = block_registry.getBlockID(mc::BlockState("minecraft:unknown_block"));
	assert(block_images.size() > solid_id && block_images[solid_id] != nullptr);
	const BlockImage& solid = *block_images[solid_id];

	const uint16_t air_id = block_registry.getBlockID(mc::BlockState("minecraft:air"));
	assert(block_images.size() > air_id && block_images[air_id] != nullptr);
	const BlockImage& air = *block_images[air_id];
	const uint16_t air_image_id = air.images_idx[0];

	std::unordered_set<uint16_t> shaded_blocks;
	shaded_blocks.reserve(BlockAtlas::instance().GetCount());

	// Go through all images to clarify few flags, and
	// prepare compute the shading per direction
	for (uint16_t id = 0; id < block_images.size(); ++id) {
		if (block_images[id] == nullptr) {
			continue;
		}

		BlockImage& block = *block_images[id];
		const mc::BlockState& block_state = block_registry.getBlockState(id);

		// Check if display is empty
		block.is_empty = true;
		for (auto &&i : block.images_idx) {
			if (i != air_image_id) {
				block.is_empty = false;
				break;
			}
		}

		// Biome mask ?
		// The detection by name is not the best
		// TODO: Make a flag in block file to disable the shading
		std::string name = block_state.getName();
		if (!util::endswith(name, "_biome_mask")) {
			for (int16_t i = block.images_idx.size()-1; i >= 0 ; --i) {
				uint32_t bid = block.images_idx[i];
				uint32_t uv_bid = block.uv_images_idx[i];
				BlockAtlas::instance().ShadeBlock(bid, uv_bid, darken_left, darken_right, 1.0);
			}
		}

		block.side_mask = blockImageGetSideMask(block.uv_image(0));
		block.is_transparent = blockImageIsTransparent(block.image(0), solid.uv_image(0));

		if (block.is_biome && block.is_masked_biome) {
			std::string mask_name = name + "_biome_mask";
			uint16_t mask_id = block_registry.getBlockID(mc::BlockState::parse(mask_name, block_state.getVariantDescription()));
			assert(block_images.size() > mask_id && block_images[mask_id] != nullptr);
			block.biome_mask = &block_images[mask_id]->image(0);
		}

		if (!block.lighting_specified) {
			if (!block.is_transparent) {
				block.lighting_type = LightingType::SMOOTH;
			} else {
				if (block.is_waterlogged) {
					block.lighting_type = LightingType::SMOOTH_TOP_REMAINING_SIMPLE;
				} else {
					block.lighting_type = LightingType::SIMPLE;
				}
			}
		}

		if (block.shadow_edges == -1) {
			block.shadow_edges = !block.is_transparent;
		}
	}

	unknown_block = solid;
}

void RenderedBlockImages::runBenchmark() {
	LOG(INFO) << "Running benchmark";

	typedef std::chrono::high_resolution_clock clock_;
	typedef std::chrono::duration<double, std::ratio<1> > second_;

	uint16_t id = block_registry.getBlockID(mc::BlockState::parse("minecraft:full_water", "up=false,south=false,west=false"));
	assert(block_images.size() > id && block_images[id] != nullptr);
	const BlockImage& solid = *block_images[id];

	// uint32_t color = rgba(0x30, 0x59, 0xad, 0xff);

	CornerValues left = {1.0, 0.8, 0.5, 1.0};
	CornerValues right = {1.0, 0.6, 0.3, 0.8};
	CornerValues up = {0.5, 1.0, 0.6, 0.8};
	static const LightFnc light_fnc = {};

	std::chrono::time_point<clock_> begin = clock_::now();
	const RGBAImage& image = solid.image(0);
	RGBAImage solid_image(image.getHeight(), image.getWidth());
	solid_image.simpleBlit(image,0,0);

	for (size_t i = 0; i < 1000000; i++) {

		// 5.841s
		//blockImageTint(image, image, 0x30, 0x59, 0xad, 0xff);

		// 3.441s
		// 3.072s mit nicem rgba_multiply
		// 2.876s mit alpha check als bitmaske und vergleich > 0
		//blockImageTint(image, image, color);

		// 1.597s (wenn alpha check weg!)
		// 1.590s (sonst auch!)
		// 1.105s mit nicem rgba_multiply
		//blockImageTint(image, color);

		// 9.534s mit rgb_multiply_scalar
		// 6.345s mit rgb_multiply_scalar inline
		// 6.377s mit rgba_multiply_scalar ohne f+1
		// 6.126s doch wenn der alpha check drin ist
		blockImageMultiply(solid_image, solid.uv_image(0), left, right, up, light_fnc);
	}

	double elapsed = std::chrono::duration_cast<second_>(clock_::now() - begin).count();
	LOG(INFO) << "took " << elapsed << "s";
	exit(0);
}

}
}
