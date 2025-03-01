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

#include "manager.h"

#include "blockimages.h"
#include "tilerenderworker.h"
#include "renderview.h"
#include "../renderer/biomes.h"
#include "../config/loggingconfig.h"
#include "../mc/blockstate.h"
#include "../thread/impl/singlethread.h"
#include "../thread/impl/multithreading.h"
#include "../thread/dispatcher.h"
#include "../util.h"
#include "../version.h"

#include <cstring>
#include <array>
#include <fstream>
#include <memory>
#include <thread>
#include <tuple>

namespace mapcrafter {
namespace renderer {

RenderBehaviors::RenderBehaviors(RenderBehavior default_behavior)
	: default_behavior(default_behavior) {
}

RenderBehaviors::~RenderBehaviors() {
}

RenderBehavior RenderBehaviors::getRenderBehavior(const std::string& map,
		RenderRotation::Direction rotation) const {
	if (!render_behaviors.count(map))
		return default_behavior;
	return render_behaviors.at(map).at(rotation);
}

void RenderBehaviors::setRenderBehavior(const std::string& map,
		RenderBehavior behavior) {
	for (int rotation = 0; rotation < 4; rotation++)
		render_behaviors[map][rotation] = behavior;
}

void RenderBehaviors::setRenderBehavior(const std::string& map, RenderRotation::Direction rotation,
		RenderBehavior behavior) {
	// set whole map to default behavior if setting the first rotation
	if (!render_behaviors.count(map))
		setRenderBehavior(map, default_behavior);
	render_behaviors[map][rotation] = behavior;
}

bool RenderBehaviors::isCompleteRenderSkip(const std::string& map) const {
	if (!render_behaviors.count(map))
		return default_behavior == RenderBehavior::SKIP;
	for (int rotation = 0; rotation < 4; rotation++)
		if (render_behaviors.at(map).at(rotation) != RenderBehavior::SKIP)
			return false;
	return true;
}

namespace {

void parseRenderBehaviorMaps(const std::vector<std::string>& maps,
		RenderBehavior behavior, RenderBehaviors& behaviors,
		const config::MapcrafterConfig& config) {
	for (auto map_it = maps.begin(); map_it != maps.end(); ++map_it) {
		std::string map = *map_it;
		std::string rotation;

		size_t pos = map.find(":");
		if (pos != std::string::npos) {
			rotation = map.substr(pos+1);
			map = map.substr(0, pos);
		} else {
			rotation = "";
		}

		// TODO maybe also move that conversion out to a file with global constants
		RenderRotation::Direction r = RenderRotation::Direction::ALL;
		if (rotation == "tl") r = RenderRotation::TOP_LEFT;
		if (rotation == "tr") r = RenderRotation::TOP_RIGHT;
		if (rotation == "br") r = RenderRotation::BOTTOM_RIGHT;
		if (rotation == "bl") r = RenderRotation::BOTTOM_LEFT;

		if (!config.hasMap(map)) {
			LOG(WARNING) << "Unknown map '" << map << "'.";
			continue;
		}

		if (!rotation.empty()) {
			if (r == RenderRotation::ALL) {
				LOG(WARNING) << "Unknown rotation '" << rotation << "'.";
				continue;
			}
			if (!config.getMap(map).getRotations().count((RenderRotation::Direction)r)) {
				LOG(WARNING) << "Map '" << map << "' does not have rotation '" << rotation << "'.";
				continue;
			}
		}

		if (r != RenderRotation::ALL)
			behaviors.setRenderBehavior(map, r, behavior);
		else
			behaviors.setRenderBehavior(map, behavior);
	}
}

}

RenderBehaviors RenderBehaviors::fromRenderOpts(
		const config::MapcrafterConfig& config, const RenderOpts& render_opts) {
	RenderBehaviors behaviors;

	if (render_opts.skip_all)
		behaviors = RenderBehaviors(RenderBehavior::SKIP);
	else if (render_opts.force_all)
		behaviors = RenderBehaviors(RenderBehavior::FORCE);
	else
		parseRenderBehaviorMaps(render_opts.render_skip, RenderBehavior::SKIP, behaviors, config);
	parseRenderBehaviorMaps(render_opts.render_auto, RenderBehavior::AUTO, behaviors, config);
	parseRenderBehaviorMaps(render_opts.render_force, RenderBehavior::FORCE, behaviors, config);
	return behaviors;
}

RenderManager::RenderManager(const config::MapcrafterConfig& config)
	: config(config), web_config(config), time_started_scanning(0) {
}

void RenderManager::setRenderBehaviors(const RenderBehaviors& render_behaviors) {
	this->render_behaviors = render_behaviors;
}

bool RenderManager::initialize() {
	// an output directory would be nice -- create one if it does not exist
	if (!fs::is_directory(config.getOutputDir()) && !fs::create_directories(config.getOutputDir())) {
		LOG(FATAL) << "Error: Unable to create output directory!";
		return false;
	}

	// read parameters of already rendered maps
	return web_config.readConfigJS();
}

bool RenderManager::scanWorlds() {
	auto config_worlds = config.getWorlds();
	auto config_maps = config.getMaps();

	time_started_scanning = std::time(nullptr);

	// first of all check which maps/rotations are required
	// and which tile sets (world, render view, tile width) with which rotations are needed
	//	all needed tile sets = all tile sets of maps that are not completely skipped
	// (some rotations of a map are skipped, but others are not
	//  => map tile sets are still needed)
	std::set<config::TileSetID> needed_tile_sets;
	for (auto map_it = config_maps.begin(); map_it != config_maps.end(); ++map_it) {
		std::string map = map_it->getShortName();
		if (render_behaviors.isCompleteRenderSkip(map))
			continue;

		// just the rotations that are not to be skipped are required
		std::set<RenderRotation::Direction> required_rotations;
		auto map_tile_sets = map_it->getTileSets();
		for (auto tile_set_it = map_tile_sets.begin();
				tile_set_it != map_tile_sets.end(); ++tile_set_it) {
			RenderRotation::Direction rotation = tile_set_it->rotation;
			// but we have to scan every rotation of every map to make sure that all
			// rotations of a map use the same zoom level, especially when just one
			// rotation is rendered but the other ones are skipped
			needed_tile_sets.insert(*tile_set_it);
			if (render_behaviors.getRenderBehavior(map, rotation) != RenderBehavior::SKIP)
				required_rotations.insert(rotation);
		}

		required_maps.push_back(std::make_pair(map, required_rotations));
	}

	// store the maximum max zoom level of every tile set with its rotations
	std::map<config::TileSetGroupID, int> tile_sets_max_zoom;

	// iterate through all tile sets that are needed
	for (auto tile_set_it = needed_tile_sets.begin();
			tile_set_it != needed_tile_sets.end(); ++tile_set_it) {
		config::WorldSection world_config = config.getWorld(tile_set_it->world_name);
		RenderView* render_view = createRenderView(tile_set_it->render_view, tile_set_it->rotation, 1.0f );

		// load the world
		std::shared_ptr<mc::World> world(new mc::World(world_config.getInputDir().string(),
				world_config.getDimension(), config.getCachePath(world_config.getShortName()).string()));
		world->setWorldCrop(world_config.getWorldCrop());
		if (!world->load()) {
			LOG(FATAL) << "Unable to load world " << tile_set_it->world_name << "!";
			return false;
		}
		// int world_version = world->getMinecraftVersion();
		// if (world_version == -1) {
		// 	LOG(WARNING) << "Unable to determine Minecraft version of world '"
		// 		<< tile_set_it->world_name << "'. Maybe level.dat doesn't exist in world directory?";
		// 	LOG(WARNING) << "Note that rendering of pre-1.13 worlds is not supported, "
		// 		<< "in case Mapcrafter fails to read the world.";
		// 	LOG(WARNING) << "See Mapcrafter legacy for rendering of older worlds. TODO";
		// } else if (world_version < 2860) {
		// 	// 2860 is 1.18.1, should be first version of remodeled 3d chunk based
		// 	LOG(ERROR) << "Rendering of world '" << tile_set_it->world_name << "'  is not supported.";
		// 	LOG(ERROR) << "This version of Mapcrafter supports only worlds of Minecraft 1.18.1 and newer";
		// 	LOG(ERROR) << "See Mapcrafter legacy for rendering of older worlds. TODO";
		// 	return false;
		// }

		// create a tile set for this world
		std::shared_ptr<TileSet> tile_set(render_view->createTileSet(tile_set_it->tile_width));
		// and scan the tiles of this world,
		// we automatically center the tiles for cropped worlds, but only...
		//  - the circular cropped ones and
		//  - the ones with completely specified x- AND z-bounds
		if (world_config.needsWorldCentering()) {
			TilePos tile_offset;
			tile_set->scan(*world, true, tile_offset);
			web_config.setTileSetTileOffset(*tile_set_it, tile_offset);
		} else {
			tile_set->scan(*world);
		}

		// key of this tile_sets_max_zoom map is a TileSetGroupID, not TileSetID as we access it
		// since TileSetID is a subclass of TileSetGroupID, only the TileSetGroupID-'functionality' is used
		// TADA C++ magic! (object slicing)
		int& max_zoom = tile_sets_max_zoom[*tile_set_it];
		max_zoom = std::max(max_zoom, tile_set->getDepth());

		// set world- and tileset object in the map
		worlds[tile_set_it->world_name][tile_set_it->rotation] = world;
		tile_sets[*tile_set_it] = tile_set;

		// clean up render view
		delete render_view;
	}

	// set calculated max zoom of tile sets
	for (auto tile_set_it = needed_tile_sets.begin();
			tile_set_it != needed_tile_sets.end(); ++tile_set_it) {
		// same here like above, C++ magic
		int max_zoom = tile_sets_max_zoom[*tile_set_it];
		tile_sets[*tile_set_it]->setDepth(max_zoom);
		web_config.setTileSetsMaxZoom(*tile_set_it, max_zoom);
	}

	writeTemplates();
	return true;
}

void RenderManager::renderMap(const std::string& map, RenderRotation::Direction rotation, int threads,
		util::IProgressHandler* progress) {
	// make sure this map/rotation actually exists and should be rendered
	if (!config.hasMap(map) || !config.getMap(map).getRotations().count((RenderRotation::Direction)rotation)
			|| render_behaviors.getRenderBehavior(map, rotation) == RenderBehavior::SKIP)
		return;

	// do some initialization stuff for every map once
	if (!map_initialized.count(map)) {
		initializeMap(map);
		map_initialized.insert(map);
	}

	config::MapSection map_config = config.getMap(map);
	config::WorldSection world_config = config.getWorld(map_config.getWorld());

	// TODO keep block state registry global per map. or are there any reasons to make more global?
	mc::BlockStateRegistry block_registry;
	std::shared_ptr<RenderView> render_view(createRenderView(map_config.getRenderView(), rotation, map_config.getWaterOpacity()));

	// output a small notice if we render this map incrementally
	if (std::time_t last_rendered = web_config.getMapLastRendered(map, rotation); last_rendered != 0) {
		char buffer[256];
		std::strftime(buffer, sizeof(buffer), "%d %b %Y, %H:%M:%S", std::localtime(&last_rendered));
		LOG(INFO) << "Last rendering was on " << buffer << ".";
	}

	fs::path output_dir = config.getOutputPath(map + "/" + config::ROTATION_NAMES_SHORT[rotation]);
	// get the tile set
	TileSet* tile_set = tile_sets[map_config.getTileSet((RenderRotation::Direction)rotation)].get();
	if (render_behaviors.getRenderBehavior(map, rotation) == RenderBehavior::AUTO) {
		// if incremental render, scan which tiles might have changed
		LOG(INFO) << "Scanning required tiles...";
		// use the incremental check method specified in the config
		if (map_config.useImageModificationTimes())
			tile_set->scanRequiredByFiletimes(output_dir, map_config.getImageFormatSuffix());
		else
			tile_set->scanRequiredByTimestamp(web_config.getMapLastRendered(map, rotation));
	} else {
		// or just set all tiles required if force-rendering
		tile_set->resetRequired();
	}

	// maybe we don't have to render anything at all
	if (tile_set->getRequiredRenderTilesCount() == 0) {
		LOG(INFO) << "No tiles need to get rendered.";
		return;
	}

	// create other stuff for the render dispatcher
	std::shared_ptr<BlockImages> block_images(render_view->createBlockImages(block_registry));
	render_view->configureBlockImages(block_images.get(), world_config, map_config);

	RenderedBlockImages* new_block_images = dynamic_cast<RenderedBlockImages*>(block_images.get());
	if (new_block_images != nullptr) {
		if (!new_block_images->loadBlockImages(map_config.getBlockDir().string(), util::str(map_config.getRenderView()), rotation, map_config.getTextureSize())) {
			LOG(ERROR) << "Skipping remaining rotations.";
			return;
		}
	}

	renderer::Biome::initializeBiomes();

	RenderContext context;
	context.output_dir = output_dir;
	context.world_config = config.getWorld(map_config.getWorld());
	context.map_config = map_config;
	context.background_color = config.getBackgroundColor();
	context.render_view = render_view.get();
	context.block_images = block_images.get();
	context.tile_set = tile_set;
	context.block_registry = &block_registry;
	context.world = worlds[map_config.getWorld()][rotation];
	context.initializeTileRenderer();

	// update map parameters in web config
	int tile_w = context.tile_renderer->getTileWidth();
	int tile_h = context.tile_renderer->getTileHeight();
	web_config.setMapMaxZoom(map, context.tile_set->getDepth());
	web_config.setMapTileSize(map, std::make_tuple<>(tile_w, tile_h));
	web_config.writeConfigJS();

	std::shared_ptr<thread::Dispatcher> dispatcher;
	if (threads == 1 || tile_set->getRequiredRenderTilesCount() == 1)
		dispatcher = std::make_shared<thread::SingleThreadDispatcher>();
	else
		dispatcher = std::make_shared<thread::MultiThreadingDispatcher>(threads);

	// do the dance
	dispatcher->dispatch(context, progress);

	// update the map settings with last render time
	web_config.setMapLastRendered(map, rotation, time_started_scanning);
	web_config.writeConfigJS();
}

bool RenderManager::run(int threads, bool batch) {
	if (!initialize())
		return false;

	LOG(INFO) << "Scanning worlds...";
	if (!scanWorlds())
		return false;

	int progress_maps = 0;
	int progress_maps_all = required_maps.size();
	int time_start_all = std::time(nullptr);

	// go through all required maps
	for (auto map_it = required_maps.begin(); map_it != required_maps.end(); ++map_it) {
		progress_maps++;
		config::MapSection map_config = config.getMap(map_it->first);

		LOG(INFO) << "[" << progress_maps << "/" << progress_maps_all << "] "
			<< "Rendering map " << map_config.getShortName() << " (\""
			<< map_config.getLongName() << "\"):";

		auto required_rotations = map_it->second;
		int progress_rotations = 0;
		int progress_rotations_all = required_rotations.size();

		// now go through the all required rotations of this map and render them
		for (auto rotation_it = required_rotations.begin();
				rotation_it != required_rotations.end(); ++rotation_it) {
			progress_rotations++;

			LOG(INFO) << "[" << progress_maps << "." << progress_rotations << "/"
				<< progress_maps << "." << progress_rotations_all << "] "
				<< "Rendering rotation " << config::ROTATION_NAMES[*rotation_it] << "...";

			std::shared_ptr<util::MultiplexingProgressHandler> progress(new util::MultiplexingProgressHandler);
			util::ProgressBar* progress_bar = nullptr;
			if (batch || !util::isOutTTY()) {
				util::Logging::getInstance().setSinkLogProgress("__output__", true);
			} else {
				progress_bar = new util::ProgressBar;
				progress->addHandler(progress_bar);
			}

			util::LogOutputProgressHandler* log_output = new util::LogOutputProgressHandler;
			progress->addHandler(log_output);

			std::time_t time_start = std::time(nullptr);
			renderMap(map_config.getShortName(), *rotation_it, threads, progress.get());
			std::time_t took = std::time(nullptr) - time_start;

			if (progress_bar != nullptr) {
				progress_bar->finish();
				delete progress_bar;
			}
			delete log_output;

			LOG(INFO) << "[" << progress_maps << "." << progress_rotations << "/"
				<< progress_maps << "." << progress_rotations_all << "] "
				<< "Rendering rotation " << config::ROTATION_NAMES[*rotation_it]
				<< " took " << took << " seconds.";
		}
	}

	std::time_t took_all = std::time(nullptr) - time_start_all;
	LOG(INFO) << "Rendering all worlds took " << took_all << " seconds.";
	LOG(INFO) << "Finished.....aaand it's gone!";
	return true;
}

const std::vector<std::pair<std::string, std::set<RenderRotation::Direction> > >& RenderManager::getRequiredMaps() const {
	return required_maps;
}

void RenderManager::copyTemplateFile(const std::string& filename,
		const std::map<std::string, std::string>& vars) const {
	std::string data = util::readEntireFileToString(config.getTemplatePath(filename));

	for (auto& var : vars)
		data = util::replaceAll(data, "{" + var.first + "}", var.second);

	util::writeEntireFile(config.getOutputPath(filename), data);
}

void RenderManager::writeTemplateIndexHtml() const {
	std::map<std::string, std::string> vars;
	vars["version"] = MAPCRAFTER_VERSION;
	if (strlen(MAPCRAFTER_GITVERSION))
		vars["version"] += std::string(" (") + MAPCRAFTER_GITVERSION + ")";

	time_t t = std::time(nullptr);
	char buffer[256];
	std::strftime(buffer, sizeof(buffer), "%d.%m.%Y, %H:%M:%S", std::localtime(&t));
	vars["lastUpdate"] = buffer;

	vars["backgroundColor"] = config.getBackgroundColor().hex;

	copyTemplateFile("index.html", vars);
}

void RenderManager::writeTemplates() const {
	if (!fs::is_directory(config.getTemplateDir())) {
		LOG(ERROR) << "The template directory does not exist! Can't copy templates!";
		return;
	}

	writeTemplateIndexHtml();
	web_config.writeConfigJS();

	fs::copy_file(config.getTemplatePath("markers.js"), config.getOutputPath("markers.js"), fs::copy_options::skip_existing);

	// copy all other files and directories
	for (const auto& entry : fs::directory_iterator(config.getTemplateDir())) {
		std::string filename = entry.path().filename().string();
		// do not copy the index.html
		if (filename == "index.html")
			continue;
		// and do not overwrite markers.js and markers-generated.js
		if ((filename == "markers.js" || filename == "markers-generated.js")
				&& fs::exists(config.getOutputPath(filename)))
			continue;

		fs::copy(
			entry.path(), config.getOutputPath(filename),
			fs::copy_options::recursive | fs::copy_options::update_existing | fs::copy_options::copy_symlinks);
	}
}

void RenderManager::initializeMap(const std::string& map) {
	config::MapSection map_config = config.getMap(map);

	// get the max zoom level calculated of the current tile set
	int max_zoom = web_config.getTileSetsMaxZoom(map_config.getTileSetGroup());
	// get the old max zoom level (from config.js), will be 0 if not rendered yet
	int old_max_zoom = web_config.getMapMaxZoom(map);
	// if map already rendered: check if the zoom level of the world has increased
	if (old_max_zoom != 0 && old_max_zoom < max_zoom) {
		LOG(INFO) << "The max zoom level was increased from " << old_max_zoom
				<< " to " << max_zoom << ".";
		LOG(INFO) << "I will move some files around...";

		auto background_color = config.getBackgroundColor();

		// if zoom level has increased, increase zoom levels of tile sets
		auto rotations = map_config.getRotations();
		for (auto rotation_it = rotations.begin(); rotation_it != rotations.end(); ++rotation_it) {
			fs::path output_dir = config.getOutputPath(map + "/"
					+ config::ROTATION_NAMES_SHORT[*rotation_it]);
			for (int i = old_max_zoom; i < max_zoom; i++)
				increaseMaxZoom(output_dir, map_config, background_color);
		}
	}

	// update the template with the max zoom level
	// (calculated with tile set in scanWorlds-method)
	web_config.setMapMaxZoom(map, max_zoom);
	web_config.writeConfigJS();
}

/**
 * This method increases the max zoom of a rendered map and makes the necessary changes
 * on the tile tree.
 */
void RenderManager::increaseMaxZoom(const fs::path& dir, const config::MapSection& map_config, const config::Color& background_color) const {
	// find out tile size by reading old base.png image
	RGBAImage old_base;
	map_config.loadImage(old_base, dir / (std::string("base.") + map_config.getImageFormatSuffix()));
	size_t w = old_base.getWidth();
	size_t h = old_base.getHeight();

	if (fs::exists(dir / "1")) {
		// at first rename the directories 1 2 3 4 (zoom level 0) and make new directories
		fs::rename(dir / "1", dir / "1_");
		fs::create_directories(dir / "1");
		// then move the old tile trees one zoom level deeper
		fs::rename(dir / "1_", dir / "1/4");
		// also move the images of the directories
		fs::rename(dir / (std::string("1.") + map_config.getImageFormatSuffix()),
				dir / (std::string("1/4.") + map_config.getImageFormatSuffix()));
	}

	// do the same for the other directories
	if (fs::exists(dir / "2")) {
		fs::rename(dir / "2", dir / "2_");
		fs::create_directories(dir / "2");
		fs::rename(dir / "2_", dir / "2/3");
		fs::rename(dir / (std::string("2.") + map_config.getImageFormatSuffix()),
				dir / (std::string("2/3.") + map_config.getImageFormatSuffix()));
	}

	if (fs::exists(dir / "3")) {
		fs::rename(dir / "3", dir / "3_");
		fs::create_directories(dir / "3");
		fs::rename(dir / "3_", dir / "3/2");
		fs::rename(dir / (std::string("3.") + map_config.getImageFormatSuffix()),
				dir / (std::string("3/2.") + map_config.getImageFormatSuffix()));
	}

	if (fs::exists(dir / "4")) {
		fs::rename(dir / "4", dir / "4_");
		fs::create_directories(dir / "4");
		fs::rename(dir / "4_", dir / "4/1");
		fs::rename(dir / (std::string("4.") + map_config.getImageFormatSuffix()),
				dir / (std::string("4/1.") + map_config.getImageFormatSuffix()));
	}

	// now read the images, which belong to the new directories
	std::array<const char*, 4> IMG_PATHS = {
		"1/4.",
		"2/3.",
		"3/2.",
		"4/1.",
	};
	std::array<RGBAImage, 4> imgs{};
	for (size_t i = 0; i < 4; i++) {
		try {
			map_config.loadImage(imgs[i], dir / (std::string(IMG_PATHS[i]) + map_config.getImageFormatSuffix()));
		} catch (const std::exception&) {
			//ignore
		}
	}

	// resize the old images and simplify their transparent pixels...
	for (RGBAImage& img : imgs) {
		img = img.resizeHalf();
		img.simplifyTransparentPixels();
	}

	// create images for the new directories
	std::array<RGBAImage, 4> news{};
	std::generate(news.begin(), news.end(), [w, h] { return RGBAImage(w, h); });

	// ...to blit them to the images of the new directories
	news[0].simpleBlit(imgs[0], w / 2, h / 2);
	news[1].simpleBlit(imgs[1], 0, h / 2);
	news[2].simpleBlit(imgs[2], w / 2, 0);
	news[3].simpleBlit(imgs[3], 0, 0);

	// now save the new images in the output directory
	std::array<const char*, 4> NEW_PATHS = {
		"1.",
		"2.",
		"3.",
		"4.",
	};
	for (size_t i = 0; i < 4; i++)
		map_config.saveImage(news[i], dir / (std::string(NEW_PATHS[i]) + map_config.getImageFormatSuffix()), background_color);

	// don't forget the base.png
	RGBAImage base(2 * h, 2 * h);
	const std::array<std::pair<int, int>, 4> baseOffsets = {
		std::pair<int, int>(0, 0),
		std::pair<int, int>(w, 0),
		std::pair<int, int>(0, h),
		std::pair<int, int>(w, h),
	};
	for (size_t i = 0; i < 4; i++)
		base.simpleAlphaBlit(news[i], baseOffsets[i].first, baseOffsets[i].second);
	base = base.resizeHalf();
	base.simplifyTransparentPixels();

	map_config.saveImage(base, dir / (std::string("base.") + map_config.getImageFormatSuffix()), background_color);
}

}
}
