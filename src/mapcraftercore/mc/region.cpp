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

#include "region.h"

#include "blockstate.h"

#include <cstdlib>
#include <fstream>
#include <sys/param.h>
#include <boost/endian/buffers.hpp>
#include <boost/endian/conversion.hpp>

namespace mapcrafter {
namespace mc {

RegionFile::RegionFile()
{
}

RegionFile::RegionFile(const std::string& filename)
	: filename(filename) {
	regionpos = RegionPos::byFilename(filename);
	// Adjust the Y to be the lowest provided by the file
	// !! Disabled for now as it's a fixed value (so far ...)
	// if (read()) {
	// 	int yfile = lowestY();
	// 	regionpos.y = std::min(regionpos.y,yfile);
	// }
}

RegionFile::~RegionFile() {
}

bool RegionFile::readHeaders(std::ifstream& file, uint32_t chunk_offsets[1024]) {
	if (!file)
		return false;

	containing_chunks.clear();
	for (int i = 0; i < 1024; i++) {
		chunk_offsets[i] = 0;
		chunk_exists[i] = false;
		chunk_timestamps[i] = 0;
		chunk_data_compression[i] = 0;
	}

	file.seekg(0, std::ios::end);
	size_t filesize = file.tellg();
	file.seekg(0, std::ios::beg);
	uint32_t header[2 * 32 * 32];

	// make sure the region file has a header
	if (filesize == 0) {
		// Simply ignore the file if empty. Some chunk management tools can empty all chunks but doesn't erase the file, so simply ignore it
		return false;
	}
	if (filesize < sizeof(header)) {
		LOG(ERROR) << "Corrupt region '" << filename << "': Header is too short.";
		return false;
	}

	// Make only one IO operation to parse the header
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(&header), sizeof(header));

	//convert the headers from big-endian
	for (uint32_t& header_entry : header)
		boost::endian::big_to_native_inplace(header_entry);

	for (int z = 0; z < 32; z++) {
		for (int x = 0; x < 32; x++) {
			uint32_t tmp = header[(x + z * 32)];
			if (tmp == 0)
				continue;
			uint32_t offset = (tmp >> 8) * 4096;
			if (filesize < offset + 5) {
				LOG(ERROR) << "Corrupt region '" << filename << "': Invalid offset of chunk "
						<< x << ":" << z << ".";
				return false;
			}

			uint32_t timestamp = header[1024 + (x + z * 32)];

			// get the original position of the chunk
			ChunkPos chunkpos(x + regionpos.x * 32, z + regionpos.z * 32);
			// check if this chunk is not cropped
			if (!world_crop.isChunkContained(chunkpos))
				continue;

			chunk_exists[z * 32 + x] = true;
			containing_chunks.insert(chunkpos);

			// set offset and timestamp of this chunk
			// now with the original coordinates again
			chunk_offsets[z * 32 + x] = offset;
			chunk_timestamps[z * 32 + x] = timestamp;
		}
	}
	return true;
}

size_t RegionFile::getChunkIndex(const mc::ChunkPos& chunkpos) const {
	return chunkpos.getLocalZ() * 32 + chunkpos.getLocalX();
}

void RegionFile::setWorldCrop(const WorldCrop& world_crop) {
	this->world_crop = world_crop;
}

bool RegionFile::read() {
	std::ifstream file(filename.c_str(), std::ios_base::binary);
	uint32_t chunk_offsets[1024];
	if (!readHeaders(file, chunk_offsets))
		return false;
	file.seekg(0, std::ios::end);
	size_t filesize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> regiondata(filesize);
	file.read(reinterpret_cast<char*>(&regiondata[0]), filesize);

	for (int i = 0; i < 1024; i++) {
		// get the offsets, where the chunk data starts
		int offset = chunk_offsets[i];
		if (offset == 0)
			continue;

		// i = x + z * 32
		int x = i % 32;
		int z = (i - x) / 32;

		// get data size and compression type
		uint32_t size = boost::endian::load_big_u32(reinterpret_cast<unsigned char*>(&regiondata[offset]));
		if (size == 0) {
			LOG(ERROR)  << "Corrupt region '" << filename << "': Size of chunk "
				<< x << ":" << z << " is zero.";
			return false;
		}
		uint8_t compression = regiondata[offset + 4];
		if (filesize < offset + 4 + size) {
			LOG(ERROR) << "Corrupt region '" << filename << "': Invalid size of chunk "
				<< x << ":" << z << ".";
			return false;
		}

		chunk_data_compression[i] = compression;
		chunk_data[i].assign(&regiondata[offset + 4 + 1], &regiondata[offset + 4 + size]);
	}

	return true;
}

bool RegionFile::readOnlyHeaders() {
	std::ifstream file(filename.c_str(), std::ios_base::binary);
	uint32_t chunk_offsets[1024];
	return readHeaders(file, chunk_offsets);
}

bool RegionFile::write(std::string filename) const {
	if (filename.empty())
		filename = this->filename;
	if (filename.empty())
		throw std::invalid_argument("You have to specify a filename!");

	uint32_t offsets[1024]{}; //zero-initialize

	std::string out_data;

	// write chunk data to a temporary string stream
	unsigned position = 8192;
	for (int i = 0; i < 1024; i++) {
		if (chunk_data[i].size() == 0)
			continue;
		// pad every chunk data with zeros to the next n*4096 bytes
		if (position % 4096 != 0) {
			unsigned append = 4096 - position % 4096;
			position += append;
			static const char ZERO_BYTES[4096]{};
			out_data.append(ZERO_BYTES, append);
		}

		// calculate the offset, the chunk starts at 4096*offset bytes
		offsets[i] = position / 4096;

		// get chunk data, size and compression type
		const std::vector<uint8_t>& data = chunk_data[i];
		uint32_t size = boost::endian::native_to_big<uint32_t>(data.size() + 1);
		uint8_t compression = chunk_data_compression[i];

		// append everything to the data
		out_data.append(reinterpret_cast<char*>(&size), 4);
		out_data.append(reinterpret_cast<char*>(&compression), 1);
		out_data.append(reinterpret_cast<const char*>(&data[0]), data.size());
		position += data.size() + 5;
	}

	// create the header with offsets and timestamps
	boost::endian::big_uint32_buf_t out_header[1024 * 2];
	for (int i = 0; i < 1024; i++) {
		//TODO: this is wrong, it should also contain the chunk size in sectors
		out_header[i] = (offsets[i] << 8);
	}

	for (int i = 0; i < 1024; i++) {
		out_header[1024 + i] = chunk_timestamps[i];
	}

	// write complete region file
	std::ofstream out(filename, std::ios::binary);
	if (!out)
		return false;
	out.write(reinterpret_cast<char*>(&out_header), sizeof(out_header)) << out_data;
	out.close();
	return !out.fail();
}

const std::string& RegionFile::getFilename() const {
	return filename;
}

const RegionPos& RegionFile::getPos() const {
	return regionpos;
}

int RegionFile::getContainingChunksCount() const {
	return containing_chunks.size();
}

const RegionFile::ChunkMap& RegionFile::getContainingChunks() const {
	return containing_chunks;
}

bool RegionFile::hasChunk(const ChunkPos& chunk) const {
	return chunk_exists[getChunkIndex(chunk)];
}

uint32_t RegionFile::getChunkTimestamp(const ChunkPos& chunk) const {
	return chunk_timestamps[getChunkIndex(chunk)];
}

void RegionFile::setChunkTimestamp(const ChunkPos& chunk, uint32_t timestamp) {
	chunk_timestamps[getChunkIndex(chunk)] = timestamp;
}

const std::vector<uint8_t>& RegionFile::getChunkData(const ChunkPos& chunk) const {
	return chunk_data[getChunkIndex(chunk)];
}

uint8_t RegionFile::getChunkDataCompression(const ChunkPos& chunk) const {
	return chunk_data_compression[getChunkIndex(chunk)];
}

void RegionFile::setChunkData(const ChunkPos& chunk, const std::vector<uint8_t>& data,
		uint8_t compression) {
	size_t index = getChunkIndex(chunk);
	chunk_data[index] = data;
	chunk_data_compression[index] = compression;

	if (data.size() == 0) {
		chunk_exists[index] = false;
		containing_chunks.erase(chunk);
	} else {
		chunk_exists[index] = true;
		containing_chunks.insert(chunk);
	}
}

/**
 * This method tries to load a chunk from the region data and returns a status.
 */
int RegionFile::loadChunk(const ChunkPos& pos, BlockStateRegistry& block_registry, Chunk& chunk) {
	int index = getChunkIndex(pos);

	// check if the chunk exists
	if (chunk_data[index].size() == 0)
		return CHUNK_DOES_NOT_EXIST;

	// get compression type and size of the data
	uint8_t compression = chunk_data_compression[index];
	nbt::Compression comp = nbt::Compression::NO_COMPRESSION;
	if (compression == 1)
		comp = nbt::Compression::GZIP;
	else if (compression == 2)
		comp = nbt::Compression::ZLIB;
	size_t size = chunk_data[index].size();

	chunk.setWorldCrop(world_crop);
	// try to load the chunk
	try {
		if (!chunk.readNBT(block_registry, reinterpret_cast<char*>(&chunk_data[index][0]), size, comp))
			return CHUNK_DATA_INVALID;
	} catch (const nbt::NBTError& err) {
		LOG(ERROR) << "Unable to read chunk at " << pos << ": " << err.what();
		return CHUNK_NBT_ERROR;
	}
	return CHUNK_OK;
}


/**
 * This method tries to read out the lowest chunksection Y value.
 * TODO: Actually not used yet ...
 */
int RegionFile::lowestY() {
	auto chunks = getContainingChunks();
	int y = CHUNK_HIGHEST;
	for (auto chunk_it = chunks.begin(); chunk_it != chunks.end(); ++chunk_it) {
		ChunkPos pos = *chunk_it;
		int index = getChunkIndex(pos);
		if (chunk_data[index].size() == 0)
			continue;

		// get compression type and size of the data
		uint8_t compression = chunk_data_compression[index];
		nbt::Compression comp = nbt::Compression::NO_COMPRESSION;
		if (compression == 1)
			comp = nbt::Compression::GZIP;
		else if (compression == 2)
			comp = nbt::Compression::ZLIB;

		Chunk chunk;
		size_t size = chunk_data[index].size();
		nbt::NBTFile nbt;

		try {
			nbt.readNBT(reinterpret_cast<char*>(&chunk_data[index][0]), size, comp);
			if (!nbt.hasTag<nbt::TagInt>("yPos")) {
				continue;
			}
			int ychunk = nbt.findTag<nbt::TagInt>("yPos").payload;
			y = std::min(y, ychunk);
		} catch (const nbt::NBTError& err) {
			LOG(ERROR) << "Unable to read chunk at " << pos << ": " << err.what();
			continue;
		}

	}
	return y;
}
}
}
