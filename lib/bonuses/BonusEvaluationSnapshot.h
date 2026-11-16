/*
 * BonusEvaluationSnapshot.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "BonusGraphSnapshot.h"
#include "BonusUpdaterContext.h"
#include "BonusLimiterContext.h"

class BonusGraphMutationState;

/// Fresh native bonus evaluation over owned graph and node facts.
class DLL_LINKAGE BonusEvaluationSnapshot
{
public:
	using NodeId = BonusGraphSnapshot::NodeId;
	using FactCapture = std::function<BonusLimiterFacts(const CBonusSystemNode &)>;

	/// The caller supplies one consistent read epoch and a read-only value-fact capture.
	static BonusEvaluationSnapshot capture(const std::vector<const CBonusSystemNode *> & roots, const FactCapture & captureFacts, const std::vector<TConstBonusListPtr> & extraLists = {});
	static BonusEvaluationSnapshot capture(const std::vector<const CBonusSystemNode *> & roots);
	static BonusEvaluationSnapshot captureWithLists(const std::vector<const CBonusSystemNode *> & roots, const std::vector<TConstBonusListPtr> & extraLists);
	const BonusGraphSnapshot & graph() const;
	// Arbitrary reparenting after a battle-leaf append has no native-order guarantee.
	BonusGraphMutationState mutations() const;
	const SnapshotBonusUpdaterContext & updaterContext(NodeId node) const;
	/// Rebind fixed node facts only to a committed graph from this capture.
	BonusEvaluationSnapshot withGraph(BonusGraphSnapshot graph) const;
	TConstBonusListPtr getAllBonuses(NodeId node, const CSelector & selector) const;

private:
	friend class OwnedBattleBonusModel;
	friend class OrdinaryPreviewDefinitionGuard;
	static BonusEvaluationSnapshot captureObserved(const std::vector<const CBonusSystemNode *> & roots, const FactCapture & captureFacts,
		const std::vector<TConstBonusListPtr> & extraLists, const BonusGraphSnapshot::NodeVisitor & visitor);
	friend class OwnedBonusGraphSession;
	BonusList evaluateAccepted(NodeId node, const std::vector<std::shared_ptr<const Bonus>> & graphInputs) const;
	// Native ordering holds only for the model's closed battle-leaf operations.
	std::pair<BonusEvaluationSnapshot, NodeId> appendBattleLeaf(SnapshotBonusUpdaterContext context, BonusLimiterFacts facts, NodeId army, NodeId creature) const;
	BonusGraphSnapshot capturedGraph;
	std::vector<SnapshotBonusUpdaterContext> updaterContexts;
	std::vector<BonusLimiterFacts> limiterFacts;
};
