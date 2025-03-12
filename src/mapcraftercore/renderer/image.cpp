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

#if defined(_WIN32) || defined(_WIN64)
#define NOMINMAX
#endif

#include "image.h"

#include "image/dithering.h"
#include "image/quantization.h"
#include "../util.h"

#include <jpeglib.h>
#include <png.h>

#if HAVE_SPNG_LIBRARY
#include <spng.h>
#endif

#include <boost/endian.hpp>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace mapcrafter {
namespace renderer {

RGBAPixel rgba_multiply(RGBAPixel value, double r, double g, double b, double a) {
	uint8_t red = rgba_red(value);
	uint8_t green = rgba_green(value);
	uint8_t blue = rgba_blue(value);
	uint8_t alpha = rgba_alpha(value);
	return rgba(red * r, green * g, blue * b, alpha * a);
}

int rgba_distance2(RGBAPixel value1, RGBAPixel value2) {
	int16_t delta_red = rgba_red(value1) - rgba_red(value2);
	int16_t delta_green = rgba_green(value1) - rgba_green(value2);
	int16_t delta_blue = rgba_blue(value1) - rgba_blue(value2);
	int16_t delta_alpha = rgba_alpha(value1) - rgba_alpha(value2);
	return delta_red * delta_red +
	       delta_green * delta_green +
	       delta_blue * delta_blue +
	       delta_alpha * delta_alpha;
}

# ifndef UINT64_C
#  if __WORDSIZE == 64
#   define UINT64_C(c)	c ## UL
#  else
#   define UINT64_C(c)	c ## ULL
#  endif
# endif

template<typename Pixel>
bool Image<Pixel>::containsRect(size_t x, size_t y, size_t w, size_t h) const {
	return x <= this->width && x + w <= this->width &&
		   y <= this->height && y + h <= this->height;
}

void RGBAImage::simpleBlit(const RGBAImage& image, size_t x, size_t y) {
	assert(containsRect(x, y, image.width, image.height));

	size_t src_width = image.width;
	size_t src_height = image.height;
	const RGBAPixel* src_it = image.begin();
	size_t dst_width = width;
	RGBAPixel* dst_it = begin() + y * width + x;
	for (size_t row = 0; row < src_height; row++, src_it += src_width, dst_it += dst_width) {
		std::copy(src_it, src_it + src_width, dst_it);
	}
}

AUTO_TARGET_CLONES void RGBAImage::simpleAlphaBlit(const RGBAImage& image, size_t x, size_t y) {
	assert(containsRect(x, y, image.width, image.height));

	size_t src_width = image.width;
	size_t src_height = image.height;
	const RGBAPixel* src_it = image.begin();
	size_t dst_width = width;
	RGBAPixel* dst_it = begin() + y * width + x;
	for (size_t row = 0; row < src_height; row++, src_it += src_width, dst_it += dst_width) {
		std::transform(
				src_it, src_it + src_width, dst_it, dst_it,
				[](RGBAPixel src_pixel, RGBAPixel dst_pixel) -> RGBAPixel {
					return rgba_alpha(src_pixel) != 0 ? src_pixel : dst_pixel;
				});
	}
}

AUTO_TARGET_CLONES void RGBAImage::alphaBlit(const RGBAImage& image, int x, int y) {
	/*int sy = std::max(0, -y);
	for (; sy < image.height && sy + y < height; sy++) {
		int sx = std::max(0, -x);
		for (; sx < image.width && sx + x < width; sx++) {
			data[(sy + y) * width + (sx + x)] = rgba_alphablend(data[(sy + y) * width + (sx + x)], image.data[sy * image.width + sx]);
		}
	}*/

	//annoyingly, this function gets called with coordinates which exceed the image bounds, so we need some extra logic
	int sx = std::max(0, -x);
	int sy = std::max(0, -y);
	int nx = std::min(static_cast<int>(image.width) - sx, static_cast<int>(width) - (sx + x));
	int ny = std::min(static_cast<int>(image.height) - sy, static_cast<int>(height) - (sy + y));
	if (nx <= 0 || ny <= 0) {
		return;
	}

	for (int iy = 0; iy < ny; iy++) {
		const RGBAPixel* src_begin = &image.data[(sy + iy) * image.width + sx];
		RGBAPixel* dst_begin = &data[(sy + iy + y) * width + (sx + x)];
		std::transform(dst_begin, dst_begin + nx, src_begin, dst_begin, rgba_alphablend);
	}
}

RGBAImage RGBAImage::clip(size_t x, size_t y, size_t w, size_t h) const {
	assert(containsRect(x, y, w, h));

	RGBAImage image(w, h, util::UninitializedTag{});

	size_t src_width = width;
	const RGBAPixel* src_it = begin() + y * src_width + x;
	RGBAPixel* dst_it = image.begin();

	for (size_t row = 0; row < h; row++, src_it += src_width, dst_it += w) {
		std::copy(src_it, src_it + w, dst_it);
	}
	return image;
}

AUTO_TARGET_CLONES RGBAImage RGBAImage::resizeHalf() const {
	size_t src_width = this->width;
	size_t src_height = this->height;
	assert(src_width % 2 == 0 && src_height % 2 == 0 && "image size must be divisible by two!");

	size_t dst_width = src_width / 2;
	size_t dst_height = src_height / 2;
	RGBAImage dst(dst_width, dst_height, util::UninitializedTag{});

	/*for (size_t x = 0; x < src_width - 1; x += 2) {
		for (size_t y = 0; y < src_height - 1; y += 2) {
			RGBAPixel p1 = this->pixel(x, y);
			RGBAPixel p2 = this->pixel(x + 1, y);
			RGBAPixel p3 = this->pixel(x, y + 1);
			RGBAPixel p4 = this->pixel(x + 1, y + 1);
			RGBAPixel highBits = ((p1 >> 2) & 0x3f3f3f3f) + ((p2 >> 2) & 0x3f3f3f3f) + ((p3 >> 2) & 0x3f3f3f3f) + ((p4 >> 2) & 0x3f3f3f3f);
			RGBAPixel lowBits = (((p1 & 0x03030303) + (p2 & 0x03030303) + (p3 & 0x03030303) + (p4 & 0x03030303)) >> 2) & 0x03030303;
			dst.pixel(x >> 1, y >> 1) = highBits + lowBits;
		}
	}*/

	const RGBAPixel* src_it = begin();
	RGBAPixel* dst_it = dst.begin();
	for (size_t row = 0; row < dst_height; row++) {
		// For each row: iterate along two rows in the source image at once, reading two pixels from each (for a total
		// of four pixels at a time), averaging them out and writing a single pixel into the destination image.
		// This loop is simple enough to be autovectorized by both GCC and clang.

		const RGBAPixel* src_it0 = src_it;
		const RGBAPixel* src_it1 = src_it + src_width;
		for (size_t col = 0; col < dst_width; col++) {
			*dst_it = rgba_average_with_alpha(src_it0[0], src_it0[1], src_it1[0], src_it1[1]);

			src_it0 += 2;
			src_it1 += 2;
			dst_it++;
		}

		src_it += src_width * 2;
	}

	return dst;
}

AUTO_TARGET_CLONES void RGBAImage::simplifyTransparentPixels() noexcept {
	std::transform(
			begin(), end(), begin(),
			[](RGBAPixel pixel) -> RGBAPixel {
				return rgba_alpha(pixel) != 0 ? pixel : rgba(0, 0, 0, 0);
			});
}

namespace {
struct ImageIndexResult {
	std::vector<RGBAPixel> palette;
	std::vector<uint8_t> color_table_bytes;
	size_t height;
	size_t row_width_bytes;
	uint32_t bit_depth;

	bool palette_has_transparency;

	std::vector<uint8_t*> getRowPointers() {
		std::vector<uint8_t*> result(this->height);

		for (size_t i = 0; i < this->height; i++) {
			result[i] = &this->color_table_bytes[i * this->row_width_bytes];
		}

		return result;
	}
};
}

static std::unique_ptr<ImageIndexResult> indexPNGImage(const RGBAImage& img, const WritePngOptions& options) {
	std::unique_ptr<ImageIndexResult> result(new ImageIndexResult);

	size_t max_colors = size_t(1) << static_cast<size_t>(options.palette_bits);
	result->palette = octreeColorQuantize(img, max_colors);

	result->palette_has_transparency = std::any_of(
			result->palette.begin(), result->palette.end(),
			[](RGBAPixel pixel) {
				return rgba_alpha(pixel) != 255;
			});

	/*static void setRowPixel(png_byte* line, int bit_depth, int x, uint8_t index) {
		if (bit_depth == 8) {
			line[x] = index;
		} else if (bit_depth == 4) {
			index &= 0xf;
			if ((x % 2) == 0)
				line[x/2] = (line[x/2] & 0x0f) | (index << 4);
			else
				line[x/2] = (line[x/2] & 0xf0) | index;
		} else if (bit_depth == 2) {
			index &= 0x3;
			int mod = 3 - (x % 4);
			line[x/4] = (line[x/4] & ~(0x3 << mod*2)) | (index << mod*2);
		} else if (bit_depth == 1) {
			if (index)
				line[x/8] |= (1 << (7 - (x % 8)));
			else
				line[x/8] &= ~(1 << (7 - (x % 8)));
		}
	}
...
	std::vector<int> data_dithered;
	if (dithered) {
		RGBAImage copy = *this;
		imageDither(copy, p, data_dithered);
	}

	png_bytep* rows = (png_bytep*) png_malloc(png, height * sizeof(png_bytep));
	for (size_t y = 0; y < height; y++) {
		rows[y] = (png_byte*) png_calloc(png, width * sizeof(png_byte));
		for (size_t x = 0; x < width; x++) {
			if (dithered) {
				setRowPixel(rows[y], palette_bits, x, data_dithered[y * width + x]);
			} else {
				setRowPixel(rows[y], palette_bits, x, p.getNearestColor(pixel(x, y)));
			}
		}
	}*/

	OctreePalette p(result->palette);

	if (options.palette_bits != WritePngOptions::PaletteBits::PALETTE_BITS_8)
		throw std::invalid_argument("only WritePngOptions::PaletteBits::PALETTE_BITS_8 is supported!");

	result->height = img.getHeight();
	result->row_width_bytes = img.getWidth();
	result->bit_depth = 8;

	result->color_table_bytes.assign(img.getPixelCount(), 0);
	if (options.dithered) {
		//dither the image, then copy the dithered pixels into the output color table
		RGBAImage copy = img;
		std::vector<int> data_dithered = imageDither(copy, p);

		std::copy(data_dithered.begin(), data_dithered.end(), result->color_table_bytes.begin());
	} else {
		//simply map each input pixel to the nearest color in the palette
		std::transform(
				img.begin(), img.end(), result->color_table_bytes.begin(),
				[&p](const RGBAPixel& color) {
					return p.getNearestColor(color);
				});
	}

	return result;
}

#if HAVE_SPNG_LIBRARY
namespace {
	struct spng_ctx_wrapper {
		spng_ctx *ctx;

		explicit spng_ctx_wrapper(int flags) : ctx(spng_ctx_new(flags)) {
		}

		spng_ctx_wrapper(const spng_ctx_wrapper&) = delete;

		~spng_ctx_wrapper() { spng_ctx_free(ctx); }
		operator spng_ctx *() { return ctx; }
	};

	//free a unique_ptr using std::free() instead of operator delete
	struct free_deleter {
		void operator()(void *ptr) const {
			std::free(ptr);
		}
	};

	RGBAImage readPNG_spng(const fs::path& filename) {
		auto fail = [](int err) -> RGBAImage {
			throw std::runtime_error(std::string("failed to decode png image: ") + spng_strerror(err));
		};

		std::vector<uint8_t> file = util::readEntireFileToVector(filename);

		//prepare the context
		spng_ctx_wrapper ctx(0);
		spng_set_png_buffer(ctx, file.data(), file.size());

		//determine the image size
		spng_ihdr ihdr{};
		if (int ret = spng_get_ihdr(ctx, &ihdr))
			return fail(ret);

		size_t buf_size;
		if (int ret = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &buf_size))
			return fail(ret);

		//resize the actual image buffer
		RGBAImage result(ihdr.width, ihdr.height, util::UninitializedTag{});
		if (buf_size != result.getPixelCount() * sizeof(RGBAPixel))
			return fail(SPNG_EBUFSIZ);

		//actually decode the image
		if (int ret = spng_decode_image(ctx, result.begin(), buf_size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS))
			return fail(ret);

		return result;
	}

	void writePNG_spng(const RGBAImage& img, const fs::path& filename, const WritePngOptions& options) {
		auto fail = [](int err) {
			throw std::runtime_error(std::string("failed to encode png image: ") + spng_strerror(err));
		};

		//index the image if requested
		std::unique_ptr<ImageIndexResult> index_result;
		if (options.indexed) {
			index_result = indexPNGImage(img, options);
		}

		//prepare the context
		spng_ctx_wrapper ctx(SPNG_CTX_ENCODER);
		spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);

		//apply write options
		if (options.compression_level >= 0) {
			spng_set_option(ctx, SPNG_IMG_COMPRESSION_LEVEL, options.compression_level);
		}

		//prepare the image header
		spng_ihdr ihdr = {};
		ihdr.width = img.width;
		ihdr.height = img.height;
		if (options.indexed) {
			ihdr.color_type = SPNG_COLOR_TYPE_INDEXED;
			ihdr.bit_depth = index_result->bit_depth;
		} else {
			ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
			ihdr.bit_depth = 8;
		}
		spng_set_ihdr(ctx, &ihdr);

		//if the image is indexed, set the palette
		const void* img_data;
		size_t img_data_len;
		spng_format img_data_fmt;
		if (options.indexed) {
			spng_plte plte = {};
			plte.n_entries = index_result->palette.size();
			std::transform(
					index_result->palette.begin(), index_result->palette.end(), plte.entries,
					[](RGBAPixel pixel) -> spng_plte_entry {
						spng_plte_entry result = {};
						result.red = rgba_red(pixel);
						result.green = rgba_green(pixel);
						result.blue = rgba_blue(pixel);
						return result;
					});

			//this copies the colors into the context, so we don't need to keep the plte/trns around after this
			if (int ret = spng_set_plte(ctx, &plte))
				return fail(ret);

			//if the palette contains any transparent pixels, add the transparency info
			if (index_result->palette_has_transparency) {
				spng_trns trns = {};
				trns.n_type3_entries = index_result->palette.size();
				std::transform(
						index_result->palette.begin(), index_result->palette.end(), trns.type3_alpha,
						rgba_alpha);

				if (int ret = spng_set_trns(ctx, &trns))
					return fail(ret);
			}

			img_data = index_result->color_table_bytes.data();
			img_data_len = index_result->color_table_bytes.size();
			img_data_fmt = SPNG_FMT_RAW;
		} else {
			img_data = img.begin();
			img_data_len = img.getPixelCount() * sizeof(RGBAPixel);
			img_data_fmt = SPNG_FMT_PNG;
		}

		//encode the image
		if (int ret = spng_encode_image(ctx, img_data, img_data_len, img_data_fmt, SPNG_ENCODE_FINALIZE))
			return fail(ret);

		//get a pointer to the result buffer
		size_t png_size;
		int err;
		std::unique_ptr<void, free_deleter> png_buf(spng_get_png_buffer(ctx, &png_size, &err));
		if (png_buf == nullptr)
			return fail(err);

		//save the result to a file
		util::writeEntireFile(filename, png_buf.get(), png_size);
	}
}
#endif //HAVE_SPNG_LIBRARY

namespace {
	void pngReadData(png_structp png, png_bytep data, png_size_t length) {
		auto* stream = static_cast<std::istream*>(png_get_io_ptr(png));
		stream->read((char*) data, length);
	}

	void pngWriteData(png_structp png, png_bytep data, png_size_t length) {
		auto* buf = static_cast<std::string*>(png_get_io_ptr(png));
		buf->append((char*) data, length);
	}

	void pngFlushData(png_structp png) {
		//no-op
	}

	struct PNGErrorState {
		const char* msg_prefix;
		const fs::path* filename;
	};

	void pngHandleError(png_structp png, png_const_charp msg) {
		auto* state = static_cast<PNGErrorState*>(png_get_error_ptr(png));
		throw std::runtime_error(std::string(state->msg_prefix) + ": \"" + state->filename->native() + "\": " + msg);
	}

	void pngHandleWarning(png_structp png, png_const_charp msg) {
		auto* state = static_cast<PNGErrorState*>(png_get_error_ptr(png));
		LOG(WARNING) << state->msg_prefix << ": \"" << *state->filename << "\": " << msg;
	}

	struct PNGReadCtx {
		png_structp png;
		png_infop info;

		explicit PNGReadCtx(PNGErrorState* error_state) {
			png = png_create_read_struct(PNG_LIBPNG_VER_STRING, error_state, &pngHandleError, &pngHandleWarning);
			if (!png) throw std::bad_alloc();

			info = png_create_info_struct(png);
			if (!info) {
				png_destroy_read_struct(&png, nullptr, nullptr);
				throw std::bad_alloc();
			}
		}

		~PNGReadCtx() {
			png_destroy_read_struct(&png, &info, nullptr);
		}
	};

	struct PNGWriteCtx {
		png_structp png;
		png_infop info;

		explicit PNGWriteCtx(PNGErrorState* error_state) {
			png = png_create_write_struct(PNG_LIBPNG_VER_STRING, error_state, &pngHandleError, &pngHandleWarning);
			if (!png) throw std::bad_alloc();

			info = png_create_info_struct(png);
			if (!info) {
				png_destroy_write_struct(&png, nullptr);
				throw std::bad_alloc();
			}
		}

		~PNGWriteCtx() {
			png_destroy_write_struct(&png, &info);
		}
	};

	RGBAImage readPNG_libpng(const fs::path& filename) {
		PNGErrorState error_state = {
				.msg_prefix = "failed to decode png image",
				.filename = &filename,
		};

		PNGReadCtx ctx(&error_state);
		auto& png = ctx.png;
		auto& info = ctx.info;

		std::ifstream file = util::openBinaryFileForRead(filename);

		//uint8_t png_signature[8];
		//file.read((char*) &png_signature, 8);
		//if (png_sig_cmp(png_signature, 0, 8) != 0)
		//	return fail();

		png_set_read_fn(png, &file, &pngReadData);
		//png_set_sig_bytes(png, 8);

		png_read_info(png, info);
		int color = png_get_color_type(png, info);
		int bit_depth = png_get_bit_depth(png, info);

		// strip down images of 16 bits per channel to 8 bits per channel
		if (bit_depth == 16)
			png_set_strip_16(png);

		// convert gray images to rgb(a)
		if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(png);
		// make sure they are also using 8 bit per channel
		if (color == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
			png_set_expand_gray_1_2_4_to_8(png);

		// convert indexed images to rgb(a)
		if (color == PNG_COLOR_TYPE_PALETTE)
			png_set_palette_to_rgb(png);

		// add alpha channel if not existing
		if ((color & PNG_COLOR_MASK_ALPHA) == 0)
			png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);

		// allocate the result image
		RGBAImage result(png_get_image_width(png, info), png_get_image_height(png, info), util::UninitializedTag{});

		// set up the row pointers
		std::vector<png_bytep> rows(result.getHeight());
		for (size_t row = 0; row < result.getHeight(); row++) {
			rows[row] = reinterpret_cast<png_bytep>(result.rowbegin(row));
		}

		png_set_interlace_handling(png);
		png_read_update_info(png, info);

		if (boost::endian::order::native == boost::endian::order::big) {
			png_set_bgr(png);
			png_set_swap_alpha(png);
		}
		png_read_image(png, rows.data());
		png_read_end(png, nullptr);

		return result;
	}

	void writePNG_libpng(const RGBAImage& img, const fs::path& filename, const WritePngOptions& options) {
		std::string file_data;
		file_data.reserve(img.getPixelCount() * sizeof(RGBAPixel) * 2); //this should be more than enough space

		PNGErrorState error_state = {
				.msg_prefix = "failed to encode png image",
				.filename = &filename,
		};

		//index the image if requested
		std::unique_ptr<ImageIndexResult> index_result;
		if (options.indexed) {
			index_result = indexPNGImage(img, options);
		}

		//prepare the context
		PNGWriteCtx ctx(&error_state);
		auto& png = ctx.png;
		auto& info = ctx.info;

		png_set_write_fn(png, &file_data, &pngWriteData, &pngFlushData);

		//prepare the image header
		int bit_depth, color_type;
		if (options.indexed) {
			bit_depth = index_result->bit_depth;
			color_type = PNG_COLOR_TYPE_PALETTE;
		} else {
			bit_depth = 8;
			color_type = PNG_COLOR_TYPE_RGB_ALPHA;
		}
		png_set_IHDR(png, info, img.getWidth(), img.getHeight(), bit_depth, color_type,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

		//apply write options
		if (options.compression_level >= 0) {
			png_set_compression_level(png, options.compression_level);
		}

		//if the image is indexed, set the palette
		//  we'll allocate the buffers for this in the outer scope, since it's not clear if libpng copies the PLTE/TRNS
		//  values immediately or only saves the pointer for later.
		std::array<png_color, 256> palette = {};
		std::array<png_byte, 256> palette_alpha = {};
		if (options.indexed) {
			std::transform(
					index_result->palette.begin(), index_result->palette.end(), palette.begin(),
					[](RGBAPixel pixel) -> png_color {
						png_color result = {};
						result.red = rgba_red(pixel);
						result.green = rgba_green(pixel);
						result.blue = rgba_blue(pixel);
						return result;
					});

			png_set_PLTE(png, info, palette.data(), static_cast<int>(index_result->palette.size()));

			//if the palette contains any transparent pixels, add the transparency info
			if (index_result->palette_has_transparency) {
				std::transform(
						index_result->palette.begin(), index_result->palette.end(), palette_alpha.begin(),
						rgba_alpha);

				png_set_tRNS(png, info, palette_alpha.data(), static_cast<int>(index_result->palette.size()), nullptr);
			}
		}

		//prepare pointers to the input rows
		std::vector<png_bytep> row_pointers;
		if (options.indexed) {
			row_pointers = index_result->getRowPointers();
		} else {
			row_pointers.assign(img.getHeight(), nullptr);
			for (size_t row = 0; row < img.getHeight(); row++) {
				row_pointers[row] = reinterpret_cast<png_bytep>(const_cast<RGBAPixel*>(img.rowbegin(row)));
			}
		}
		png_set_rows(png, info, row_pointers.data());

		//actually write the png image
		int transforms;
		if (options.indexed) {
			//indexed images don't need any transformations applied to the row data
			transforms = PNG_TRANSFORM_IDENTITY;
		} else {
			//for RGBA images, we'll need to reverse the byte order if this is a big-endian machine
			if (boost::endian::order::native == boost::endian::order::big) {
				transforms = PNG_TRANSFORM_BGR | PNG_TRANSFORM_SWAP_ALPHA;
			} else {
				transforms = PNG_TRANSFORM_IDENTITY;
			}
		}
		png_write_png(png, info, transforms, nullptr);

		//finally, write the finished PNG image to disk!
		util::writeEntireFile(filename, file_data);
	}
}

void RGBAImage::readPNG(const fs::path& filename) {
	#if HAVE_SPNG_LIBRARY
		*this = readPNG_spng(filename);
		return;
	#endif

	*this = readPNG_libpng(filename);
}

void RGBAImage::writePNG(const fs::path& filename, const WritePngOptions& options) const {
	#if HAVE_SPNG_LIBRARY
		writePNG_spng(*this, filename, options);
		return;
	#endif

	writePNG_libpng(*this, filename, options);
}

/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * Our example here shows how to override the "error_exit" method so that
 * control is returned to the library's caller when a fatal error occurs,
 * rather than calling exit() as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */

struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

bool RGBAImage::readJPEG(const fs::path& filename) {
	/* This struct contains the JPEG decompression parameters and pointers to
	 * working space (which is allocated as needed by the JPEG library).
	 */
	struct jpeg_decompress_struct cinfo;
	/* We use our private extension JPEG error handler.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct my_error_mgr jerr;
	/* More stuff */
	FILE * infile;		/* source file */
	JSAMPARRAY buffer;		/* Output row buffer */
	int row_stride;		/* physical row width in output buffer */

	/* In this example we want to open the input file before doing anything else,
	 * so that the setjmp() error recovery below can assume the file is open.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to read binary files.
	 */

	if ((infile = fopen(filename.c_str(), "rb")) == NULL) {
		//fprintf(stderr, "can't open %s\n", filename.c_str());
		//return 0;
		return false;
	}

	/* Step 1: allocate and initialize JPEG decompression object */

	/* We set up the normal JPEG error routines, then override error_exit. */
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp(jerr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled an error.
		 * We need to clean up the JPEG object, close the input file, and return.
		 */
		jpeg_destroy_decompress(&cinfo);
		fclose(infile);
		return 0;
	}
	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress(&cinfo);

	/* Step 2: specify data source (eg, a file) */

	jpeg_stdio_src(&cinfo, infile);

	/* Step 3: read file parameters with jpeg_read_header() */

	(void) jpeg_read_header(&cinfo, TRUE);
	/* We can ignore the return value from jpeg_read_header since
	 *	 (a) suspension is not possible with the stdio data source, and
	 *	 (b) we passed TRUE to reject a tables-only JPEG file as an error.
	 * See libjpeg.txt for more info.
	 */

	/* Step 4: set parameters for decompression */

	/* In this example, we don't need to change any of the defaults set by
	 * jpeg_read_header(), so we do nothing here.
	 */

	/* Step 5: Start decompressor */

	(void) jpeg_start_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	 * with the stdio data source.
	 */

	/* We may need to do some setup of our own at this point before reading
	 * the data.	After jpeg_start_decompress() we have the correct scaled
	 * output image dimensions available, as well as the output colormap
	 * if we asked for color quantization.
	 * In this example, we need to make an output work buffer of the right size.
	 */
	/* JSAMPLEs per row in output buffer */
	row_stride = cinfo.output_width * cinfo.output_components;
	/* Make a one-row-high sample array that will go away when done with image */
	buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

	/* Step 6: while (scan lines remain to be read) */
	/*					 jpeg_read_scanlines(...); */

	setSize(cinfo.output_width, cinfo.output_height);

	/* Here we use the library's state variable cinfo.output_scanline as the
	 * loop counter, so that we don't have to keep track ourselves.
	 */
	while (cinfo.output_scanline < cinfo.output_height) {
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could ask for
		 * more than one scanline at a time if that's more convenient.
		 */
		(void) jpeg_read_scanlines(&cinfo, buffer, 1);
		/* Assume put_scanline_someplace wants a pointer and sample count. */
		for(size_t x = 0; x < width; x++) {
			uint8_t red = buffer[0][3 * x];
			uint8_t green = buffer[0][3 * x + 1];
			uint8_t blue = buffer[0][3 * x + 2];
			// output_scanline is increased by the jpeg_read_scanlines method
			// before we use it for the image, that's why the -1
			pixel(x, cinfo.output_scanline - 1) = rgba(red, green, blue, 255);
		}
	}

	/* Step 7: Finish decompression */

	(void) jpeg_finish_decompress(&cinfo);
	/* We can ignore the return value since suspension is not possible
	 * with the stdio data source.
	 */

	/* Step 8: Release JPEG decompression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress(&cinfo);

	/* After finish_decompress, we can close the input file.
	 * Here we postpone it until after no more JPEG errors are possible,
	 * so as to simplify the setjmp error logic above.	(Actually, I don't
	 * think that jpeg_destroy can do an error exit, but why assume anything...)
	 */
	fclose(infile);

	/* At this point you may want to check to see whether any corrupt-data
	 * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	 */

	/* And we're done! */

	return true;
}

bool RGBAImage::writeJPEG(const fs::path& filename, int quality,
		RGBAPixel background) const {

	/* This struct contains the JPEG compression parameters and pointers to
	 * working space (which is allocated as needed by the JPEG library).
	 * It is possible to have several such structures, representing multiple
	 * compression/decompression processes, in existence at once.	We refer
	 * to any one struct (and its associated working data) as a "JPEG object".
	 */
	struct jpeg_compress_struct cinfo;
	/* This struct represents a JPEG error handler.	It is declared separately
	 * because applications often want to supply a specialized error handler
	 * (see the second half of this file for an example).	But here we just
	 * take the easy way out and use the standard error handler, which will
	 * print a message on stderr and call exit() if compression fails.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct jpeg_error_mgr jerr;
	/* More stuff */
	FILE * outfile;		/* target file */

	/* Step 1: allocate and initialize JPEG compression object */

	/* We have to set up the error handler first, in case the initialization
	 * step fails.	(Unlikely, but it could happen if you are out of memory.)
	 * This routine fills in the contents of struct jerr, and returns jerr's
	 * address which we place into the link field in cinfo.
	 */
	cinfo.err = jpeg_std_error(&jerr);
	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */

	/* Here we use the library-supplied code to send compressed data to a
	 * stdio stream.	You can also write your own code to do something else.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to write binary files.
	 */
	if ((outfile = fopen(filename.c_str(), "wb")) == NULL) {
		//fprintf(stderr, "can't open %s\n", filename.c_str());
		//exit(1);
		return false;
	}
	jpeg_stdio_dest(&cinfo, outfile);

	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	 * Four fields of the cinfo struct must be filled in:
	 */
	cinfo.image_width = width; 	/* image width and height, in pixels */
	cinfo.image_height = height;
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
	/* Now use the library's routine to set default compression parameters.
	 * (You must set at least cinfo.in_color_space before calling this,
	 * since the defaults depend on the source color space.)
	 */
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	 * Here we just illustrate the use of quality (quantization table) scaling:
	 */
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	 * Pass TRUE unless you are very sure of what you're doing.
	 */
	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*					 jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	 * loop counter, so that we don't have to keep track ourselves.
	 * To keep things simple, we pass one scanline per call; you can pass
	 * more if you wish, though.
	 */
	std::vector<JSAMPLE> line_buffer(width * 3, 0);
	JSAMPLE* scanlineData = &line_buffer[0];

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could pass
		 * more than one scanline at a time if that's more convenient.
		 */
		for (size_t x = 0; x < width; x++) {
			RGBAPixel color = pixel(x, cinfo.next_scanline);
			// jpeg does not support transparency
			// add background color if this pixel has transparency
			// but ignore a bit transparency
			if (rgba_alpha(color) < 250) {
				color = rgba_alphablend(background, color);
			}

			line_buffer[3 * x] = rgba_red(color);
			line_buffer[3 * x + 1] = rgba_green(color);
			line_buffer[3 * x + 2] = rgba_blue(color);
		}
		(void) jpeg_write_scanlines(&cinfo, &scanlineData, 1);
	}

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);
	/* After finish_compress, we can close the output file. */
	fclose(outfile);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */
	return true;
}

}
}
