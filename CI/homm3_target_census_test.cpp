// Source-input differential tests, not native engine or retail-binary parity.
#include "../AI/Integration/Ports/HoMM3TargetCensus.h"
#include "../test/aiIntegration/homm3TargetCensus/PinnedTargetCensus.h"

#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
using namespace AIIntegration::Ports::HoMM3;
uint64_t cases = 0;
void require(bool passed, const char * message)
{
	if(!passed) throw std::runtime_error(message);
}
void compare(int32_t speed, bool shooting, const std::vector<SourceTargetCandidate> & candidates)
{
	const auto actual = reduceSourceTargetCensus(speed, shooting, candidates);
	require(actual.has_value(), "Known source input unexpectedly unavailable");
	PinnedTargetCensus::army actor;
	actor.speed = speed;
	actor.shooting = shooting;
	std::vector<PinnedTargetCensus::army> enemies(candidates.size());
	for(size_t i = 0; i < candidates.size(); ++i)
	{
		enemies[i].index = candidates[i].rosterIndex;
		actor.considerAttack(&enemies[i], *candidates[i].exchangeValue, *candidates[i].attackDistance);
	}
	require(actual->possibleTargets == static_cast<uint32_t>(actor.AI_possible_targets), "Possible target mask differs");
	require(actual->target.has_value() == (actor.AI_target != nullptr), "Target presence differs");
	if(actor.AI_target)
	{
		require(actual->target->rosterIndex == actor.AI_target->index, "Target roster identity differs");
		require(actual->target->exchangeValue == actor.AI_target_value, "Stored exchange value differs");
		require(actual->target->attackDistance == actor.AI_target_distance, "Stored attack distance differs");
		require(actual->target->attackTime == actor.getAITargetTime(), "Final target time differs");
	}
	++cases;
}
void unavailable(int32_t speed, const std::optional<std::vector<SourceTargetCandidate>> & candidates)
{
	require(!reduceSourceTargetCensus(speed, false, candidates), "Unknown or invalid census became a known answer");
	++cases;
}
}
int main()
{
	try
	{
		for(const bool shooting : {false, true})
			for(const int speed : {0, 1, 2, 7, 20})
			{
				compare(speed, shooting, {});
				for(const int distanceA : {0, 1, 2, 6, 7, 8, 14, 21, 127})
					for(const int distanceB : {0, 1, 2, 6, 7, 8, 14, 21, 127})
						for(const int valueA : {INT32_MIN, -100, -1, 0, 1, 100, INT32_MAX})
							for(const int valueB : {INT32_MIN, -100, -1, 0, 1, 100, INT32_MAX})
							{
								compare(speed, shooting, {{2, valueA, distanceA}, {0, valueB, distanceB}});
								compare(speed, shooting, {{0, valueB, distanceB}, {2, valueA, distanceA}});
							}
			}
		std::mt19937 random(0xC3A515);
		for(int trial = 0; trial < 100000; ++trial)
		{
			std::vector<SourceTargetCandidate> candidates;
			const int count = random() % 22;
			for(int i = 0; i < count; ++i)
				candidates.push_back({i, static_cast<int32_t>(random() % 20001) - 10000,
					static_cast<int32_t>(random() % 128)});
			std::shuffle(candidates.begin(), candidates.end(), random);
			compare(random() % 32, (random() & 1) != 0, candidates);
		}
		// findAITargets supplies distance 1 for shooters, so this includes its exact subdomain.
		compare(0, true, {{0, -5, 1}, {1, -5, 1}, {2, -1, 1}});
		unavailable(INT32_MAX, std::vector<SourceTargetCandidate>{{0, INT32_MIN, 0}, {30, INT32_MAX, 1}});
		compare(INT32_MAX - 1, false, {{0, INT32_MIN, 0}, {30, INT32_MAX, 1}});
		compare(INT32_MAX, false, {{0, INT32_MIN, 0}});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, 0, INT32_MAX}});
		compare(1, false, {{0, 0, INT32_MAX - 1}});
		compare(0, false, {{0, 0, INT32_MAX}});
		const auto distant = reduceSourceTargetCensus(3, false,
			std::vector<SourceTargetCandidate>{{0, -100, 9}, {1, 1000, 10}});
		require(distant && distant->target && distant->target->rosterIndex == 0
			&& distant->target->attackTime == 3 && distant->possibleTargets == 0, "Distant negative target lost to richer later target");
		const auto tie = reduceSourceTargetCensus(3, false,
			std::vector<SourceTargetCandidate>{{2, -1, 6}, {0, -1, 5}});
		require(tie && tie->target && tie->target->rosterIndex == 2, "Tie did not preserve first roster candidate");
		unavailable(1, std::nullopt);
		unavailable(-1, std::vector<SourceTargetCandidate>{});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, std::nullopt, 1}});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, 2, std::nullopt}});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, 5, 1}, {1, std::nullopt, 127}});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, 1, -1}});
		unavailable(1, std::vector<SourceTargetCandidate>{{-1, 1, 1}});
		unavailable(1, std::vector<SourceTargetCandidate>{{31, 1, 1}});
		unavailable(1, std::vector<SourceTargetCandidate>{{0, 1, 1}, {0, 2, 1}});
		unavailable(2, std::vector<SourceTargetCandidate>{{0, 1, INT32_MAX}});
		unavailable(INT32_MAX, std::vector<SourceTargetCandidate>{{0, 1, 2}});
		std::cout << "homm3_target_census: " << cases << " differential/contract cases passed\n";
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
