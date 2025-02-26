#include "scaling.h"

#include "../image.h"

namespace mapcrafter {
namespace renderer {

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

