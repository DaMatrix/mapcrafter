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

#ifndef IMAGE_SCALING_H_
#define IMAGE_SCALING_H_

#include "../../config.h" //AUTO_TARGET_CLONES

#include <cstddef>

namespace mapcrafter {
namespace renderer {

class RGBAImage;

void imageResizeSimple(const RGBAImage& image, RGBAImage& dest, size_t width, size_t height);
void imageResizeBilinear(const RGBAImage& image, RGBAImage& dest, size_t width, size_t height);
AUTO_TARGET_CLONES void imageResizeHalf(const RGBAImage& image, RGBAImage& dst);

}
}

#endif /* IMAGE_SCALING_H_ */
