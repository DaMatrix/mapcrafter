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

#ifndef RENDERVIEW_H_
#define RENDERVIEW_H_

#include "blockimages.h"
#include "rendermode.h"
#include "tileset.h"
#include "tilerenderer.h"
#include "../mc/world.h"

namespace mapcrafter {
namespace renderer {

class RenderView {
public:
	virtual ~RenderView();

	virtual BlockImages* createBlockImages() const = 0;
	virtual TileSet* createTileSet(int tile_width) const = 0;
	virtual TileRenderer* createTileRenderer(BlockImages* images, int tile_width,
			mc::WorldCache* world, RenderModes& render_modes) const = 0;

	/**
	 * Configures a tile renderer with the belonging world- and map section.
	 *
	 * If you overwrite this method, you should also call the parent method since it
	 * sets generic tile renderer options such as whether to render biomes.
	 */
	virtual void configureTileRenderer(TileRenderer* tile_renderer,
			const config::WorldSection& world_config,
			const config::MapSection& map_config) const;
};

RenderView* createRenderView(const std::string& render_view);

} /* namespace renderer */
} /* namespace mapcrafter */

#endif /* RENDERVIEW_H_ */