/*
 * nn_model_metadata.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "schema/base.h"
#include <stdexcept>
#include <string>

namespace MMAI::BAI
{
// Both schema versions advertise their trained battle side via ONNX custom
// metadata. Do not silently accept partial strings (std::stoi("0broken"))
// or unknown values as valid roles. Reject before the first server action.
inline Schema::Side parseNNModelSide(const std::string & value)
{
	std::size_t consumed = 0;
	int side = -1;
	try
	{
		side = std::stoi(value, &consumed);
	}
	catch(const std::exception &)
	{
		throw std::runtime_error("MMAI: invalid ONNX model side metadata: not an integer");
	}

	if(consumed != value.size() ||
		(side != static_cast<int>(Schema::Side::LEFT) &&
		 side != static_cast<int>(Schema::Side::RIGHT) &&
		 side != static_cast<int>(Schema::Side::BOTH)))
		throw std::runtime_error("MMAI: invalid ONNX model side metadata: unsupported side");

	return static_cast<Schema::Side>(side);
}
}
