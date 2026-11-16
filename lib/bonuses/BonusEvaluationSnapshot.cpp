/*
 * BonusEvaluationSnapshot.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BonusEvaluationSnapshot.h"
#include "OwnedBonusValues.h"
#include "BonusEvaluationFacts.h"
#include "BonusGraphMutationState.h"
#include "BonusGraphAlgorithms.h"
#include "CBonusSystemNode.h"
#include "Updaters.h"

BonusEvaluationSnapshot BonusEvaluationSnapshot::capture(const std::vector<const CBonusSystemNode *> & roots)
{
	return captureWithLists(roots, {});
}

BonusEvaluationSnapshot BonusEvaluationSnapshot::captureWithLists(const std::vector<const CBonusSystemNode *> & roots, const std::vector<TConstBonusListPtr> & extraLists)
{
	const auto definitions = BonusEvaluationFactCapture::captureDefinitions();
	return capture(roots, [&](const CBonusSystemNode & node) { return definitions.captureNode(node); }, extraLists);
}

BonusEvaluationSnapshot BonusEvaluationSnapshot::capture(const std::vector<const CBonusSystemNode *> & roots, const FactCapture & captureFacts, const std::vector<TConstBonusListPtr> & extraLists)
{
	return captureObserved(roots, captureFacts, extraLists, {});
}

BonusEvaluationSnapshot BonusEvaluationSnapshot::captureObserved(const std::vector<const CBonusSystemNode *> & roots, const FactCapture & captureFacts,
	const std::vector<TConstBonusListPtr> & extraLists, const BonusGraphSnapshot::NodeVisitor & visitor)
{
	if(!captureFacts)
		throw std::invalid_argument("Bonus evaluation snapshot requires node facts");
	BonusEvaluationSnapshot result;
	result.capturedGraph = BonusGraphSnapshot::capture(roots, [&](NodeId id, const CBonusSystemNode & node)
	{
		if(visitor) visitor(id, node);
		assert(id == result.updaterContexts.size());
		result.updaterContexts.emplace_back(NativeBonusUpdaterContext(node));
		auto facts = captureFacts(node);
		if(facts.type != node.getNodeType() || facts.originalOwner != node.getOwner())
			throw std::invalid_argument("Bonus evaluation facts do not match native node");
		result.limiterFacts.push_back(std::move(facts));
	}, extraLists);
	return result;
}

BonusGraphMutationState BonusEvaluationSnapshot::mutations() const
{
	return BonusGraphMutationState(capturedGraph, updaterContexts);
}

const BonusGraphSnapshot & BonusEvaluationSnapshot::graph() const
{
	return capturedGraph;
}

const SnapshotBonusUpdaterContext & BonusEvaluationSnapshot::updaterContext(NodeId node) const
{
	return updaterContexts.at(node);
}

BonusEvaluationSnapshot BonusEvaluationSnapshot::withGraph(BonusGraphSnapshot graph) const
{
	if(!capturedGraph.sameOrigin(graph) || graph.nodes().size() != capturedGraph.nodes().size())
		throw std::invalid_argument("Graph does not belong to this evaluation capture");
	for(NodeId id = 0; id < graph.nodes().size(); ++id)
	{
		const auto & original = capturedGraph.nodes().at(id);
		const auto & replacement = graph.nodes().at(id);
		if(original.type != replacement.type || original.hypothetical != replacement.hypothetical)
			throw std::invalid_argument("Graph node kind changed without replacement facts");
	}
	auto result = *this;
	result.capturedGraph = std::move(graph);
	return result;
}

TConstBonusListPtr BonusEvaluationSnapshot::getAllBonuses(NodeId node, const CSelector & selector) const
{
	std::vector<BonusGraphSnapshot::BonusId> ids;
	for(size_t id=0; id<capturedGraph.bonuses().size(); ++id) ids.push_back(id);
	auto detached = OwnedBonusValues::capture(capturedGraph).query(ids);
	std::vector<std::shared_ptr<const Bonus>> inputs(detached.begin(), detached.end());
	auto accepted = evaluateAccepted(node, inputs);
	auto result = std::make_shared<BonusList>();
	accepted.getBonuses(*result, selector);
	return result;
}

BonusList BonusEvaluationSnapshot::evaluateAccepted(NodeId node, const std::vector<std::shared_ptr<const Bonus>> & inputs) const
{
	if(inputs.size() != capturedGraph.bonuses().size())
		throw std::invalid_argument("Evaluation graph value binding differs");
	std::vector<bool> active(capturedGraph.nodes().size(), false);
	std::function<void(NodeId, BonusList &)> collect = [&](NodeId current, BonusList & out)
	{
		if(active.at(current))
			throw std::logic_error("Cyclic bonus inheritance graph");
		active[current] = true;
		const auto & record = capturedGraph.nodes().at(current);
		BonusList local;
		for(auto id : record.materialized)
			local.push_back(std::const_pointer_cast<Bonus>(inputs.at(id)));
		BonusGraphAlgorithms::collect(record.inheritanceParents, local, collect,
			[&](const std::shared_ptr<Bonus> & bonus, const TUpdaterPtr & updater)
			{
				return updater->createUpdatedBonus(bonus, updaterContexts.at(current));
			}, out);
		active[current] = false;
	};

	BonusList all;
	BonusList accepted;
	collect(node, all);
	BonusGraphAlgorithms::limit(all, accepted, [&](const Bonus & bonus, const BonusList & currentAccepted, const BonusList & undecided)
	{
		if(!bonus.limiter)
			return ILimiter::EDecision::ACCEPT;
		SnapshotBonusLimiterContext context(limiterFacts.at(node), bonus, currentAccepted, undecided);
		return limitBonus(*bonus.limiter, context);
	});
	accepted.stackBonuses();
	return accepted;
}

std::pair<BonusEvaluationSnapshot, BonusEvaluationSnapshot::NodeId> BonusEvaluationSnapshot::appendBattleLeaf(
	SnapshotBonusUpdaterContext context, BonusLimiterFacts facts, NodeId army, NodeId creature) const
{
	const auto & creatureFacts = limiterFacts.at(creature);
	if(context.nodeType() != BonusNodeType::STACK_BATTLE || facts.type != BonusNodeType::STACK_BATTLE
		|| facts.originalOwner != context.owner() || !facts.battleStack || facts.stackInstance
		|| !facts.creature || !facts.creatureID || facts.creatureID != creatureFacts.creatureID
		|| !facts.creatureLevel || *facts.creatureLevel != context.stackLevel()
		|| facts.creatureLevel != creatureFacts.creatureLevel || context.stackCount() < 0)
		throw std::invalid_argument("Invalid owned battle leaf facts");
	auto mutation = mutations();
	const auto node = mutation.appendBattleLeaf(context, army, creature);
	auto result = *this;
	result.updaterContexts.push_back(std::move(context));
	result.limiterFacts.push_back(std::move(facts));
	result.capturedGraph = mutation.snapshot();
	return {std::move(result), node};
}
