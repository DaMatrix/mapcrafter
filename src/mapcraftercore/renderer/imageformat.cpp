#include "imageformat.h"

#include "image/dithering.h"
#include "image/quantization.h"
#include "../util/other.h" //util::isBigEndian()

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/string_file.hpp>
#include <png.h>

#include <cstdint>
#include <cstddef>

#include <stdexcept> //std::invalid_argument, std::runtime_error
#include <string>

namespace mapcrafter {
namespace renderer {

ImageFormat::ImageFormat(const char *fileExtension)
	: _fileExtension(fileExtension) {
}

ImageFormat::~ImageFormat() = default;

namespace {

class ImageFormatPNG : public ImageFormat {
	/**
	 * http://www.piko3d.com/tutorials/libpng-tutorial-loading-png-files-from-streams
	 */
	static void pngReadData(png_structp pngPtr, png_bytep data, png_size_t length) {
		//Here we get our IO pointer back from the read struct.
		//This is the parameter we passed to the png_set_read_fn() function.
		//Our std::istream pointer.
		png_voidp a = png_get_io_ptr(pngPtr);
		//Cast the pointer to std::istream* and read 'length' bytes into 'data'
		static_cast<std::istream *>(a)->read(reinterpret_cast<char *>(data), length);
	}

	static void pngWriteData(png_structp pngPtr, png_bytep data, png_size_t length) {
		png_voidp a = png_get_io_ptr(pngPtr);
		static_cast<std::string *>(a)->append(reinterpret_cast<char *>(data), length);
	}

	int palette_bits;
	bool dithered;
	bool indexed;

public:
	ImageFormatPNG()
		: ImageFormat("png"),
		  indexed(false) {
	}

	ImageFormatPNG(int palette_bits, bool dithered)
		: ImageFormat("png"),
		  palette_bits(palette_bits), dithered(dithered),
		  indexed(true) {
	}

	RGBAImage readImage(const boost::filesystem::path &path) const override {
		std::string file_data;
		boost::filesystem::load_string_file(path, file_data);

		std::istringstream file_stream(std::move(file_data), std::ios::binary);
		file_stream.exceptions(std::ofstream::failbit | std::ofstream::badbit | std::ofstream::eofbit);

		uint8_t png_signature[8];
		file_stream.read((char *) &png_signature, 8);
		if (png_sig_cmp(png_signature, 0, 8) != 0)
			throw std::invalid_argument("invalid PNG signature!");

		struct PNGReadState {
			png_structp png = nullptr;
			png_infop info = nullptr;

			~PNGReadState() {
				png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
			}
		};

		PNGReadState state;
		state.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!state.png)
			throw std::runtime_error("failed to create PNG reader");

		state.info = png_create_info_struct(state.png);
		if (!state.info)
			throw std::runtime_error("failed to create PNG info struct");

		if (setjmp(png_jmpbuf(state.png)))
			throw std::runtime_error("exception occurred while reading PNG image");

		png_set_read_fn(state.png, &file_stream, pngReadData);
		png_set_sig_bytes(state.png, 8);

		png_read_info(state.png, state.info);
		int color = png_get_color_type(state.png, state.info);
		int bit_depth = png_get_bit_depth(state.png, state.info);

		// strip down images of 16 bits per channel to 8 bits per channel
		if (bit_depth == 16)
			png_set_strip_16(state.png);

		// convert gray images to rgb(a)
		if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_to_rgb(state.png);
		// make sure they are also using 8 bit per channel
		if (color == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
			png_set_expand_gray_1_2_4_to_8(state.png);

		// convert indexed images to rgb(a)
		if (color == PNG_COLOR_TYPE_PALETTE)
			png_set_palette_to_rgb(state.png);

		// add alpha channel if not existing
		if ((color & PNG_COLOR_MASK_ALPHA) == 0)
			png_set_add_alpha(state.png, 0xff, PNG_FILLER_AFTER);

		RGBAImage result(png_get_image_width(state.png, state.info), png_get_image_height(state.png, state.info));

		png_set_interlace_handling(state.png);
		png_read_update_info(state.png, state.info);

		std::unique_ptr<png_bytep[]> rows(new png_bytep[result.height]);
		uint32_t *p = result.data.get();
		for (size_t i = 0; i < result.height; i++, p += result.width)
			rows[i] = (png_bytep) p;

		if (mapcrafter::util::isBigEndian()) {
			png_set_bgr(state.png);
			png_set_swap_alpha(state.png);
		}
		png_read_image(state.png, rows.get());
		png_read_end(state.png, nullptr);

		return result;
	}

	void writeImage(const RGBAImage &image, const boost::filesystem::path &path) const override {
		std::string file_data;
		//this should be more than enough space
		file_data.reserve(image.getWidth() * image.getHeight() * sizeof(RGBAPixel) * 2);

		struct PNGWriteState {
			png_structp png = nullptr;
			png_infop info = nullptr;

			~PNGWriteState() {
				png_destroy_write_struct(&png, info ? &info : nullptr);
			}
		};

		PNGWriteState state;
		state.png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if (!state.png)
			throw std::runtime_error("failed to create PNG writer");

		state.info = png_create_info_struct(state.png);
		if (!state.info)
			throw std::runtime_error("failed to create PNG info struct");

		//define all local variables with destructors here so that they get destroyed if libpng jumps to this setjmp
		//point
		std::unique_ptr<png_bytep[]> rows;
		std::vector<RGBAPixel> colors;
		std::unique_ptr<png_color[]> palette;
		std::unique_ptr<png_byte[]> palette_alpha;
		std::unique_ptr<png_byte[]> rows_cols;
		std::vector<int> data_dithered;

		if (setjmp(png_jmpbuf(state.png)))
			throw std::runtime_error("exception occurred while writing PNG image");

		png_set_write_fn(state.png, &file_data, pngWriteData, nullptr);

		if (indexed) {
			png_set_IHDR(state.png, state.info, image.width, image.height,
			             palette_bits, PNG_COLOR_TYPE_PALETTE,
			             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

			//std::cout << "Doing quantization." << std::endl;
			octreeColorQuantize(image, 1 << palette_bits, colors, nullptr);
			size_t palette_size = colors.size();
			//std::cout << "Finished quantization. " << palette_size << " colors." << std::endl;

			palette.reset(new png_color[palette_size]);
			palette_alpha.reset(new png_byte[palette_size]);

			for (size_t i = 0; i < palette_size; i++) {
				palette[i].red = rgba_red(colors[i]);
				palette[i].green = rgba_green(colors[i]);
				palette[i].blue = rgba_blue(colors[i]);
				palette_alpha[i] = rgba_alpha(colors[i]);
			}

			png_set_PLTE(state.png, state.info, palette.get(), palette_size);
			png_set_tRNS(state.png, state.info, palette_alpha.get(), palette_size, nullptr);

			{
				OctreePalette p(colors);
				//OctreePalette2 p(colors);

				if (dithered) {
					RGBAImage copy = image;
					imageDither(copy, p, data_dithered);
				}

				rows_cols.reset(new png_byte[image.width * image.height]()); //zero-initialize

				rows.reset(new png_bytep[image.height]);
				for (size_t y = 0; y < image.height; y++)
					rows[y] = &rows_cols[y * image.width];

				auto setRowPixel = [](png_byte *line, int bit_depth, int x, uint8_t index) {
					if (bit_depth == 8) {
						line[x] = index;
					} else if (bit_depth == 4) {
						index &= 0xf;
						if ((x % 2) == 0)
							line[x / 2] = (line[x / 2] & 0x0f) | (index << 4);
						else
							line[x / 2] = (line[x / 2] & 0xf0) | index;
					} else if (bit_depth == 2) {
						index &= 0x3;
						int mod = 3 - (x % 4);
						line[x / 4] = (line[x / 4] & ~(0x3 << mod * 2)) | (index << mod * 2);
					} else if (bit_depth == 1) {
						if (index)
							line[x / 8] |= (1 << (7 - (x % 8)));
						else
							line[x / 8] &= ~(1 << (7 - (x % 8)));
					}
				};

				for (int y = 0; y < image.height; y++) {
					auto *row = rows[y];
					for (int x = 0; x < image.width; x++) {
						if (dithered) {
							setRowPixel(row, palette_bits, x, data_dithered[y * image.width + x]);
						} else {
							setRowPixel(row, palette_bits, x, p.getNearestColor(image.pixel(x, y)));
						}
					}
				}
			}

			png_set_rows(state.png, state.info, rows.get());

			png_write_png(state.png, state.info, PNG_TRANSFORM_IDENTITY, nullptr);
		} else {
			png_set_IHDR(state.png, state.info, image.width, image.height,
			             8,PNG_COLOR_TYPE_RGB_ALPHA,
			             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

			rows.reset(new png_bytep[image.height]);
			const uint32_t *p = image.data.get();
			for (size_t i = 0; i < image.height; i++, p += image.width)
				rows[i] = (png_bytep) p;

			png_set_rows(state.png, state.info, rows.get());

			if (mapcrafter::util::isBigEndian())
				png_write_png(state.png, state.info, PNG_TRANSFORM_BGR | PNG_TRANSFORM_SWAP_ALPHA, nullptr);
			else
				png_write_png(state.png, state.info, PNG_TRANSFORM_IDENTITY, nullptr);
		}

		boost::filesystem::save_string_file(path, file_data);
	}
};
}

std::unique_ptr<ImageFormat> ImageFormat::createPNG() {
	return std::unique_ptr<ImageFormat>(new ImageFormatPNG());
}

std::unique_ptr<ImageFormat> ImageFormat::createIndexedPNG(unsigned palette_bits, bool dither) {
	return std::unique_ptr<ImageFormat>(new ImageFormatPNG(palette_bits, dither));
}

namespace {
class ImageFormatJPG : public ImageFormat {
	int quality;
	RGBAPixel background;

public:
	ImageFormatJPG(int quality, RGBAPixel background)
		: ImageFormat("jpg"),
		  quality(quality),
		  background(background) {
	}

	RGBAImage readImage(const boost::filesystem::path &path) const override {
		RGBAImage result;
		if (!result.readJPEG(path.string()))
			throw std::runtime_error("failed to load JPEG image");
		return result;
	}

	void writeImage(const RGBAImage &image, const boost::filesystem::path &path) const override {
		if (!image.writeJPEG(path.string(), quality, background))
			throw std::runtime_error("failed to write JPEG image");
	}
};
}

std::unique_ptr<ImageFormat> ImageFormat::createJPEG(int quality, RGBAPixel background) {
	return std::unique_ptr<ImageFormat>(new ImageFormatJPG(quality, background));
}
}
}
