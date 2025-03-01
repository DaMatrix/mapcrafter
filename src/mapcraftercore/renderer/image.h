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

#ifndef IMAGE_H_
#define IMAGE_H_

#include "../util/other.h" //util::UninitializedTag
#include <generated/config.h> //AUTO_TARGET_CLONES

#define _USE_MATH_DEFINES
#include <cassert>
#include <cmath>
#include <math.h> // to be sure M_PI is defined

#include <algorithm> //std::copy(), std::fill()
#include <array>
#include <cstdint>
#include <memory> //std::unique_ptr
#include <utility> //std::move(), std::swap()

namespace mapcrafter {
namespace renderer {

/**
 * An 8-bit integer normalized to the range [0, 1]
 */
struct NormalizedUInt8 {
	uint8_t value;

	NormalizedUInt8() = default;

	template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
	explicit NormalizedUInt8(T value) : value(value) {
		assert(value >= 0 && value <= 255);
	}

	template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
	NormalizedUInt8& operator=(T value) {
		assert(value >= 0 && value <= 255);
		this->value = value;
		return *this;
	}

	bool operator==(const NormalizedUInt8& rhs) const { return value == rhs.value; }
	bool operator!=(const NormalizedUInt8& rhs) const { return value != rhs.value; }
	bool operator< (const NormalizedUInt8& rhs) const { return value <  rhs.value; }
	bool operator<=(const NormalizedUInt8& rhs) const { return value <= rhs.value; }
	bool operator> (const NormalizedUInt8& rhs) const { return value >  rhs.value; }
	bool operator>=(const NormalizedUInt8& rhs) const { return value >= rhs.value; }

	operator uint8_t() const { return value; }

	float toFloat() const { return static_cast<float>(value) / 255.0f; }
	double toDouble() const { return static_cast<double>(value) / 255.0; }

	template<typename T>
	static typename std::enable_if<std::is_floating_point<T>::value, NormalizedUInt8>::type fromFloatingPoint(T f) {
		assert(f >= 0 && f <= 1);
		return NormalizedUInt8(static_cast<uint8_t>(f * 255));
	}
};

inline NormalizedUInt8 multiply(const NormalizedUInt8& lhs, const NormalizedUInt8& rhs) {
	//approximately equal to:
	//  uint8_t result = ((double) lhs.value / 255.0) * ((double) rhs.value / 255.0) * 255.0;
	return NormalizedUInt8(((lhs.value + 1) * rhs.value) >> 8);
}

typedef uint32_t RGBAPixel;

inline RGBAPixel rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
	return (a << 24) | (b << 16) | (g << 8) | r;
}

inline uint8_t rgba_red(RGBAPixel value) {
	return value & 0xff;
}

inline uint8_t rgba_green(RGBAPixel value) {
	return (value & 0xff00) >> 8;
}

inline uint8_t rgba_blue(RGBAPixel value) {
	return (value & 0xff0000) >> 16;
}

inline uint8_t rgba_alpha(RGBAPixel value) {
	return (value & 0xff000000) >> 24;
}

inline RGBAPixel rgba(std::array<uint8_t, 4> value) {
	return rgba(value[0], value[1], value[2], value[3]);
}

inline std::array<uint8_t, 4> rgba_to_array(RGBAPixel value) {
	return { rgba_red(value), rgba_green(value), rgba_blue(value), rgba_alpha(value) };
}

inline RGBAPixel rgba_average(RGBAPixel v1, RGBAPixel v2) {
	RGBAPixel v1_masked = v1 & 0x00FEFEFE;
	RGBAPixel v2_masked = v2 & 0x00FEFEFE;
	RGBAPixel sum = (v1_masked + v2_masked) >> 1;
	return (v1 & 0xff000000) | sum;
}

inline RGBAPixel rgba_average_with_alpha(RGBAPixel p1, RGBAPixel p2, RGBAPixel p3, RGBAPixel p4) {
	RGBAPixel highBits = ((p1 >> 2) & 0X3F3F3F3F) + ((p2 >> 2) & 0X3F3F3F3F) + ((p3 >> 2) & 0X3F3F3F3F) + ((p4 >> 2) & 0X3F3F3F3F);
	RGBAPixel lowBits = (((p1 & 0x03030303) + (p2 & 0x03030303) + (p3 & 0x03030303) + (p4 & 0x03030303)) >> 2) & 0x03030303;
	return highBits + lowBits;
}

inline RGBAPixel rgba_multiply(RGBAPixel v1, RGBAPixel v2) {
	uint8_t r = (rgba_red(v1) * (rgba_red(v2) + 1)) >> 8;
	uint8_t g = (rgba_green(v1) * (rgba_green(v2) + 1)) >> 8;
	uint8_t b = (rgba_blue(v1) * (rgba_blue(v2) + 1)) >> 8;
	return rgba(r, g, b, rgba_alpha(v1));
}

inline RGBAPixel rgba_multiply_with_alpha(RGBAPixel v1, RGBAPixel v2) {
	uint8_t r = (rgba_red(v1) * (rgba_red(v2) + 1)) >> 8;
	uint8_t g = (rgba_green(v1) * (rgba_green(v2) + 1)) >> 8;
	uint8_t b = (rgba_blue(v1) * (rgba_blue(v2) + 1)) >> 8;
	uint8_t a = (rgba_alpha(v1) * (rgba_alpha(v2) + 1)) >> 8;
	return rgba(r, g, b, a);
}

// Make sure 255 x 255 = 255 by adding 1 before the mult
// Max color value : 255
// (255+1) * 0 / 256 = 0
// (255+1) * 1 / 256 = 1
// ...
// (255+1) * 254 / 256 = 254
// (255+1) * 255 / 256 = 255
//
// Mid color value : 128
// (128+1) * 0 / 256 = 0
// (128+1) * 1 / 256 = 0
// (128+1) * 2 / 256 = 1
// ...
// (128+1) * 128 / 256 = 64
// ...
// (128+1) * 254 / 256 = 127
// (128+1) * 255 / 256 = 128
inline RGBAPixel rgba_multiply_scalar(RGBAPixel value, NormalizedUInt8 factor) {
	uint32_t g = (((value & 0xff00) * (factor + 1)) >> 8) & 0xff00;
	uint32_t br = (((value & 0xff00ff) * (factor + 1)) >> 8) & 0xff00ff;
	uint32_t a = value & 0xff000000;
	return a | g | br;
}

inline RGBAPixel rgba_multiply_scalar_with_alpha(RGBAPixel value, NormalizedUInt8 factor) {
	//we shift the green+alpha to the right first so that both can be processed in parallel
	uint32_t ga_orig = (value >> 8) & 0xff00ff;
	uint32_t br_orig = value & 0xff00ff;

	uint32_t ga = ((ga_orig * (factor + 1)) >> 8) & 0xff00ff;
	uint32_t br = ((br_orig * (factor + 1)) >> 8) & 0xff00ff;
	return (ga << 8) | br;
}

inline RGBAPixel rgba_add_clamp(RGBAPixel value, std::array<int, 4> add) {
	auto value_array = rgba_to_array(value);
	for (int i = 0; i < 4; i++)
		value_array[i] = std::min(std::max(value_array[i] + add[i], 0), 255);
	return rgba(value_array);
}

inline RGBAPixel rgba_add_clamp(RGBAPixel value, int r, int g, int b, int a = 0) {
	return rgba_add_clamp(value, { r, g, b, a });
}

RGBAPixel rgba_multiply(RGBAPixel value, double r, double g, double b, double a = 1);
int rgba_distance2(RGBAPixel value1, RGBAPixel value2);

inline RGBAPixel rgba_alphablend(RGBAPixel dst, RGBAPixel src) {
	//this code is simple enough to be eligible for auto-vectorization by both clang and gcc

	//if source is fully transparent, leave the destination pixel unchanged.
	//  this is technically not necessary, removing this check only affects the rgb values of fully transparent pixels.
	if (rgba_alpha(src) == 0) return dst;

	//if the destination is fully transparent, just set it to the source pixel
	if (rgba_alpha(dst) == 0) return src;

	//equivalent code assuming floating-point colors normalized on [0,1]:
	//  vec3 result_rgba = dst.rgb * (1.0 - src.a) + src.rgb * src.a;
	//  float result_alpha = 1.0 - (1.0 - src.a) * (1.0 - dst.a)
	auto dst_alpha = NormalizedUInt8(rgba_alpha(dst));
	auto one_minus_dst_alpha = NormalizedUInt8(255 - dst_alpha);
	auto src_alpha = NormalizedUInt8(rgba_alpha(src));
	auto one_minus_src_alpha = NormalizedUInt8(255 - src_alpha);

	RGBAPixel dst_rgba_times_one_minus_src_alpha
			= rgba_multiply_scalar_with_alpha(dst, one_minus_src_alpha);
	RGBAPixel src_rgba_times_src_alpha
			= rgba_multiply_scalar_with_alpha(src, src_alpha);
	RGBAPixel result_rgb = (dst_rgba_times_one_minus_src_alpha + src_rgba_times_src_alpha) & 0x00FFFFFF;

	auto one_minus_src_alpha_time_one_minus_dst_alpha
			= multiply(one_minus_src_alpha, one_minus_dst_alpha);
	auto result_alpha = NormalizedUInt8(255 - one_minus_src_alpha_time_one_minus_dst_alpha);

	return (result_alpha << 24) | result_rgb;
}

template <typename Pixel>
class Image {
public:
	Image() noexcept
		: width(0), height(0), data(nullptr) {}
	Image(size_t width, size_t height)
		: width(width), height(height), data(new Pixel[width * height]()) {}
	Image(size_t width, size_t height, util::UninitializedTag)
		: width(width), height(height), data(new Pixel[width * height]) {}

    Image(const Image& src) : Image(src.width, src.height, util::UninitializedTag{}) {
        std::copy(src.begin(), src.end(), begin());
    }

	Image(Image&& src) noexcept
			: width(src.width), height(src.height), data(std::move(src.data)) {
		src.width = 0;
		src.height = 0;
	}

    Image& operator=(const Image& src) {
        if (&src != this) {
			setSize(src.width, src.height, util::UninitializedTag{});
            std::copy(src.begin(), src.end(), begin());
        }
        return *this;
    }

    Image& operator=(Image&& src) noexcept {
        if (&src != this) {
			width = src.width;
			height = src.height;
			src.width = 0;
			src.height = 0;

			data.reset(src.data.release());
        }
        return *this;
    }

	size_t getWidth() const { return width; }
	size_t getHeight() const { return height; }

	size_t getPixelCount() const { return width * height; }
	bool isSameSize(const Image& other) const { return width == other.width && height == other.height; }

	Pixel getPixel(size_t x, size_t y) const { return x >= width || y >= height ? 0 : pixel(x, y); }
	void setPixel(size_t x, size_t y, Pixel pixel) {
		if (x < width && y < height)
			this->pixel(x, y) = pixel;
	}

	Pixel& pixel(size_t x, size_t y) { assert(x < width && y < height); return data[y * width + x]; }
	const Pixel& pixel(size_t x, size_t y) const { assert(x < width && y < height); return data[y * width + x]; }

	/**
	 * Sets all pixels in this image to the default value.
	 */
	void clear() noexcept {
		std::fill(begin(), end(), Pixel());
	}

	/**
	 * Sets this image to the given size. All pixels will be initialized to the default value.
	 * @param new_width the new image width
	 * @param new_height the new image height
	 */
	//TODO: this is kinda redundant, everything that uses this function seems to assume the pixels are uninitialized
	void setSize(size_t new_width, size_t new_height) {
		setSize(new_width, new_height, util::UninitializedTag{});
		clear();
	}

	/**
	 * Sets this image to the given size. All pixels will be initialized with undefined contents.
	 * @param new_width the new image width
	 * @param new_height the new image height
	 */
	void setSize(size_t new_width, size_t new_height, util::UninitializedTag) {
		if (width * height != new_width * new_height) {
			data.reset(new Pixel[new_width * new_height]);
		}
		width = new_width;
		height = new_height;
	}

//protected:
	size_t width;
	size_t height;

	std::unique_ptr<Pixel[]> data;

public:
    Pixel* begin() { return data.get(); }
    Pixel* end() { return data.get() + getPixelCount(); }

    const Pixel* begin() const { return data.get(); }
    const Pixel* end() const { return data.get() + getPixelCount(); }

    Pixel* rowbegin(size_t row) { assert(row < height); return begin() + row * width; }
    Pixel* rowend(size_t row) { assert(row < height); return begin() + (row + 1) * width; }

    const Pixel* rowbegin(size_t row) const { assert(row < height); return begin() + row * width; }
    const Pixel* rowend(size_t row) const { assert(row < height); return begin() + (row + 1) * width; }

protected:
	bool containsRect(size_t x, size_t y, size_t w, size_t h) const;
};

struct WritePngOptions {
	int compression_level = -1;
};

// TODO better documentation...
class RGBAImage : public Image<RGBAPixel> {
public:
	RGBAImage() : Image<RGBAPixel>() {}
	RGBAImage(size_t width, size_t height) : Image<RGBAPixel>(width, height) {}
	RGBAImage(size_t width, size_t height, util::UninitializedTag u) : Image<RGBAPixel>(width, height, u) {}

	/**
	 * Blits one image to another one. Just copies the pixels over without any processing.
	 */
	void simpleBlit(const RGBAImage& image, size_t x, size_t y);

	/**
	 * Blits one image to another one. Just copies the pixels over, but skips completely
	 * transparent pixels (alpha(pixel) == 0).
	 */
	AUTO_TARGET_CLONES void simpleAlphaBlit(const RGBAImage& image, size_t x, size_t y);

	/**
	 * Blits one image to another one. Also Alphablends transparent pixels of the source
	 * image with the pixels of the destination image.
	 */
	AUTO_TARGET_CLONES void alphaBlit(const RGBAImage& image, int x, int y);

	RGBAImage clip(size_t x, size_t y, size_t w, size_t h) const;

	AUTO_TARGET_CLONES RGBAImage resizeHalf() const;

	/**
	 * Sets the RGB values of every fully transparent pixel to zero.
	 */
	AUTO_TARGET_CLONES void simplifyTransparentPixels() noexcept;

	//these functions may throw an std::exception or return false to indicate failure
	void readPNG(const std::string& filename);
	void writePNG(const std::string& filename, const WritePngOptions& options = {}) const;
	bool writeIndexedPNG(const std::string& filename, const WritePngOptions& options = {}, int palette_bits = 8, bool dithered = true) const;

	bool readJPEG(const std::string& filename);
	bool writeJPEG(const std::string& filename, int quality,
			RGBAPixel background = rgba(255, 255, 255, 255)) const;
};

}
}

#endif /* IMAGE_H_ */
