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

#include "tilerenderworker.h"

#include "blockimages.h"
#include "image.h"
#include "rendermode.h"
#include "renderview.h"
#include "tilerenderer.h"
#include "tileset.h"
#include "../mc/worldcache.h"
#include "../mc/blockstate.h"
#include "../util.h"

namespace mapcrafter {
namespace renderer {

void RenderContext::initializeTileRenderer() {
	world_cache.reset(new mc::WorldCache(*block_registry, *world));
	render_mode.reset(createRenderMode(world_config, map_config, render_view->getRotation()));
	tile_renderer.reset(render_view->createTileRenderer(*block_registry, block_images,
			map_config.getTileWidth(), world_cache.get(), render_mode.get()));
	render_view->configureTileRenderer(tile_renderer.get(), world_config, map_config);
}

TileRenderWorker::TileRenderWorker()
	: progress(nullptr) {
}

TileRenderWorker::~TileRenderWorker() {
}

void TileRenderWorker::setRenderContext(const RenderContext& context) {
	render_context = context;
}

void TileRenderWorker::setRenderWork(const RenderWork& work) {
	render_work = work;
	render_work_result = RenderWorkResult();
	render_work_result.render_work = work;
}

const RenderWorkResult& TileRenderWorker::getRenderWorkResult() const {
	return render_work_result;
}

void TileRenderWorker::setProgressHandler(util::IProgressHandler* progress) {
	this->progress = progress;
}

void TileRenderWorker::saveTile(const TilePath& tile, const RGBAImage& image) {
	std::string filename = tile.toString() + '.' + render_context.map_config.getImageFormatSuffix();
	if (tile.getDepth() == 0)
		filename = std::string("base.") + render_context.map_config.getImageFormatSuffix();

	fs::path file = render_context.output_dir / filename;
	if (!fs::exists(file.parent_path()))
		fs::create_directories(file.parent_path());

	try {
		render_context.map_config.saveImage(image, file, render_context.background_color);
	} catch (const std::exception& e) {
		LOG(WARNING) << "Unable to write '" << file.string() << '.' << render_context.map_config.getImageFormatSuffix() << "'. Cause: " << e.what();
	}
}

void TileRenderWorker::renderRecursive(const TilePath& tile, RGBAImage& image) {
	// if this is tile is not required or we should skip it, try to load it from file
	if (!render_context.tile_set->isTileRequired(tile)
			|| render_work.tiles_skip.count(tile)) {
		fs::path file = render_context.output_dir
		                / (tile.toString() + '.' + render_context.map_config.getImageFormatSuffix());
		try {
			render_context.map_config.loadImage(image, file);
			if (render_work.tiles_skip.count(tile) && progress != nullptr)
				progress->incrementValue(render_context.tile_set->getContainingRenderTiles(tile));
			return;
		} catch (const std::exception &e) {
			LOG(WARNING) << "Unable to read tile '" << file.string()
					<< "', I will just render it again. Cause: " << e.what();
		}
	}

	if (tile.getDepth() == render_context.tile_set->getDepth()) {
		// this tile is a render tile, render it
		render_context.tile_renderer->renderTile(tile.getTilePos()
				+ render_context.tile_set->getTileOffset(), image);
		render_work_result.tiles_rendered++;

		/*
		// draws a border on the tile
		uint32_t color = rgba(0, 0, 255, 255);
		if (tile.getTilePos() == TilePos(0, 0)) {
			color = rgba(255, 0, 0, 255);
		}
		int border = 1;
		for (int x = 0; x < image.getWidth(); x++) {
			for (int y = 0; y < image.getHeight(); y++) {
				if (x < border || x+1 > image.getWidth() - border) {
					image.setPixel(x, y, color);
				}
				if (y < border || y+1 > image.getHeight() - border) {
					image.setPixel(x, y, color);
				}
			}
		}
		*/

		//ensure that all transparent pixels are canonicalized before writing the image
		image.simplifyTransparentPixels();

		// save it
		saveTile(tile, image);

		// update progress
		if (progress != nullptr)
			progress->incrementValue();
	} else {
		// this tile is a composite tile, we need to compose it from its children
		// just check, if children 1, 2, 3, 4 exists, render it, resize it to the half size
		// and blit it to the properly position
		//int size = render_context.map_config.getTextureSize() * 32 * TILE_WIDTH;
		// TODO
		auto w = render_context.tile_renderer->getTileWidth();
		auto h = render_context.tile_renderer->getTileHeight();
		image.setSize(w, h);

		RGBAImage other;
		if (render_context.tile_set->hasTile(tile + 1)) {
			renderRecursive(tile + 1, other);
			image.simpleBlit(other.resizeHalf(), 0, 0);
		}
		if (render_context.tile_set->hasTile(tile + 2)) {
			renderRecursive(tile + 2, other);
			image.simpleBlit(other.resizeHalf(), w / 2, 0);
		}
		if (render_context.tile_set->hasTile(tile + 3)) {
			renderRecursive(tile + 3, other);
			image.simpleBlit(other.resizeHalf(), 0, h / 2);
		}
		if (render_context.tile_set->hasTile(tile + 4)) {
			renderRecursive(tile + 4, other);
			image.simpleBlit(other.resizeHalf(), w / 2, h / 2);
		}

		/*
		// draws a border on the tile
		for (int x = 0; x < size; x++)
			for (int y = 0; y < size; y++) {
				if (x < 5 || x > size-5)
					tile.setPixel(x, y, rgba(255, 0, 0, 255));
				if (y < 5 || y > size-5)
					tile.setPixel(x, y, rgba(255, 0, 0, 255));
			}
		*/

		//ensure that all transparent pixels are canonicalized before writing the image
		image.simplifyTransparentPixels();

		// then save the tile
		saveTile(tile, image);
	}
}

void TileRenderWorker::operator()() {
	int work = 0;
	for (auto it = render_work.tiles.begin(); it != render_work.tiles.end(); ++it)
		work += render_context.tile_set->getContainingRenderTiles(*it);
	if (progress != nullptr)
		progress->begin(work);

	RGBAImage image;
	// iterate through the start composite tiles
	for (auto it = render_work.tiles.begin(); it != render_work.tiles.end(); ++it) {
		// render this composite tile
		renderRecursive(*it, image);

		// clear image
		image.clear();
	}
}

} /* namespace render */
} /* namespace mapcrafter */
