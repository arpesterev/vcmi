/*
 * CRandomGenerator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CRandomGenerator.h"

#include <boost/stacktrace.hpp>
#include <charconv>
#include <cstdlib>

namespace
{
template<typename T>
std::string invalidRangeMessage(T lower, T upper)
{
	std::ostringstream out;
	out << "Invalid range provided: " << lower << " ... " << upper << "\nStacktrace:\n" << boost::stacktrace::stacktrace();
	return out.str();
}
}

CRandomGenerator::CRandomGenerator()
{
	logRng->trace("CRandomGenerator constructed");
	resetSeed();
}

CRandomGenerator::CRandomGenerator(int seed)
{
	logRng->trace("CRandomGenerator constructed (%d)", seed);
	setSeed(seed);
}

void CRandomGenerator::setSeed(int seed)
{
	logRng->trace("CRandomGenerator::setSeed (%d)", seed);
	rand.seed(seed);
}

void CRandomGenerator::resetSeed()
{
	logRng->trace("CRandomGenerator::resetSeed");
	// Explicitly opt in for reproducible subprocess integration tests.
	if(const char * value = std::getenv("VCMI_TEST_RNG_SEED"))
	{
		int seed = 0;
		const char * end = value + std::char_traits<char>::length(value);
		const auto parsed = std::from_chars(value, end, seed);
		if(parsed.ec != std::errc{} || parsed.ptr != end)
			throw std::runtime_error("VCMI_TEST_RNG_SEED must be a signed integer");
		setSeed(seed);
		return;
	}
	std::hash<std::thread::id> hasher;
	auto threadIdHash = hasher(std::this_thread::get_id());
	setSeed(static_cast<int>(threadIdHash * std::time(nullptr)));
}

int CRandomGenerator::nextInt(int upper)
{
	logRng->trace("CRandomGenerator::nextInt (%d)", upper);
	return nextInt(0, upper);
}

int64_t CRandomGenerator::nextInt64(int64_t upper)
{
	logRng->trace("CRandomGenerator::nextInt64 (%d)", upper);
	return nextInt64(0, upper);
}

int CRandomGenerator::nextInt(int lower, int upper)
{
	logRng->trace("CRandomGenerator::nextInt64 (%d, %d)", lower, upper);

	if (lower > upper)
		throw std::runtime_error(invalidRangeMessage(lower, upper));

	return TIntDist(lower, upper)(rand);
}

int CRandomGenerator::nextInt()
{
	logRng->trace("CRandomGenerator::nextInt64");
	return TIntDist()(rand);
}

int CRandomGenerator::nextBinomialInt(int coinsCount, double coinChance)
{
	logRng->trace("CRandomGenerator::nextBinomialInt (%d, %f)", coinsCount, coinChance);
	std::binomial_distribution<> distribution(coinsCount, coinChance);
	return distribution(rand);
}

int64_t CRandomGenerator::nextInt64(int64_t lower, int64_t upper)
{
	logRng->trace("CRandomGenerator::nextInt64 (%d, %d)", lower, upper);
	if (lower > upper)
		throw std::runtime_error(invalidRangeMessage(lower, upper));

	return TInt64Dist(lower, upper)(rand);
}

double CRandomGenerator::nextDouble(double upper)
{
	logRng->trace("CRandomGenerator::nextDouble (%f)", upper);
	return nextDouble(0, upper);
}

double CRandomGenerator::nextDouble(double lower, double upper)
{
	logRng->trace("CRandomGenerator::nextDouble (%f, %f)", lower, upper);
	if(lower > upper)
		throw std::runtime_error(invalidRangeMessage(lower, upper));

	return TRealDist(lower, upper)(rand);
}

CRandomGenerator & CRandomGenerator::getDefault()
{
	static thread_local CRandomGenerator defaultRand;
	return defaultRand;
}
