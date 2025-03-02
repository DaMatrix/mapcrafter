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

#ifndef WORLD_H_
#define WORLD_H_

#include "chunk.h"
#include "pos.h"
#include "region.h"
#include "worldcrop.h"
#include "../util/filesystem.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

namespace mapcrafter {
namespace mc {

/**
 * Dimension of the Minecraft world.
 * The Nether, normal Overworld or The End.
 */
enum class Dimension {
	NETHER,
	OVERWORLD,
	END,
};

std::ostream& operator<<(std::ostream& out, Dimension dimension);

/**
 * Simple hash function to use regions in unordered_set/map.
 * This is the algorythm used in Minecraft 1.18.1 engine to hash chunk coordinates
 */
struct hash_function_chunk {
	long operator()(const ChunkPos& chunk) const {
		long hash = 0L;
		hash |= ((long)chunk.x & 0x3FFFFF) << 42;
		// hash |= ((long)chunk.y & 0x0FFFFF) << 0;
		hash |= ((long)chunk.z & 0x3FFFFF) << 20;
		return hash;
	}
};
struct hash_function_region {
	long operator()(const RegionPos& region) const {
		long hash = 0L;
		hash |= ((long)region.x & 0x3FFFFF) << 42;
		// hash |= ((long)region.y & 0x0FFFFF) << 0;
		hash |= ((long)region.z & 0x3FFFFF) << 20;
		return hash;
	}
};

/**
 * This class represents a Minecraft World.
 *
 * It manages only the available region files. Access to the chunks is with the region
 * files possible. If you want full reading access to the world, use the WorldCache class.
 */
class World {
public:
	typedef boost::unordered_set<ChunkPos, hash_function_chunk> ChunkSet;
	typedef boost::unordered_set<RegionPos, hash_function_region> RegionSet;
	typedef boost::unordered_map<RegionPos, std::string, hash_function_region> RegionMap;

	/**
	 * Constructor. You should specify a world directory and you can specify a dimension
	 * of the world (Nether, Overworld per default, End). Mapcrafter will automagically
	 * try to find the right region directory.
	 */
	World(const std::string& world_dir, Dimension dimension, const std::string& cache_dir);
	~World();

	/**
	 * Returns the directory of the world.
	 */
	const fs::path& getWorldDir() const;

	/**
	 * Returns the region directory of the world.
	 */
	const fs::path& getRegionDir() const;

	/**
	 * Returns the cache directory of the world.
	 */
	const fs::path& getCacheDir() const;

	/**
	 * Returns the used dimension of the world.
	 */
	Dimension getDimension() const;

	/**
	 * Returns/Sets the boundaries of the world. You also have to set this before
	 * loading the world.
	 */
	WorldCrop getWorldCrop() const;
	void setWorldCrop(const WorldCrop& world_crop);

	/**
	 * Loads a world from the specified directory. Returns false if the world- or region
	 * directory does not exist.
	 */
	bool load();

	/**
	 * Returns the count of available region files.
	 */
	int getAvailableRegionCount() const;

	/**
	 * Returns the positions of all available regions.
	 */
	const World::RegionSet& getAvailableRegions() const;

	/**
	 * Returns whether a specific region exists.
	 */
	bool hasRegion(const RegionPos& pos) const;

	/**
	 * Returns the path of a region file. Returns an empty path if the region does
	 * not exist.
	 */
	fs::path getRegionPath(const RegionPos& pos) const;

	/**
	 * Creates the Region-object for a specific region and assigns the supplied reference
	 * 'region' to it. Returns false if the region does not exist.
	 */
	bool getRegion(const RegionPos& pos, RegionFile& region) const;

	/**
	 * Returns the Minecraft version ID the world is running with. Returns -1 if no
	 * level.dat or the specific tag can be found.
	 *
	 * The ID is the value of tag "Id" in tag "Version" of the level.dat,
	 * like example below:
	 *
	 * TAG_Compound("Version"): 3 entries
	 * {
	 *   TAG_Int("Id"): 1628 <--- This number
	 *   TAG_String("Name"): 1.13.1
	 *   TAG_Byte("Snapshot"): 0
	 * }
	 */
	int getMinecraftVersion() const;

private:
	// world directory, region directory
	fs::path world_dir, region_dir, cache_dir;
	// used dimension of the world
	Dimension dimension;

	// boundaries of the world
	WorldCrop world_crop;

	// (hash-) set containing positions of available region files
	RegionSet available_regions;
	// (hash-) map containing positions of available region files and their file paths
	RegionMap region_files;
	// (hash-) set containing positions of available chunks
	ChunkSet available_chunks;

	/**
	 * Scans a directory for Anvil *.mca region files and adds them to the available
	 * region files. Returns false if the directory does not exist.
	 */
	bool readRegions(const fs::path& region_dir);
};

}
}

#endif /* WORLD_H_ */
