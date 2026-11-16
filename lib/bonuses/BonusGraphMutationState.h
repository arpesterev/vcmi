/*
 * BonusGraphMutationState.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "BonusGraphSnapshot.h"
#include "BonusUpdaterContext.h"

/// Transactional graph mutations accept snapshot-local identities only.
class DLL_LINKAGE BonusGraphMutationState
{
public:
	using NodeId = BonusGraphSnapshot::NodeId;
	using BonusId = BonusGraphSnapshot::BonusId;

	const std::vector<BonusGraphSnapshot::Node> & nodes() const;
	const std::vector<std::shared_ptr<const Bonus>> & bonuses() const;
	uint64_t revision() const;
	BonusGraphSnapshot snapshot() const;

	void addExistingBonus(NodeId node, BonusId bonus);
	void removeBonus(NodeId node, BonusId bonus);

	void attach(NodeId node, NodeId parent);
	void attachSource(NodeId node, NodeId parent);
	void detach(NodeId node, NodeId parent);
	void detachSource(NodeId node, NodeId parent);

private:
	friend class BonusEvaluationSnapshot;
	friend class OwnedBonusGraphSession;
	void replaceOwnedValues(std::vector<std::shared_ptr<const Bonus>> values);
	BonusId addOwnedRoot(NodeId node, const Bonus & value);
	BonusGraphMutationState(const BonusGraphSnapshot & graph, std::vector<SnapshotBonusUpdaterContext> contexts);
	std::vector<BonusGraphSnapshot::Node> ownedNodes;
	std::vector<NodeId> capturedRoots;
	std::shared_ptr<const BonusGraphSnapshot::CaptureIdentity> capturedIdentity;
	std::vector<std::shared_ptr<const Bonus>> ownedBonuses;
	std::vector<SnapshotBonusUpdaterContext> updaterContexts;
	uint64_t committedRevision = 0;
	std::vector<NodeId> invalidatedNodes;
	BonusGraphSnapshot captureMetadata;

	NodeId appendBattleLeaf(SnapshotBonusUpdaterContext context, NodeId army, NodeId creature);
	void validateAttachment(NodeId node, NodeId parent, bool sourceOnly) const;
	friend class OwnedBonusGraphMutationStorage;
};
