/* GPL-2.0-or-later; see license.txt. Test-only publication injection. */
#pragma once
#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
namespace NK2AI
{
class PlanningRiskConsumerRegressionAccess
{
public:
	static void publish(Nullkiller & ai, experimental::PlanningRiskKey key, experimental::PlanningRiskRecord record)
	{
		record.finalizeDecisionSummary();
		auto table = std::shared_ptr<experimental::PlanningRiskTable>(new experimental::PlanningRiskTable);
		table->entries.emplace(key, std::move(record));
		auto publication = std::make_shared<Nullkiller::PreparedPlanningRisk>();
		publication->epoch = key.epoch;
		publication->keys.push_back(key);
		publication->table = table;
		ai.planningRiskEpoch.store(key.epoch);
		ai.storePlanningRiskPublication(std::move(publication));
	}
};
}
