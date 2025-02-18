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

#include "nbt.h"

#include <fstream>
#include <boost/endian/conversion.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zlib.hpp>

namespace mapcrafter {
namespace mc {
namespace nbt {

namespace nbtstream {
template <>
int8_t read<int8_t>(istream& stream) {
	int8_t value;
	stream.read(reinterpret_cast<char*>(&value), sizeof(value));
	return value;
}

template <>
int16_t read<int16_t>(istream& stream) {
	int16_t value;
	stream.read(reinterpret_cast<char*>(&value), sizeof(value));
	return boost::endian::big_to_native<int16_t>(value);
}

template <>
int32_t read<int32_t>(istream& stream) {
	int32_t value;
	stream.read(reinterpret_cast<char*>(&value), sizeof(value));
	return boost::endian::big_to_native<int32_t>(value);
}

template <>
int64_t read<int64_t>(istream& stream) {
	int64_t value;
	stream.read(reinterpret_cast<char*>(&value), sizeof(value));
	return boost::endian::big_to_native<int64_t>(value);
}

template <>
float read<float>(istream& stream) {
	return util::bit_cast<float>(read<int32_t>(stream));
}

template <>
double read<double>(istream& stream) {
	return util::bit_cast<double>(read<int64_t>(stream));
}

template <>
std::string read<std::string>(istream& stream) {
	int16_t length = read<int16_t>(stream);
	std::string value;
	value.resize(length);
	stream.read(&value[0], length);
	return value;
}

template<>
void read_array<int8_t>(istream& stream, int8_t* dst, size_t n) {
	stream.read(reinterpret_cast<char*>(dst), n * sizeof(int8_t));
}

template<>
void read_array<int32_t>(istream& stream, int32_t* dst, size_t n) {
	//read all the values in one shot
	stream.read(reinterpret_cast<char*>(dst), n * sizeof(int32_t));
	
	//convert the values from big-endian to native
	for (size_t i = 0; i < n; i++)
		boost::endian::big_to_native_inplace<int32_t>(dst[i]);
}

template<>
void read_array<int64_t>(istream& stream, int64_t* dst, size_t n) {
	//read all the values in one shot
	stream.read(reinterpret_cast<char*>(dst), n * sizeof(int64_t));
	
	//convert the values from big-endian to native
	for (size_t i = 0; i < n; i++)
		boost::endian::big_to_native_inplace<int64_t>(dst[i]);
}

template <>
void write<int8_t>(std::ostream& stream, int8_t value) {
	stream.write(reinterpret_cast<char*>(&value), sizeof(value));
}

template <>
void write<int16_t>(std::ostream& stream, int16_t value) {
	int16_t tmp = boost::endian::native_to_big<int16_t>(value);
	stream.write(reinterpret_cast<char*>(&tmp), sizeof(value));
}

template <>
void write<int32_t>(std::ostream& stream, int32_t value) {
	int32_t tmp = boost::endian::native_to_big<int32_t>(value);
	stream.write(reinterpret_cast<char*>(&tmp), sizeof(value));
}

template <>
void write<int64_t>(std::ostream& stream, int64_t value) {
	int64_t tmp = boost::endian::native_to_big<int64_t>(value);
	stream.write(reinterpret_cast<char*>(&tmp), sizeof(value));
}

template <>
void write<float>(std::ostream& stream, float value) {
	write<int32_t>(stream, util::bit_cast<int32_t>(value));
}

template <>
void write<double>(std::ostream& stream, double value) {
	write<int64_t>(stream, util::bit_cast<int64_t>(value));
}

template <>
void write<std::string>(std::ostream& stream, std::string value) {
	write<int16_t>(stream, value.size());
	stream.write(value.c_str(), value.size());
}
}

Tag::Tag(int8_t type)
	: type(type), named(false), write_type(true) {
}

Tag::~Tag() {
}

int8_t Tag::getType() const {
	return type;
}

bool Tag::isWriteType() const {
	return write_type;
}

void Tag::setWriteType(bool write_type) {
	this->write_type = write_type;
}

bool Tag::isNamed() const {
	return named;
}

void Tag::setNamed(bool named) {
	this->named = named;
}

const std::string& Tag::getName() const {
	return name;
}

void Tag::setName(const std::string& name, bool set_named) {
	if (set_named)
		this->named = true;
	this->name = name;
}

void Tag::setName(std::string&& name, bool set_named) {
	if (set_named)
		this->named = true;
	this->name.assign(std::move(name));
}

void Tag::write(std::ostream& stream) const {
	if (write_type)
		nbtstream::write<int8_t>(stream, type);
	if (named)
		nbtstream::write<std::string>(stream, name);
}

void Tag::dump(std::ostream& stream, const std::string& indendation) const {
}

TagString::TagString(istream& stream)
		: Tag(TAG_TYPE), payload(nbtstream::read<std::string>(stream)) {
}

Tag& TagString::read(istream& stream) {
	payload = nbtstream::read<std::string>(stream);
	return *this;
}

void TagString::write(std::ostream& stream) const {
	Tag::write(stream);
	nbtstream::write<std::string>(stream, payload);
}

void TagString::dump(std::ostream& stream, const std::string& indendation) const {
	dumpTag(stream, indendation, *this);
}

Tag* TagString::clone() const {
	return new TagString(*this);
}

TagList::TagList(int8_t tag_type)
	: Tag(TAG_TYPE), tag_type(tag_type) {
}

TagList::TagList(const TagList& other)
	: Tag(TAG_TYPE) {
	*this = other;
}

TagList::~TagList() {
}

void TagList::operator=(const TagList& other) {
	if (this != &other) {
		name = other.name;
		named = other.named;

		tag_type = other.tag_type;

		payload.clear();
		payload.reserve(other.payload.size());
		for (auto& tag : other.payload)
			payload.emplace_back(tag->clone());
	}
}

Tag& TagList::read(istream& stream) {
	tag_type = nbtstream::read<int8_t>(stream);
	int32_t length = nbtstream::read<int32_t>(stream);
	payload.reserve(length);
	for (int32_t i = 0; i < length; i++) {
		Tag* tag = createTag(tag_type);
		if (tag == nullptr)
			throw NBTError(std::string("Unknown tag type with id ") + util::str(static_cast<int>(tag_type))
						   + ". NBT data stream may be corrupted.");
		tag->read(stream);
		tag->setWriteType(false);
		tag->setNamed(false);
		payload.emplace_back(tag);
	}
	return *this;
}

void TagList::write(std::ostream& stream) const {
	Tag::write(stream);
	nbtstream::write<int8_t>(stream, tag_type);
	nbtstream::write<int32_t>(stream, payload.size());
	for (auto& tag : payload) {
		tag->setWriteType(false);
		tag->setNamed(false);
		tag->write(stream);
	}
}

void TagList::dump(std::ostream& stream, const std::string& indendation) const {
	stream << indendation << "TAG_List";
	if (named)
		stream << "(\"" << name << "\")";
	stream << ": " << payload.size() << " entries of type " << static_cast<int>(tag_type) << std::endl;
	stream << indendation << "{" << std::endl;
	for (auto& tag : payload)
		tag->dump(stream, indendation + "   ");
	stream << indendation << "}" << std::endl;
}

Tag* TagList::clone() const {
	return new TagList(*this);
}

TagCompound::TagCompound(const std::string& name)
	: Tag(TAG_TYPE) {
	setName(name);
}

TagCompound::TagCompound(const TagCompound& other)
	: Tag(TAG_TYPE) {
	*this = other;
}

TagCompound::~TagCompound() {
}

void TagCompound::operator=(const TagCompound& other) {
	if (this != &other) {
		name = other.name;
		named = other.named;

		payload.clear();
		for (auto &tag: other.payload)
			payload.emplace(tag.first, tag.second->clone());
	}
}

Tag& TagCompound::read(istream& stream) {
	while (1) {
		int8_t tag_type = nbtstream::read<int8_t>(stream);
		if (tag_type == TagEnd::TAG_TYPE)
			break;
		std::string name = nbtstream::read<std::string>(stream);
		Tag* tag = createTagAndRead(tag_type, stream);
		if (tag == nullptr)
			throw NBTError(std::string("Unknown tag type with id ") + util::str(static_cast<int>(tag_type))
						   + ". NBT data stream may be corrupted.");
		tag->setName(name);
		tag->setWriteType(true);
		if (!payload.emplace(std::move(name), tag).second)
			throw NBTError("NBT compound tag contains duplicate name!");
	}
	return *this;
}

void TagCompound::write(std::ostream& stream) const {
	Tag::write(stream);
	for (auto& tag : payload) {
		tag.second->setWriteType(true);
		tag.second->setNamed(true);
		tag.second->write(stream);
	}
	nbtstream::write<int8_t>(stream, TagEnd::TAG_TYPE);
}

void TagCompound::dump(std::ostream& stream, const std::string& indendation) const {
	stream << indendation << "TAG_Compound";
	if (named)
		stream << "(\"" << name << "\")";
	stream << ": " << payload.size() << " entries" << std::endl;
	stream << indendation << "{" << std::endl;
	for (auto& tag : payload)
		tag.second->dump(stream, indendation + "   ");
	stream << indendation << "}" << std::endl;
}

Tag* TagCompound::clone() const {
	return new TagCompound(*this);
}

bool TagCompound::hasTag(const std::string& name) const {
	return payload.count(name);
}

Tag& TagCompound::findTag(const std::string& name) {
	return *payload.at(name);
}

const Tag& TagCompound::findTag(const std::string& name) const {
	return *payload.at(name);
}

void TagCompound::addTag(const std::string& name, const Tag& tag) {
	Tag* tag_ptr = tag.clone();
	tag_ptr->setName(name);
	tag_ptr->setWriteType(true);
	payload[name] = TagPtr(tag_ptr);
}

NBTFile::NBTFile() {
}

NBTFile::~NBTFile() {
}

void NBTFile::decompressStream(std::istream& stream, std::stringstream& decompressed,
        Compression compression) {
	if (compression == Compression::NO_COMPRESSION) {
		decompressed << stream.rdbuf();
		return;
	}
	boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
	if (compression == Compression::GZIP) {
		in.push(boost::iostreams::gzip_decompressor());
	} else if (compression == Compression::ZLIB) {
		in.push(boost::iostreams::zlib_decompressor());
	}
	try {
		in.push(stream);
		boost::iostreams::copy(in, decompressed);
	} catch (boost::iostreams::gzip_error &e) {
		throw NBTError(
		        "Error while decompressing gzip data: " + std::string(e.what()) + " ("
		                + util::str(e.error()) + ")");
	} catch (boost::iostreams::zlib_error &e) {
		throw NBTError(
		        "Error while decompressing zlib data: " + std::string(e.what()) + " ("
		                + util::str(e.error()) + ")");
	}
}

void NBTFile::readCompressed(std::istream& stream, Compression compression) {
	std::stringstream decompressed(std::ios::in | std::ios::out | std::ios::binary);
	decompressStream(stream, decompressed, compression);
	int8_t type = ((TagByte&) TagByte().read(decompressed)).payload;
	if (type != TagCompound::TAG_TYPE)
		throw NBTError("First tag is not a tag compound!");
	std::string name = ((TagString&) TagString().read(decompressed)).payload;
	TagCompound::read(decompressed);
	setName(name);
}

void NBTFile::readNBT(std::istream& stream, Compression compression) {
	readCompressed(stream, compression);
}

void NBTFile::readNBT(const char* filename, Compression compression) {
	std::ifstream file(filename, std::ios::binary);
	if (!file)
		throw NBTError(std::string("Unable to open file '") + filename + "'!");
	readCompressed(file, compression);
	file.close();
}

void NBTFile::readNBT(const char* buffer, size_t len, Compression compression) {
	std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
	stream.write(buffer, len);
	readCompressed(stream, compression);
}

void NBTFile::writeNBT(std::ostream& stream, Compression compression) {
	std::stringstream in(std::ios::in | std::ios::out | std::ios::binary);
	boost::iostreams::filtering_streambuf<boost::iostreams::input> out;
	if (compression == Compression::GZIP) {
		out.push(boost::iostreams::gzip_compressor());
	} else if (compression == Compression::ZLIB) {
		out.push(boost::iostreams::zlib_compressor());
	} else {
		write(stream);
		return;
	}
	out.push(in);
	write(in);
	boost::iostreams::copy(out, stream);
}

void NBTFile::writeNBT(const char* filename, Compression compression) {
	std::ofstream file(filename, std::ios::binary);
	if (!file)
		throw NBTError(std::string("Unable to open file '") + filename + "'!");
	writeNBT(file, compression);
	file.close();
}

Tag* createTag(int8_t type) {
	switch (type) {
	case TagByte::TAG_TYPE:
		return new TagByte;
	case TagShort::TAG_TYPE:
		return new TagShort;
	case TagInt::TAG_TYPE:
		return new TagInt;
	case TagLong::TAG_TYPE:
		return new TagLong;
	case TagFloat::TAG_TYPE:
		return new TagFloat;
	case TagDouble::TAG_TYPE:
		return new TagDouble;
	case TagByteArray::TAG_TYPE:
		return new TagByteArray;
	case TagString::TAG_TYPE:
		return new TagString;
	case TagList::TAG_TYPE:
		return new TagList;
	case TagCompound::TAG_TYPE:
		return new TagCompound;
	case TagIntArray::TAG_TYPE:
		return new TagIntArray;
	case TagLongArray::TAG_TYPE:
		return new TagLongArray;
	default:
		return nullptr;
	}
}

Tag* createTagAndRead(int8_t type, istream& stream) {
	switch (type) {
	case TagByte::TAG_TYPE:
		return new TagByte(stream);
	case TagShort::TAG_TYPE:
		return new TagShort(stream);
	case TagInt::TAG_TYPE:
		return new TagInt(stream);
	case TagLong::TAG_TYPE:
		return new TagLong(stream);
	case TagFloat::TAG_TYPE:
		return new TagFloat(stream);
	case TagDouble::TAG_TYPE:
		return new TagDouble(stream);
	case TagByteArray::TAG_TYPE:
		return new TagByteArray(stream);
	case TagString::TAG_TYPE:
		return new TagString(stream);
	case TagList::TAG_TYPE:
		return new TagList(stream);
	case TagCompound::TAG_TYPE:
		return new TagCompound(stream);
	case TagIntArray::TAG_TYPE:
		return new TagIntArray(stream);
	case TagLongArray::TAG_TYPE:
		return new TagLongArray(stream);
	default:
		return nullptr;
	}
}

}
}
}
