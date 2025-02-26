#include "scaling.h"

#include "../image.h"

namespace mapcrafter {
namespace renderer {

void imageResizeSimple(const RGBAImage& image, RGBAImage& dest, size_t width, size_t height) {
	assert(&image != &dest);
	dest.setSize(width, height, util::UninitializedTag{});

	double x_ratio = (double) width / image.getWidth();
	double y_ratio = (double) height / image.getHeight();
	for (size_t y = 0; y < height; y++) {
		for (size_t x = 0; x < width; x++) {
			dest.setPixel(x, y, image.getPixel(x / x_ratio, y / y_ratio));
		}
	}
}

namespace {

uint8_t interpolate(uint8_t a, uint8_t b, uint8_t c, uint8_t d, double w, double h) {
	double aa = (double) a / 255.0;
	double bb = (double) b / 255.0;
	double cc = (double) c / 255.0;
	double dd = (double) d / 255.0;
	double result = aa * (1 - w) * (1 - h) + bb * w * (1 - h) + cc * h * (1 - w) + dd * (w * h);
	return result * 255;
}

}

void imageResizeBilinear(const RGBAImage& image, RGBAImage& dest, size_t width, size_t height) {
	assert(&image != &dest);
	dest.setSize(width, height, util::UninitializedTag{});

	double x_ratio = (double) image.getWidth() / width;
	double y_ratio = (double) image.getWidth() / height;
	if(image.getWidth() < width)
		x_ratio = (double) (image.getWidth() - 1) / width;
	if(image.getHeight() < height)
		y_ratio = (double) (image.getWidth() - 1) / height;

	for (size_t y = 0; y < height; y++) {
		for (size_t x = 0; x < width; x++) {
			size_t sx = x_ratio * x;
			size_t sy = y_ratio * y;
			double x_diff = (x_ratio * x) - sx;
			double y_diff = (y_ratio * y) - sy;
			RGBAPixel a = image.getPixel(sx, sy);
			RGBAPixel b = image.getPixel(sx + 1, sy);
			RGBAPixel c = image.getPixel(sx, sy + 1);
			RGBAPixel d = image.getPixel(sx + 1, sy + 1);

			uint8_t red = interpolate(rgba_red(a), rgba_red(b), rgba_red(c), rgba_red(d), x_diff, y_diff);
			uint8_t green = interpolate(rgba_green(a), rgba_green(b), rgba_green(c), rgba_green(d), x_diff, y_diff);
			uint8_t blue = interpolate(rgba_blue(a), rgba_blue(b), rgba_blue(c), rgba_blue(d), x_diff, y_diff);
			uint8_t alpha = interpolate(rgba_alpha(a), rgba_alpha(b), rgba_alpha(c), rgba_alpha(d), x_diff, y_diff);

			// make sure that that no transparency (aka alpha=254) sneaks into images
			// caused by weird interpolation bugses, otherwise shit hits the fan
			// when all blocks contain just a bit of transparency
			
			// do this by just clamping alpha >= threshold to 255, 245 or 255 won't be
			// a big visual difference
			if (alpha >= 245)
				alpha = 255;
		
			dest.setPixel(x, y, rgba(red, green, blue, alpha));
		}
	}
}

AUTO_TARGET_CLONES void imageResizeHalf(const RGBAImage& image, RGBAImage& dst) {
	assert(&image != &dst);
	size_t width = image.getWidth();
	size_t height = image.getHeight();
	assert(width % 2 == 0 && height % 2 == 0 && "image size must be divisible by two!");
	dst.setSize(width / 2, height / 2, util::UninitializedTag{});

	/*for (size_t x = 0; x < width - 1; x += 2) {
		for (size_t y = 0; y < height - 1; y += 2) {
			RGBAPixel p1 = image.pixel(x, y);
			RGBAPixel p2 = image.pixel(x + 1, y);
			RGBAPixel p3 = image.pixel(x, y + 1);
			RGBAPixel p4 = image.pixel(x + 1, y + 1);
			RGBAPixel highBits = ((p1 >> 2) & 0x3f3f3f3f) + ((p2 >> 2) & 0x3f3f3f3f) + ((p3 >> 2) & 0x3f3f3f3f) + ((p4 >> 2) & 0x3f3f3f3f);
			RGBAPixel lowBits = (((p1 & 0x03030303) + (p2 & 0x03030303) + (p3 & 0x03030303) + (p4 & 0x03030303)) >> 2) & 0x03030303;
			dest.pixel(x >> 1, y >> 1) = highBits + lowBits;
		}
	}*/

	const RGBAPixel* src_it = image.begin();
	RGBAPixel* dst_it = dst.begin();
	for (size_t row = 0; row < height; row++) {
		// For each row: iterate along two rows in the source image at once, reading two pixels from each (for a total
		// of four pixels at a time), averaging them out and writing a single pixel into the destination image.
		// This loop is simple enough to be autovectorized by both GCC and clang.

		const RGBAPixel* src_it0 = src_it;
		const RGBAPixel* src_it1 = src_it + width;
		for (size_t col = 0; col < width; col++) {
			RGBAPixel p1 = src_it0[0];
			RGBAPixel p2 = src_it0[1];
			RGBAPixel p3 = src_it1[0];
			RGBAPixel p4 = src_it1[1];

			RGBAPixel highBits = ((p1 >> 2) & 0x3f3f3f3f) + ((p2 >> 2) & 0x3f3f3f3f) + ((p3 >> 2) & 0x3f3f3f3f) + ((p4 >> 2) & 0x3f3f3f3f);
			RGBAPixel lowBits = (((p1 & 0x03030303) + (p2 & 0x03030303) + (p3 & 0x03030303) + (p4 & 0x03030303)) >> 2) & 0x03030303;
			*dst_it = highBits + lowBits;

			src_it0 += 2;
			src_it1 += 2;
			dst_it++;
		}

		src_it += width * 2;
	}
}

}
}

