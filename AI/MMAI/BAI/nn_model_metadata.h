/*
 * nn_model_metadata.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "schema/base.h"
#include <charconv>
#include <stdexcept>
#include <string>

namespace MMAI::BAI
{
// ONNX version and side are exact decimal metadata fields. std::stoi()
// accepts whitespace, signs and suffixes, so use from_chars for a fully
// consumed, nonnegative decimal value before constructing any NN policy.
inline int parseNNMetadataDecimal(const std::string & value, const char * field)
{
	int parsed = -1;
	if(value.empty())
		throw std::runtime_error(std::string("MMAI: invalid ONNX ") + field + " metadata: empty");

	const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
	if(error != std::errc{} || end != value.data() + value.size() || parsed < 0)
		throw std::runtime_error(std::string("MMAI: invalid ONNX ") + field + " metadata: expected unsigned decimal integer");

	return parsed;
}

inline int parseNNModelVersion(const std::string & value)
{
	const int version = parseNNMetadataDecimal(value, "version");
	if(version != 13 && version != 15)
		throw std::runtime_error("MMAI: unsupported ONNX model schema version: " + std::to_string(version));
	return version;
}

inline Schema::Side parseNNModelSide(const std::string & value)
{
	const int side = parseNNMetadataDecimal(value, "side");
	if(side != static_cast<int>(Schema::Side::LEFT) &&
	   side != static_cast<int>(Schema::Side::RIGHT) &&
	   side != static_cast<int>(Schema::Side::BOTH))
		throw std::runtime_error("MMAI: invalid ONNX model side metadata: unsupported side");

	return static_cast<Schema::Side>(side);
}
}
