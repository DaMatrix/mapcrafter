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

#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h> // to be sure M_PI is defined

#include <png.h>
#include <cassert>
#include <algorithm> // std::copy()
#include <memory> // std::allocator, std::uninitialized_*()
#include <cstdint>
#include <string>
#include <type_traits> // std::is_trivially_copyable

#include "../util/other.h" // mapcrafter::util::UninitializedTag
#include <generated/config.h> // AUTO_TARGET_CLONES

namespace mapcrafter {
namespace renderer {

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

inline RGBAPixel rgba_average(RGBAPixel v1, RGBAPixel v2) {
	RGBAPixel v1_masked = v1 & 0x00FEFEFE;
	RGBAPixel v2_masked = v2 & 0x00FEFEFE;
	RGBAPixel sum = (v1_masked + v2_masked) >> 1;
	return (v1 & 0xff000000) | sum;
}

inline RGBAPixel rgba_multiply(RGBAPixel v1, RGBAPixel v2) {
	uint32_t r = ((((v1 & 0xff) + 0x01) * (v2 & 0xff)) >> 8) & 0xff;
	uint32_t g = ((((v1 & 0xff00) + 0x0100) * (v2 & 0xff00)) >> 16) & 0xff00;
	uint32_t b = ((((uint64_t) (v1 & 0xff0000) + 0x010000) * (v2 & 0xff0000)) >> 24) & 0xff0000;
	return (v1 & 0xff000000) | r | g | b;
}

inline RGBAPixel rgba_multiply_with_alpha(RGBAPixel v1, RGBAPixel v2) {
	uint32_t r = (((v1 & 0xff) + 0x01) * (v2 & 0xff) >> 8) & 0xff;
	uint32_t g = (((v1 & 0xff00) + 0x0100)  * (v2 & 0xff00) >> 16) & 0xff00;
	uint32_t b = ((((uint64_t) (v1 & 0xff0000) + 0x010000) * (v2 & 0xff0000)) >> 24) & 0xff0000;
	uint32_t a = ((((uint64_t) (v1 & 0xff000000) + 0x01000000) * (v2 & 0xff000000)) >> 32) & 0xff000000;
	return a | r | g | b;
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
inline RGBAPixel rgba_multiply_scalar(RGBAPixel value, uint32_t factor) {
	uint32_t g = ((((value & 0xff00) + 0x0100) * factor) >> 8) & 0xff00;
	uint32_t br = ((((value & 0xff00ff) + 0x010001) * factor) >> 8) & 0xff00ff;
	uint32_t a = value & 0xff000000;
	return a | g | br;
}

RGBAPixel rgba_add_clamp(RGBAPixel value, int r, int g, int b, int a = 0);
RGBAPixel rgba_add_clamp(RGBAPixel value, const std::tuple<int, int, int>& values);
RGBAPixel rgba_multiply(RGBAPixel value, double r, double g, double b, double a = 1);
int rgba_distance2(RGBAPixel value1, RGBAPixel value2);

void blend(RGBAPixel& dest, const RGBAPixel& source);

void pngReadData(png_structp pngPtr, png_bytep data, png_size_t length);
void pngWriteData(png_structp pngPtr, png_bytep data, png_size_t length);

template <typename Pixel>
class Image : protected std::allocator<Pixel> {
protected:
	using allocator_traits = std::allocator_traits<std::allocator<Pixel>>;

	static_assert(std::is_trivially_copyable<Pixel>::value, "Image expects a trivially copyable pixel type");
	static_assert(allocator_traits::is_always_equal::value, "expected std::allocator to be always equal");

	Pixel* allocateStorage(size_t pixelCount) {
		return pixelCount != 0 ? allocator_traits::allocate(*this, pixelCount) : nullptr;
	}

	void deallocateStorage(Pixel* p, size_t pixelCount) noexcept {
		if (p) {
			allocator_traits::deallocate(*this, p, pixelCount);
		}
	}

	size_t width;
	size_t height;
	Pixel* ptr;

public:
	Image() noexcept : width(0), height(0), ptr(nullptr) {}

	Image(size_t width, size_t height, util::UninitializedTag) :
			width(width), height(height), ptr(allocateStorage(width * height)) {
		std::uninitialized_default_construct(begin(), end());
	}

	Image(size_t width, size_t height) :
			width(width), height(height), ptr(allocateStorage(width * height)) {
		std::uninitialized_fill(begin(), end(), Pixel{});
	}

	Image(const Image& src) :
			width(src.width), height(src.height), ptr(allocateStorage(width * height)) {
		std::uninitialized_copy(src.begin(), src.end(), begin());
	}

	Image(Image&& src) noexcept : width(src.width), height(src.height), ptr(src.ptr) {
		src.width = 0;
		src.height = 0;
		src.ptr = nullptr;
	}

	~Image() {
		deallocateStorage(ptr, getPixelCount());
	}

	Image& operator=(const Image& src) {
		if (this != &src) {
			//copy the source image dimensions, reallocating the storage if necessary
			setSize(src.width, src.height, util::UninitializedTag{});

			//actually copy the pixels
			std::copy(src.begin(), src.end(), begin());
		}
		return *this;
	}

	Image& operator=(Image&& src) noexcept {
		if (this != &src) {
			deallocateStorage(ptr, getPixelCount());

			width = src.width;
			height = src.height;
			ptr = src.ptr;
			src.width = 0;
			src.height = 0;
			src.ptr = nullptr;
		}
		return *this;
	}

	size_t getWidth() const noexcept { return width; }
	size_t getHeight() const noexcept { return height; }

	size_t getPixelCount() const noexcept { return width * height; }
	bool isSameSize(const Image& other) const noexcept { return width == other.width && height == other.height; }

	Pixel getPixel(size_t x, size_t y) const {
		if (x < width && y < height) {
			return ptr[y * width + height];
		} else {
			return Pixel();
		}
	}

	void setPixel(size_t x, size_t y, Pixel pixel) {
		if (x < width && y < height)
			ptr[y * width + height] = pixel;
	}

	const Pixel& pixel(size_t x, size_t y) const {
		assert(x >= 0 && x < width);
		assert(y >= 0 && y < height);
		return ptr[y * width + height];
	}

	Pixel& pixel(size_t x, size_t y) {
		assert(x >= 0 && x < width);
		assert(y >= 0 && y < height);
		return ptr[y * width + height];
	}

	/**
	 * Sets all pixels in this image to the default value.
	 */
	void clear() noexcept {
		std::fill(begin(), end(), Pixel{}); //generally compiles into memset()
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
		if (getPixelCount() != new_width * new_height) { //capacity doesn't match, reallocate the storage
			Pixel* newPtr = allocateStorage(new_width * new_height);
			std::uninitialized_default_construct(newPtr, newPtr + new_width * new_height);

			deallocateStorage(ptr, getPixelCount());
			ptr = newPtr;
		}
		width = new_width;
		height = new_height;
	}

	Pixel* data() noexcept { return ptr; }
	const Pixel* data() const noexcept { return ptr; }

	Pixel* begin() noexcept { return ptr; }
	const Pixel* begin() const noexcept { return ptr; }

	Pixel* end() noexcept { return ptr + getPixelCount(); }
	const Pixel* end() const noexcept { return ptr + getPixelCount(); }

    Pixel* rowbegin(size_t row) { assert(row < height); return begin() + row * width; }
    Pixel* rowend(size_t row) { assert(row < height); return begin() + (row + 1) * width; }

    const Pixel* rowbegin(size_t row) const { assert(row < height); return begin() + row * width; }
    const Pixel* rowend(size_t row) const { assert(row < height); return begin() + (row + 1) * width; }

protected:
	bool containsRect(size_t x, size_t y, size_t w, size_t h) const noexcept;
};

// TODO better documentation...
class RGBAImage : public Image<RGBAPixel> {
public:
	RGBAImage() noexcept = default;

	RGBAImage(size_t width, size_t height, util::UninitializedTag)
			: Image<RGBAPixel>(width, height, util::UninitializedTag{}) {}

	RGBAImage(size_t width, size_t height)
			: Image<RGBAPixel>(width, height) {}

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
	AUTO_TARGET_CLONES void alphaBlit(const RGBAImage& image, size_t x, size_t y);

	RGBAImage clip(size_t x, size_t y, size_t w, size_t h) const;

	AUTO_TARGET_CLONES RGBAImage resizeHalf() const;

	bool readPNG(const std::string& filename);
	bool writePNG(const std::string& filename) const;
	bool writeIndexedPNG(const std::string& filename, int palette_bits = 8, bool dithered = true) const;

	bool readJPEG(const std::string& filename);
	bool writeJPEG(const std::string& filename, int quality,
			RGBAPixel background = rgba(255, 255, 255, 255)) const;
};

}
}

#endif /* IMAGE_H_ */
