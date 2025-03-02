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

#ifndef SECTIONS_MAP_H_
#define SECTIONS_MAP_H_

#include "../configsection.h"
#include "../validation.h"
#include "../../renderer/rendermode.h"
#include "../../renderer/renderview.h"
#include "../../util/filesystem.h"

#include <iostream>
#include <set>
#include <string>

namespace mapcrafter {
namespace renderer {
class RGBAImage;
}

namespace config {

struct Color;

/**
 * Represents all tile sets which are using the same world (as specified in the world
 * config section, with a specific world crop eventually), render view and tile width,
 * independent of the rotation of the used world.
 */
class TileSetGroupID {
public:
	TileSetGroupID();
	TileSetGroupID(const std::string& world_name, renderer::RenderViewType render_view,
			int tile_width);

	std::string toString() const;
	bool operator<(const TileSetGroupID& other) const;

	std::string world_name;
	renderer::RenderViewType render_view;
	int tile_width;
};

/**
 * Represents a single tile set (like a unique id) by storing the tile sets world,
 * render view, tile width and world rotation.
 *
 * Just like the TileSetGroupID, but with the rotation additionally. This is because
 * some attributes of the tile sets need to be stored across different rotations.
 */
class TileSetID : public TileSetGroupID {
public:
	TileSetID();
	TileSetID(const std::string& world_name, renderer::RenderViewType render_view,
			int tile_width, renderer::RenderRotation::Direction rotation);
	TileSetID(const TileSetGroupID& group, renderer::RenderRotation::Direction rotation);

	std::string toString() const;
	bool operator<(const TileSetID& other) const;

	renderer::RenderRotation::Direction rotation;
};

enum class ImageFormat {
	PNG,
	JPEG
};

std::ostream& operator<<(std::ostream& out, ImageFormat image_format);

class INIConfigSection;

class MapSection : public ConfigSection {
public:
	MapSection();
	~MapSection();

	virtual std::string getPrettyName() const;
	virtual void dump(std::ostream& out) const;

	void setConfigDir(const fs::path& config_dir);

	const std::string& getShortName() const;
	const std::string& getLongName() const;
	const std::string& getWorld() const;

	renderer::RenderViewType getRenderView() const;
	renderer::RenderModeType getRenderMode() const;
	renderer::OverlayType getOverlay() const;
	const std::set<renderer::RenderRotation::Direction>& getRotations() const;
	const fs::path& getBlockDir() const;
	int getTextureSize() const;
	double getWaterOpacity() const;
	int getTileWidth() const;

	ImageFormat getImageFormat() const;
	const char* getImageFormatSuffix() const;
	bool isPNGIndexed() const;
	int getPNGCompressionLevel() const;
	int getJPEGQuality() const;

	//these functions throw an std::exception if image loading fails
	void loadImage(renderer::RGBAImage& image, const fs::path& filename) const;
	void saveImage(const renderer::RGBAImage& image, const fs::path& filename, const Color& background_color) const;

	double getLightingIntensity() const;
	double getLightingWaterIntensity() const;
	bool renderBiomes() const;
	bool useImageModificationTimes() const;

	TileSetGroupID getTileSetGroup() const;
	TileSetID getTileSet(renderer::RenderRotation::Direction rotation) const;
	const std::set<TileSetID>& getTileSets() const;

protected:
	virtual void preParse(const INIConfigSection& section,
			ValidationList& validation);
	virtual bool parseField(const std::string key, const std::string value,
			ValidationList& validation);
	virtual void postParse(const INIConfigSection& section,
			ValidationList& validation);

private:
	fs::path config_dir;

	std::string name_short, name_long;
	Field<std::string> world;

	Field<renderer::RenderViewType> render_view;
	Field<renderer::RenderModeType> render_mode;
	Field<renderer::OverlayType> overlay;
	Field<std::string> rotations;
	std::set<renderer::RenderRotation::Direction> rotations_set;

	Field<fs::path> block_dir;
	Field<int> texture_size, tile_width;
	Field<double> water_opacity;

	Field<ImageFormat> image_format;
    Field<bool> png_indexed;
	Field<int> png_compression_level;
	Field<int> jpeg_quality;

	Field<double> lighting_intensity, lighting_water_intensity;
	Field<bool> cave_high_contrast;
	Field<bool> render_biomes, use_image_mtimes;

	std::set<TileSetID> tile_sets;
};

} /* namespace config */
} /* namespace mapcrafter */

#endif /* SECTIONS_MAP_H_ */
