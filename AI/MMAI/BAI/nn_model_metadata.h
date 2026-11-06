/*
 * nn_model_metadata.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "schema/base.h"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <set>
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

// V15's node_order and edge_order come from dictionaries of schema types in
// vcmi-gym's exporter: each type occurs exactly once. Duplicating a type can
// cause the ONNX model to consume a repeated observation or edge relation
// under the wrong positional input. Reject that metadata on model load.
inline void requireUniqueNNMetadataType(std::set<int> & seen, int type, const char * field)
{
	if(!seen.insert(type).second)
		throw std::runtime_error(std::string("MMAI: duplicate ONNX ") + field
			+ " type: " + std::to_string(type));
}

// V13's exporter (vcmi-gym/rl/algos/mppo_dna_gnn/export.py) defines
// positional ONNX input/output names. A count-only check lets a different
// graph interface initialize under the MMAI V13 identity, potentially
// binding the wrong observation or action head. Refuse it at model load.
inline void requireNNV13TensorName(const char * actual, const char * expected, std::size_t index, const char * kind)
{
	if(actual && std::string(actual) == expected)
		return;
	throw std::runtime_error("MMAI V13: unexpected ONNX " + std::string(kind) + " name at index "
		+ std::to_string(index) + ": expected '" + expected + "', got '"
		+ (actual ? actual : "<null>") + "'");
}

// Reject an incompatible ONNX interface when loading weights, before a real
// VCMI battle reaches inference. A -1 expected dimension is dynamic; ONNX
// symbolic axes also report -1 and may match fixed input dimensions.
// Known positive dimensions must agree, and every tensor must have the
// correct rank and element type. This does not validate learned weights.
inline void requireNNModelTensorSignature(const std::string & name, int actualType, int expectedType,
                                          const std::vector<int64_t> & actualShape,
                                          std::initializer_list<int64_t> expectedShape)
{
	if(actualType != expectedType)
		throw std::runtime_error("MMAI: ONNX tensor '" + name + "' has unexpected element type: "
			+ std::to_string(actualType) + ", expected " + std::to_string(expectedType));
	if(actualShape.size() != expectedShape.size())
		throw std::runtime_error("MMAI: ONNX tensor '" + name + "' has unexpected rank: "
			+ std::to_string(actualShape.size()) + ", expected " + std::to_string(expectedShape.size()));

	std::size_t i = 0;
	for(const auto expected : expectedShape)
	{
		const auto actual = actualShape[i];
		if(actual < -1 || (expected >= 0 && actual >= 0 && actual != expected))
			throw std::runtime_error("MMAI: ONNX tensor '" + name + "' has incompatible dimension "
				+ std::to_string(i) + ": " + std::to_string(actual)
				+ ", expected " + std::to_string(expected));
		++i;
	}
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
