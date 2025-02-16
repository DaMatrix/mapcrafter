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

#pragma once
#ifndef IMAGEFORMAT_H_
#define IMAGEFORMAT_H_

#include "image.h" //RGBAPixel

#include <memory> //std::unique_ptr
#include <string>

namespace boost {
namespace filesystem {
class path;
}
}

namespace mapcrafter {
namespace renderer {

class ImageFormat {
	std::string _fileExtension;

protected:
	ImageFormat(const char *fileExtension);

public:
	virtual ~ImageFormat();

	const std::string &fileExtension() const { return _fileExtension; }

	/**
	 * Reads an image from the file at the given path and stores the pixel data into an RGBAImage.
	 * @param path the file path
	 * @return the loaded RGBAImage
	 * @throws std::exception if the image could not be read for any reason
	 */
	virtual RGBAImage readImage(const boost::filesystem::path &path) const = 0;

	/**
	 * Writes the given RGBAImage to a file at the given path.
	 * @param image the image to write
	 * @param path the file path
	 * @throws std::exception if the image could not be written for any reason
	 */
	virtual void writeImage(const RGBAImage &image, const boost::filesystem::path &path) const = 0;

	static std::unique_ptr<ImageFormat> createPNG();

	static std::unique_ptr<ImageFormat> createIndexedPNG(unsigned palette_bits = 8, bool dither = true);

	static std::unique_ptr<ImageFormat> createJPEG(int quality,
	                                               RGBAPixel background = rgba(255, 255, 255, 255));
};

}
}

#endif //IMAGEFORMAT_H_
