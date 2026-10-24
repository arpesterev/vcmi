/* GPL-2.0-or-later; see license.txt. Experimental, not production-wired. */
#pragma once
#include "PlanningBattleSnapshot.h"
#include "SampledBattleRollout.h"
#include "PlanningRiskEvidence.h"
#include "DecisionRiskBudget.h"

VCMI_LIB_NAMESPACE_BEGIN
class CGameInfoCallback;
VCMI_LIB_NAMESPACE_END

namespace NK2AI { class PlanningRiskConsumerRegressionAccess; }
namespace NK2AI::experimental
{
// Rebuilt for each planning epoch, never reused across a server-state update.
// The capture producer must provide a canonical, lossless descriptor covering
// source/target composition, effective bonuses, mana, settings and all context
// variants. This is NOT an ID-only or hash-only cache key.
struct PlanningRiskKey
{
	uint64_t epoch = 0;
	ObjectInstanceID hero;
	ObjectInstanceID target;
	std::string capturedState;
	bool operator==(const PlanningRiskKey & other) const = default;
	bool operator<(const PlanningRiskKey & other) const
	{
		return std::tie(epoch, hero, target, capturedState)
			< std::tie(other.epoch, other.hero, other.target, other.capturedState);
	}
};

struct PlanningRiskCandidate
{
	ObjectInstanceID hero;
	ObjectInstanceID target;
	uint32_t distance;
};
// Cheap, deterministic round-robin selection BEFORE snapshot construction.
std::vector<PlanningRiskCandidate> selectRiskCandidates(std::vector<PlanningRiskCandidate> candidates, uint64_t turn);

struct CapturedRiskRequest
{
	PlanningRiskKey key;
	std::string physicsState;
	bool unmodelledOwnMagic = false;
	double contextualLossTolerance = -1;
	// Every compatible visible battlefield must be represented, not a random
	// single field. More than the fixed variant budget produces no forecast.
	std::vector<std::unique_ptr<PlanningBattleSnapshot>> variants;
};

// Capture is called only while the gateway owns the shared game-state lock.
// Nullopt means eligibility, mechanics or context are unknown, never unsafe.
std::optional<CapturedRiskRequest> captureNeutralRisk(const CGameInfoCallback & visibleInfo,
	const CGHeroInstance & hero, const CGCreature & target, uint64_t epoch, std::string * unknownReason = nullptr);

struct PlanningRiskRoute
{
	bool usesCurrentHeroArmy = false;
	uint64_t earlierArmyLoss = 0;
	uint32_t spentMana = 0;
	uint64_t earlierDanger = 0;
	bool knownEarlierDanger = false;
};

struct PlanningRiskRecord
{
	std::vector<SampledBattleOutcome> scenarios;
	std::vector<std::vector<SampledBattleOutcome>> variantScenarios;
	bool decisionEvidence = false;
	RiskAssessment assess(double contextualLossTolerance, bool permitDefeatRecommendation) const;
	double worstSampledLoss() const;
	// Frozen once per publication, never recompute numerical bounds in path workers.
	RiskAssessment contextualAssessment;
	double contextualWorstLoss = 1;
	bool contextualSummaryReady = false;
	void finalizeDecisionSummary();
	uint64_t elapsedMicroseconds = 0;
	bool allScenariosFinished = false;
	bool reused = false;
	// A no-spell defeat/high loss cannot veto a plausible magic-assisted fight.
	bool unmodelledOwnMagic = false;
	double contextualLossTolerance = -1;
};

struct PlanningRiskMemo
{
	uint64_t turn = 0;
	unsigned consumedScenarios = 0;
	std::map<std::string, PlanningRiskRecord> entries;
};

// Immutable after construction; concurrent path readers require no mutex.
// Capture and source revalidation/publication are caller responsibilities.
// Builders read ONLY owned snapshots and must run without live game/AI locks.
// The deadline builder may run fewer than all admitted contexts to completion.
// Scheduler serializes builds using the latest same-turn memo, including when
// publication is rejected: spent computation cannot be refunded by a stale key.
class PlanningRiskTable
{
	friend class NK2AI::PlanningRiskConsumerRegressionAccess;
	std::map<PlanningRiskKey, PlanningRiskRecord> entries;
	PlanningRiskTable() = default;
	std::shared_ptr<const PlanningRiskMemo> memoState;
	unsigned computedScenarios = 0;
public:
	static constexpr size_t MAX_REQUESTS = 7;
	static constexpr size_t MAX_VARIANTS = 3;
	static constexpr size_t SAMPLES_PER_VARIANT = 3;
	static constexpr size_t MAX_MEMO_ENTRIES = 32;
	static constexpr unsigned MAX_SCENARIOS_PER_TURN = 42;
	static std::shared_ptr<const PlanningRiskTable> build(std::vector<CapturedRiskRequest> requests,
		uint64_t turn = 0, unsigned remainingTurnScenarios = MAX_SCENARIOS_PER_TURN,
		std::shared_ptr<const PlanningRiskMemo> previousMemo = {});
	static std::shared_ptr<const PlanningRiskTable> buildDecision(std::vector<CapturedRiskRequest> requests,
		uint64_t turn, const DecisionRiskBudget & budget, std::shared_ptr<const PlanningRiskMemo> previousMemo = {});
	unsigned scenariosUsed() const { return computedScenarios; }
	std::shared_ptr<const PlanningRiskMemo> memo() const { return memoState; }
	const PlanningRiskRecord * find(const PlanningRiskKey & exactCurrentKey,
		const PlanningRiskRoute & route) const;
};
}
