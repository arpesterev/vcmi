/*
 * BonusGraphSnapshot.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "Bonus.h"

class CBonusSystemNode;

/// Owned graph and native cache capture; node evaluation facts are stored separately.
class DLL_LINKAGE BonusGraphSnapshot
{
public:
	using NodeId = size_t;
	using BonusId = size_t;
	using ValueId = size_t;

	struct MutationBatch
	{
		std::vector<NodeId> invalidated;
	};

	struct NodeCache
	{
		int64_t epoch = 0;
		bool valid = false;
		std::vector<ValueId> accepted;
		std::map<std::string, std::vector<ValueId>> requests;
	};

	struct Node
	{
		BonusNodeType type;
		bool hypothetical;
		std::vector<NodeId> inheritanceParents;
		std::vector<NodeId> propagationParents;
		std::vector<NodeId> children;
		std::vector<BonusId> materialized;
		std::vector<BonusId> exported;
	};

	/// The caller must exclude native writes AND cache-filling reads throughout capture.
	using NodeVisitor = std::function<void(NodeId, const CBonusSystemNode &)>;
	static BonusGraphSnapshot capture(const std::vector<const CBonusSystemNode *> & roots, const NodeVisitor & visitor = {}, const std::vector<TConstBonusListPtr> & extraLists = {});

	/// Captured node IDs retain native pointer ordering; appended battle leaves do not.
	bool sameOrigin(const BonusGraphSnapshot & other) const;
	const std::vector<Node> & nodes() const;
	const std::vector<NodeId> & roots() const;
	const std::vector<std::shared_ptr<const Bonus>> & bonuses() const;
	const std::vector<std::shared_ptr<const Bonus>> & capturedValues() const;
	const std::vector<ValueId> & graphValueIds() const;
	const std::vector<NodeCache> & nodeCaches() const;
	const std::vector<std::vector<ValueId>> & capturedExtraLists() const;
	const std::shared_ptr<const MutationBatch> & mutationBatch() const;
	/// Deep copies preserve aliases within the result, never with the input.
	static std::vector<std::shared_ptr<Bonus>> detachValues(const std::vector<std::shared_ptr<const Bonus>> & values);

private:
	friend class BonusGraphMutationState;
	friend class OwnedBonusGraphSession;
	friend class OwnedBonusLogicalDomain;
	struct CaptureIdentity {};
	std::shared_ptr<const CaptureIdentity> capturedIdentity;
	std::vector<Node> capturedNodes;
	std::vector<NodeId> capturedRoots;
	std::vector<std::shared_ptr<const Bonus>> capturedBonuses;
	std::vector<std::shared_ptr<const Bonus>> capturedArenaValues;
	std::vector<ValueId> capturedGraphValueIds;
	std::vector<NodeCache> capturedNodeCaches;
	std::vector<std::vector<ValueId>> extraListValues;
	std::shared_ptr<const MutationBatch> capturedMutationBatch;
};
