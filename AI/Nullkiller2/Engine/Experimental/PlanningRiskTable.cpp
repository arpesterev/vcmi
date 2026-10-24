/* GPL-2.0-or-later; see license.txt. Experimental, not production-wired. */
#include "../../StdInc.h"
#include "../../../../lib/callback/PlanningBattleTerrain.h"
#include "PlanningRiskTable.h"
#include <future>
#include <atomic>
#include <mutex>
#include <limits>
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
	if(!hero.appearance || !target.appearance) return unknown("missing army appearance");
	if(!visibleInfo.getPlayerID() || *visibleInfo.getPlayerID() != hero.getOwner()
		|| visibleInfo.getObj(hero.id, false) != &hero || visibleInfo.getObj(target.id, false) != &target
		|| target.ID != Obj::MONSTER || typeid(target) != typeid(CGCreature)
		|| (target.getOwner() != PlayerColor::NEUTRAL && target.getOwner() != PlayerColor::UNFLAGGABLE))
		return unknown("visibility, owner or ordinary-object eligibility");
	const auto tile = target.visitablePos();
	const auto terrain = getNeutralBattleTerrain(visibleInfo, tile);
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
		PlanningBattleContext context{tile, *terrain, field, layout};
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
				reused.contextualLossTolerance = request.contextualLossTolerance;
				reused.reused = true;
				reused.elapsedMicroseconds = 0;
				table->entries.emplace(std::move(request.key), std::move(reused));
				continue;
			}
		}
		PlanningRiskRecord record;
		record.unmodelledOwnMagic = request.unmodelledOwnMagic;
		record.contextualLossTolerance = request.contextualLossTolerance;
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


RiskAssessment PlanningRiskRecord::assess(double tolerance, bool permitDefeatRecommendation) const
{
	RiskAssessment combined;
	if(!decisionEvidence || !allScenariosFinished || variantScenarios.empty()
		|| variantScenarios.size() > PlanningRiskTable::MAX_VARIANTS) return combined;
	bool allFavorable = true, allDefeats = true;
	combined.adverseUpper = 0.0;
	combined.defeatLower = 1.0;
	for(const auto & variant : variantScenarios)
	{
		PlanningRiskEvidence evidence;
		for(const auto & outcome : variant)
		{
			if(outcome.status != SampledBattleStatus::FINISHED || outcome.initialArmyValue[0] <= 0)
				return RiskAssessment{};
			const double loss = 1.0 - outcome.remainingArmyValue[0] / outcome.initialArmyValue[0];
			if(!evidence.append(outcome.winner == BattleSide::ATTACKER, loss))
				return RiskAssessment{};
		}
		const auto result = evidence.assess(tolerance, static_cast<unsigned>(variantScenarios.size()),
			permitDefeatRecommendation && !unmodelledOwnMagic);
		allFavorable &= result.recommendation == RiskRecommendation::FAVORABLE;
		allDefeats &= result.recommendation == RiskRecommendation::LIKELY_DEFEAT;
		combined.adverseUpper = std::max(combined.adverseUpper, result.adverseUpper);
		combined.defeatLower = std::min(combined.defeatLower, result.defeatLower);
		combined.samples += result.samples;
	}
	if(allFavorable) combined.recommendation = RiskRecommendation::FAVORABLE;
	else if(allDefeats) combined.recommendation = RiskRecommendation::LIKELY_DEFEAT;
	return combined;
}

double PlanningRiskRecord::worstSampledLoss() const
{
	if(!decisionEvidence || !allScenariosFinished || variantScenarios.empty()) return 1.0;
	double loss = 0.0;
	for(const auto & variant : variantScenarios)
		for(const auto & outcome : variant)
		{
			if(outcome.status != SampledBattleStatus::FINISHED || outcome.initialArmyValue[0] <= 0) return 1.0;
			loss = std::max(loss, 1.0 - outcome.remainingArmyValue[0] / outcome.initialArmyValue[0]);
		}
	return std::clamp(loss, 0.0, 1.0);
}

void PlanningRiskRecord::finalizeDecisionSummary()
{
	contextualAssessment = assess(contextualLossTolerance, true);
	contextualWorstLoss = worstSampledLoss();
	contextualSummaryReady = true;
}

std::shared_ptr<const PlanningRiskTable> PlanningRiskTable::buildDecision(std::vector<CapturedRiskRequest> requests,
	uint64_t turn, const DecisionRiskBudget & budget, std::shared_ptr<const PlanningRiskMemo> previousMemo)
{
	auto table = std::shared_ptr<PlanningRiskTable>(new PlanningRiskTable());
	auto memo = std::make_shared<PlanningRiskMemo>();
	memo->turn = turn;
	if(previousMemo && previousMemo->turn == turn) *memo = *previousMemo;
	if(requests.size() > MAX_REQUESTS) requests.resize(MAX_REQUESTS);
	std::map<PlanningRiskKey, unsigned> counts;
	for(const auto & request : requests) ++counts[request.key];
	std::vector<PlanningRiskRecord> records(requests.size());
	std::vector<bool> active(requests.size(), true);
	for(size_t i = 0; i < requests.size(); ++i)
	{
		const auto & request = requests[i];
		auto & record = records[i];
		record.decisionEvidence = true;
		record.unmodelledOwnMagic = request.unmodelledOwnMagic;
		record.contextualLossTolerance = request.contextualLossTolerance;
		active[i] = !request.key.capturedState.empty() && counts.at(request.key) == 1
			&& !request.variants.empty() && request.variants.size() <= MAX_VARIANTS
			&& std::all_of(request.variants.begin(), request.variants.end(), [](const auto & variant) { return bool(variant); });
		const auto cached = memo->entries.find("decision-v2:" + request.physicsState);
		if(active[i] && !request.physicsState.empty() && cached != memo->entries.end()
			&& cached->second.decisionEvidence && cached->second.variantScenarios.size() == request.variants.size())
		{
			record = cached->second;
			record.reused = true;
			record.elapsedMicroseconds = 0;
			record.unmodelledOwnMagic = request.unmodelledOwnMagic;
			record.contextualLossTolerance = request.contextualLossTolerance;
		}
		else record.variantScenarios.resize(request.variants.size());
	}
	const auto chargedBefore = budget.drawsStarted();
	std::vector<std::vector<std::unique_ptr<std::mutex>>> snapshotLocks(requests.size());
	for(size_t i = 0; i < requests.size(); ++i)
		for(size_t v = 0; v < requests[i].variants.size(); ++v) snapshotLocks[i].push_back(std::make_unique<std::mutex>());
	const std::function<bool()> stop = [&budget] { return budget.shouldStop(); };
	for(const auto checkpoint : PlanningRiskEvidence::CHECKPOINTS)
	{
		if(budget.shouldStop()) break;
		struct Job { size_t request, variant; unsigned seedIndex; };
		struct Result { std::optional<SampledBattleOutcome> outcome; uint64_t micros = 0; };
		std::vector<Job> jobs;
		// Prefix order is fixed, with candidates interleaved before another
		// seed is assigned. Worker completion order never selects lucky draws.
		for(unsigned seed = 0; seed < checkpoint; ++seed)
			for(size_t i = 0; i < requests.size(); ++i)
				if(active[i])
					for(size_t v = 0; v < requests[i].variants.size(); ++v)
						if(records[i].variantScenarios[v].size() <= seed) jobs.push_back({i, v, seed});
		std::vector<Result> results(jobs.size());
		std::atomic<size_t> next{0};
		auto worker = [&]
		{
			while(!budget.shouldStop())
			{
				const size_t index = next.fetch_add(1);
				if(index >= jobs.size()) return;
				const auto job = jobs[index];
				// Const engine units may still maintain caches: serialize readers
				// of one snapshot, parallelize only independently owned contexts.
				std::unique_lock snapshotLock(*snapshotLocks[job.request][job.variant]);
				if(!budget.tryStartDraw()) return;
				const auto & snapshot = requests[job.request].variants[job.variant];
				const auto begin = std::chrono::steady_clock::now();
				// Public deterministic independent seeds, never authoritative RNG.
				const int seed = 104729 + static_cast<int>(job.seedIndex) * 7919;
				try { results[index].outcome = runSampledBattle(snapshot->battle(), snapshot->frozenGameInfo(), seed, nullptr, &stop); }
				catch(...) { results[index].outcome.emplace(); results[index].outcome->unsupportedReason = "detached simulation exception"; }
				results[index].micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
			}
		};
		std::vector<std::future<void>> workers;
		for(unsigned i = 1; i < budget.workerLimit() && !jobs.empty(); ++i)
			workers.push_back(std::async(std::launch::async, worker));
		worker();
		for(auto & future : workers) future.get();
		for(size_t i = 0; i < jobs.size(); ++i)
		{
			const auto job = jobs[i];
			auto & record = records[job.request];
			record.elapsedMicroseconds += results[i].micros;
			auto & prefix = record.variantScenarios[job.variant];
			// Never publish a result after a missing/interrupted earlier seed.
			if(prefix.size() != job.seedIndex || !results[i].outcome) continue;
			// A shared cancellation/deadline leaves an incomplete prefix. An
			// intrinsic rollout cap is instead a recorded nonterminal outcome:
			// it must stop this request, not retry the identical seed at every
			// later checkpoint and discard all following charged draws.
			if(results[i].outcome->status == SampledBattleStatus::BUDGET_EXHAUSTED) continue;
			prefix.push_back(std::move(*results[i].outcome));
		}
		for(size_t i = 0; i < records.size(); ++i)
		{
			if(!active[i]) continue;
			auto & record = records[i];
			record.allScenariosFinished = active[i];
			for(const auto & variant : record.variantScenarios)
			{
				const bool supported = std::all_of(variant.begin(), variant.end(), [](const auto & outcome) { return outcome.status == SampledBattleStatus::FINISHED; });
				record.allScenariosFinished &= supported && variant.size() >= checkpoint;
				if(!supported) active[i] = false;
			}
			// Stop early only for robust no-spell defeat, never after a few wins.
			if(record.assess(1.0, true).recommendation == RiskRecommendation::LIKELY_DEFEAT) active[i] = false;
		}
	}
	table->computedScenarios = budget.drawsStarted() - chargedBefore;
	memo->consumedScenarios += std::min(table->computedScenarios, std::numeric_limits<unsigned>::max() - memo->consumedScenarios);
	for(size_t i = 0; i < records.size(); ++i)
	{
		auto & record = records[i];
		record.finalizeDecisionSummary();
		record.scenarios.clear();
		for(const auto & variant : record.variantScenarios) record.scenarios.insert(record.scenarios.end(), variant.begin(), variant.end());
		const auto memoKey = "decision-v2:" + requests[i].physicsState;
		if(!requests[i].physicsState.empty() && (memo->entries.count(memoKey) || memo->entries.size() < MAX_MEMO_ENTRIES))
			memo->entries[memoKey] = record;
		if(counts.at(requests[i].key) == 1) table->entries.emplace(std::move(requests[i].key), std::move(record));
	}
	table->memoState = std::move(memo);
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
	if(found == entries.end()) return nullptr;
	if(found->second.decisionEvidence && (!route.knownEarlierDanger || route.earlierDanger)) return nullptr;
	return &found->second;
}
}
