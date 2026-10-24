/* GPL-2.0-or-later; see license.txt. Experimental, not production-wired. */
#include "../../StdInc.h"
#include "PlanningRiskTable.h"
#include "../../../../lib/callback/CGameInfoCallback.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../../lib/mapObjects/CGCreature.h"
#include "../../../../lib/mapping/TerrainTile.h"

namespace NK2AI::experimental
{
std::vector<PlanningRiskCandidate> selectRiskCandidates(std::vector<PlanningRiskCandidate> candidates, uint64_t turn)
{
	std::sort(candidates.begin(), candidates.end(), [](const auto & a, const auto & b)
	{
		return std::tie(a.hero, a.distance, a.target) < std::tie(b.hero, b.distance, b.target);
	});
	std::map<ObjectInstanceID, std::vector<PlanningRiskCandidate>> byHero;
	std::set<std::pair<ObjectInstanceID, ObjectInstanceID>> seen;
	for(const auto & candidate : candidates)
		if(seen.emplace(candidate.hero, candidate.target).second) byHero[candidate.hero].push_back(candidate);
	std::vector<std::vector<PlanningRiskCandidate>> groups;
	for(auto & item : byHero) groups.push_back(std::move(item.second));
	std::vector<PlanningRiskCandidate> result;
	if(groups.empty()) return result;
	for(size_t rank = 0; result.size() < PlanningRiskTable::MAX_REQUESTS; ++rank)
	{
		bool appended = false;
		for(size_t offset = 0; offset < groups.size() && result.size() < PlanningRiskTable::MAX_REQUESTS; ++offset)
		{
			const auto & group = groups[(turn % groups.size() + offset) % groups.size()];
			if(rank < group.size())
			{
				result.push_back(group[rank]);
				appended = true;
			}
		}
		if(!appended) break;
	}
	return result;
}

std::optional<CapturedRiskRequest> captureNeutralRisk(const CGameInfoCallback & visibleInfo,
	const CGHeroInstance & hero, const CGCreature & target, uint64_t epoch, std::string * unknownReason)
{
	if(unknownReason) *unknownReason = "none";
	auto unknown = [unknownReason](const char * reason) -> std::optional<CapturedRiskRequest>
	{
		if(unknownReason) *unknownReason = reason;
		return std::nullopt;
	};
	if(!visibleInfo.getPlayerID() || *visibleInfo.getPlayerID() != hero.getOwner()
		|| visibleInfo.getObj(hero.id, false) != &hero || visibleInfo.getObj(target.id, false) != &target
		|| target.ID != Obj::MONSTER || target.getOwner() != PlayerColor::NEUTRAL)
		return unknown("visibility, owner or ordinary-object eligibility");
	const auto tile = target.visitablePos();
	const auto * terrain = visibleInfo.getTile(tile, false);
	if(!terrain) return unknown("unknown destination terrain");
	const auto fields = visibleInfo.getBattlefieldCandidates(tile);
	if(fields.empty()) return unknown("unknown battlefield context");
	if(fields.size() > PlanningRiskTable::MAX_VARIANTS) return unknown("battlefield variant budget");
	CapturedRiskRequest request;
	request.key.epoch = epoch;
	request.key.hero = hero.id;
	request.key.target = target.id;
	const auto layout = BattleLayout::createDefaultLayout(visibleInfo, &hero, &target);
	for(const auto field : fields)
	{
		PlanningBattleContext context{tile, terrain->terrainType, field, layout};
		auto snapshot = PlanningBattleSnapshot::createNeutralEncounter(visibleInfo, hero, target, context, unknownReason);
		if(!snapshot) return std::nullopt;
		const auto & descriptor = snapshot->capturedSourceState();
		request.key.capturedState += std::to_string(descriptor.size()) + ":" + descriptor;
		request.unmodelledOwnMagic |= snapshot->hasUnmodelledOwnMagic();
		const auto physics = snapshot->physicsState();
		request.physicsState += std::to_string(physics.size()) + ":" + physics;
		request.variants.push_back(std::move(snapshot));
	}
	return request;
}

std::shared_ptr<const PlanningRiskTable> PlanningRiskTable::build(std::vector<CapturedRiskRequest> requests,
	uint64_t turn, unsigned remainingTurnScenarios, std::shared_ptr<const PlanningRiskMemo> previousMemo)
{
	auto table = std::shared_ptr<PlanningRiskTable>(new PlanningRiskTable());
	auto nextMemo = std::make_shared<PlanningRiskMemo>();
	nextMemo->turn = turn;
	if(previousMemo && previousMemo->turn == turn) *nextMemo = *previousMemo;
	remainingTurnScenarios = std::min(remainingTurnScenarios,
		MAX_SCENARIOS_PER_TURN - std::min(nextMemo->consumedScenarios, MAX_SCENARIOS_PER_TURN));
	// Selection cannot depend on which pathfinding worker wins a race. Capture
	// order and input order do not alter the bounded lexicographic admission.
	std::sort(requests.begin(), requests.end(), [](const auto & a, const auto & b) { return a.key < b.key; });
	std::map<PlanningRiskKey, size_t> keyCounts;
	for(const auto & request : requests) ++keyCounts[request.key];
	constexpr std::array<int, SAMPLES_PER_VARIANT> seeds{17041, 17047, 17053};
	for(auto & request : requests)
	{
		if(table->entries.size() >= MAX_REQUESTS) break;
		if(request.key.capturedState.empty() || keyCounts.at(request.key) != 1) continue;
		if(!request.physicsState.empty())
		{
			const auto cached = nextMemo->entries.find(request.physicsState);
			if(cached != nextMemo->entries.end())
			{
				auto reused = cached->second;
				reused.unmodelledOwnMagic = request.unmodelledOwnMagic;
				reused.reused = true;
				reused.elapsedMicroseconds = 0;
				table->entries.emplace(std::move(request.key), std::move(reused));
				continue;
			}
		}
		PlanningRiskRecord record;
		record.unmodelledOwnMagic = request.unmodelledOwnMagic;
		const auto start = std::chrono::steady_clock::now();
		record.allScenariosFinished = !request.variants.empty() && request.variants.size() <= MAX_VARIANTS
			&& request.variants.size() * SAMPLES_PER_VARIANT <= remainingTurnScenarios - table->computedScenarios;
		if(record.allScenariosFinished)
		{
			for(const auto & variant : request.variants)
			{
				if(!variant)
				{
					record.allScenariosFinished = false;
					break;
				}
				for(int seed : seeds)
				{
					++table->computedScenarios;
					record.scenarios.push_back(runSampledBattle(variant->battle(), variant->frozenGameInfo(), seed));
					record.allScenariosFinished &= record.scenarios.back().status == SampledBattleStatus::FINISHED;
				}
			}
		}
		record.elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count();
		if(!request.physicsState.empty() && !record.scenarios.empty() && nextMemo->entries.size() < MAX_MEMO_ENTRIES)
			nextMemo->entries.emplace(std::move(request.physicsState), record);
		table->entries.emplace(std::move(request.key), std::move(record));
	}
	nextMemo->consumedScenarios += table->computedScenarios;
	table->memoState = std::move(nextMemo);
	return table;
}

const PlanningRiskRecord * PlanningRiskTable::find(const PlanningRiskKey & exactCurrentKey,
	const PlanningRiskRoute & route) const
{
	// Estimated aggregate casualties do not identify a surviving composition.
	// Never forecast a destination with the original full army after combat,
	// exchanges or movement spells. Their legacy risk handling remains intact.
	if(!route.usesCurrentHeroArmy || route.earlierArmyLoss || route.spentMana)
		return nullptr;
	const auto found = entries.find(exactCurrentKey);
	return found == entries.end() ? nullptr : &found->second;
}
}
