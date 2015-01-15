/*
 * Copyright 2012-2015 Moritz Hilscher
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

#include "renderview.h"

#include "blockimages.h"
#include "tileset.h"
#include "tilerenderer.h"

namespace mapcrafter {
namespace renderer {

BlockImages* IsometricRenderView::createBlockImages() const {
	return new IsometricBlockImages();
}

TileSet* IsometricRenderView::createTileSet(int tile_width) const {
	return new IsometricTileSet(tile_width);
}

TileRenderer* IsometricRenderView::createTileRenderer(BlockImages* images,
		int tile_width, mc::WorldCache* world, RenderModes& render_modes) const {
	return new IsometricTileRenderer(images, tile_width, world, render_modes);
}

void IsometricRenderView::configureTileRenderer(TileRenderer* tile_renderer,
		const config::WorldSection& world_config,
		const config::MapSection& map_config) const {
	RenderView::configureTileRenderer(tile_renderer, world_config, map_config);

	IsometricTileRenderer* renderer = dynamic_cast<IsometricTileRenderer*>(tile_renderer);
	if (renderer != nullptr) {
		// TODO
		renderer->setUsePreblitWater(false);
	}
}

} /* namespace renderer */
} /* namespace mapcrafter */