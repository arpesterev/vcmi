/*
 * model_api.h, part of VCMI engine
 *
 * Small public interface to load an MMAI neural policy without exposing
 * ONNX Runtime internals to battle integration tests and other callers.
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "AI/MMAI/schema/base.h"
#include <cstdint>
#include <memory>
#include <string>

namespace MMAI::BAI
{
std::shared_ptr<MMAI::Schema::IModel> CreateNNModel(
	const std::string & path, float temperature = 1.0f, uint64_t seed = 0);
}
