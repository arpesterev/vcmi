/*
 * CBonusSystemNode.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "CBonusSystemNode.h"
#include "BonusGraphMutation.h"
#include "BonusGraphAlgorithms.h"
#include "Limiters.h"
#include "Updaters.h"
#include "Propagators.h"

constexpr bool cachingEnabled = true;
static std::atomic<int32_t> globalCounter = 1;

class BonusGraphMutationStorage
{
public:
	using Node = CBonusSystemNode *;
	using ConstNode = const CBonusSystemNode *;
	using Bonus = std::shared_ptr<::Bonus>;

	const auto & inheritanceParents(ConstNode node) const { return node->parentsToInherit; }
	const auto & propagationParents(ConstNode node) const { return node->parentsToPropagate; }
	const auto & children(ConstNode node) const { return node->children; }
	const auto & exported(ConstNode node) const { return node->exportedBonuses; }
	auto & active(Node node) { return node->bonuses; }
	bool sourceOnly(ConstNode node) const { return node->actsAsBonusSourceOnly(); }
	bool hypothetical(ConstNode node) const { return node->isHypothetic(); }
	bool hasPropagator(const Bonus & bonus) const { return bool(bonus->propagator); }
	bool hasPropagationUpdater(const Bonus & bonus) const { return bool(bonus->propagationUpdater); }
	template<class Candidate>
	bool samePropagationUpdater(const Candidate & left, const Bonus & right) const
	{
		return left->propagationUpdater && left->propagationUpdater == right->propagationUpdater;
	}
	bool shouldAttach(const Bonus & bonus, Node node) const { return bonus->propagator->shouldBeAttached(node); }
	Bonus materialize(const Bonus & bonus, ConstNode source) const
	{
		return bonus->propagationUpdater ? source->getUpdatedBonus(bonus, bonus->propagationUpdater) : bonus;
	}
	void eraseExportedFirst(Node node, const Bonus & bonus) { node->exportedBonuses -= bonus; }
	void appendActive(Node node, const Bonus & bonus) { node->bonuses.push_back(bonus); }
	void eraseActive(Node node, size_t index) { node->bonuses.erase(index); }
	template<class Predicate> void eraseActiveMatching(Node node, Predicate predicate)
	{
		node->bonuses.remove_if(predicate);
	}
	void propagated(Node node, const Bonus & bonus)
	{
		logBonus->trace("Bonus (type=%d, val=%d) propagated to %s", static_cast<int>(bonus->type), bonus->val, node->nodeName());
		node->nodeHasChanged();
	}
	void unpropagated(Node node, const Bonus & bonus, bool removed)
	{
		if(!bonus->propagationUpdater)
		{
			if(removed)
				logBonus->trace("Bonus (type=%d, val=%d) is no longer propagated to %s", static_cast<int>(bonus->type), bonus->val, node->nodeName());
			else
				logBonus->warn("Attempt to remove bonus (type=%d, val=%d), which is not propagated to %s", static_cast<int>(bonus->type), bonus->val, node->nodeName());
		}
		if(removed)
			node->nodeHasChanged();
	}
	void beginEdgeChange() { ++globalCounter; }
	void invalidate(Node node) { node->invalidateChildrenNodes(globalCounter); }
	void changed(Node node) { node->nodeHasChanged(); }
	void appendInheritance(Node node, ConstNode parent)
	{
		assert(!vstd::contains(node->parentsToInherit, parent));
		node->parentsToInherit.push_back(parent);
	}
	void appendPropagation(Node node, Node parent)
	{
		assert(!vstd::contains(node->parentsToPropagate, parent));
		node->parentsToPropagate.push_back(parent);
	}
	void appendChild(Node parent, Node node)
	{
		assert(!vstd::contains(parent->children, node));
		parent->children.push_back(node);
	}
	void requireInheritance(Node node, ConstNode parent) const { assert(vstd::contains(node->parentsToInherit, parent)); }
	void requirePropagation(Node node, Node parent) const { assert(vstd::contains(node->parentsToPropagate, parent)); }
	void eraseInheritance(Node node, ConstNode parent)
	{
		if(vstd::contains(node->parentsToInherit, parent))
			node->parentsToInherit -= parent;
		else
			logBonus->error("Error on Detach. Node %s (nodeType=%d) has not parent %s (nodeType=%d)",
				node->nodeShortInfo(), static_cast<int>(node->nodeType), parent->nodeShortInfo(), static_cast<int>(parent->nodeType));
	}
	void erasePropagation(Node node, Node parent)
	{
		if(vstd::contains(node->parentsToPropagate, parent))
			node->parentsToPropagate -= parent;
		else
			logBonus->error("Error on Detach. Node %s (nodeType=%d) has not parent %s (nodeType=%d)",
				node->nodeShortInfo(), static_cast<int>(node->nodeType), parent->nodeShortInfo(), static_cast<int>(parent->nodeType));
	}
	void eraseChild(Node parent, Node node)
	{
		if(vstd::contains(parent->children, node))
			parent->children -= node;
		else
			logBonus->error("Error on Detach. Node %s (nodeType=%d) is not a child of %s (nodeType=%d)",
				node->nodeShortInfo(), static_cast<int>(node->nodeType), parent->nodeShortInfo(), static_cast<int>(parent->nodeType));
	}
};


std::shared_ptr<Bonus> CBonusSystemNode::getLocalBonus(const CSelector & selector)
{
	auto ret = bonuses.getFirst(selector);
	if(ret)
		return ret;
	return nullptr;
}

std::shared_ptr<const Bonus> CBonusSystemNode::getFirstBonus(const CSelector & selector) const
{
	auto ret = bonuses.getFirst(selector);
	if(ret)
		return ret;

	TCNodes lparents;
	getDirectParents(lparents);
	for(const CBonusSystemNode *pname : lparents)
	{
		ret = pname->getFirstBonus(selector);
		if (ret)
			return ret;
	}

	return nullptr;
}

void CBonusSystemNode::getDirectParents(TCNodes & out) const /*retrieves list of parent nodes (nodes to inherit bonuses from) */
{
	for(const auto * elem : parentsToInherit)
		out.insert(elem);
}

void CBonusSystemNode::getAllBonusesRec(BonusList & out) const
{
	BonusGraphAlgorithms::collect(parentsToInherit, bonuses,
		[](const CBonusSystemNode * parent, BonusList & result) { parent->getAllBonusesRec(result); },
		[this](const std::shared_ptr<Bonus> & bonus, const TUpdaterPtr & updater) { return getUpdatedBonus(bonus, updater); }, out);
}

TConstBonusListPtr CBonusSystemNode::getAllBonuses(const CSelector &selector, const std::string &cachingStr) const
{
	if (cachingEnabled)
	{
		// If a bonus system request comes with a caching string then look up in the map if there are any
		// pre-calculated bonus results. Limiters can't be cached so they have to be calculated.
		if (cachedLast == nodeChanged && !cachingStr.empty())
		{
			RequestsMap::const_accessor accessor;

			//Cached list contains bonuses for our query with applied limiters
			if (cachedRequests.find(accessor, cachingStr) && accessor->second.first == cachedLast)
				return accessor->second.second;
		}

		//We still don't have the bonuses (didn't returned them from cache)
		//Perform bonus selection
		auto ret = std::make_shared<BonusList>();

		if (cachedLast == nodeChanged)
		{
			// Cached bonuses are up-to-date - use shared/read access and compute results
			std::shared_lock lock(sync);
			cachedBonuses.getBonuses(*ret, selector);
		}
		else
		{
			// If the bonus system tree changes(state of a single node or the relations to each other) then
			// cache all bonus objects. Selector objects doesn't matter.
			std::lock_guard lock(sync);
			if (cachedLast == nodeChanged)
			{
				// While our thread was waiting, another one have updated bonus tree. Use cached bonuses.
				cachedBonuses.getBonuses(*ret, selector);
			}
			else
			{
				// Cached bonuses may be outdated - regenerate them
				BonusList allBonuses;

				cachedBonuses.clear();

				getAllBonusesRec(allBonuses);
				limitBonuses(allBonuses, cachedBonuses);
				cachedBonuses.stackBonuses();
				cachedLast = nodeChanged;
				cachedBonuses.getBonuses(*ret, selector);
			}
		}

		// Save the results in the cache
		if (!cachingStr.empty())
		{
			RequestsMap::accessor accessor;
			if (cachedRequests.find(accessor, cachingStr))
			{
				accessor->second.second = ret;
				accessor->second.first = cachedLast;
			}
			else
				cachedRequests.emplace(cachingStr, std::pair<int32_t, TBonusListPtr>{ cachedLast, ret });
		}

		return ret;
	}
	else
	{
		return getAllBonusesWithoutCaching(selector);
	}
}

TConstBonusListPtr CBonusSystemNode::getAllBonusesWithoutCaching(const CSelector &selector) const
{
	auto ret = std::make_shared<BonusList>();

	// Get bonus results without caching enabled.
	BonusList beforeLimiting;
	BonusList afterLimiting;
	getAllBonusesRec(beforeLimiting);
	limitBonuses(beforeLimiting, afterLimiting);
	afterLimiting.getBonuses(*ret, selector);
	ret->stackBonuses();
	return ret;
}

std::shared_ptr<Bonus> CBonusSystemNode::getUpdatedBonus(const std::shared_ptr<Bonus> & b, const TUpdaterPtr & updater) const
{
	assert(updater);
	return updater->createUpdatedBonus(b, * this);
}

CBonusSystemNode::CBonusSystemNode(BonusNodeType NodeType, bool isHypotetic):
	nodeType(NodeType),
	cachedLast(0),
	nodeChanged(0),
	isHypotheticNode(isHypotetic)
{
}

CBonusSystemNode::CBonusSystemNode(BonusNodeType NodeType):
	CBonusSystemNode(NodeType, false)
{}

CBonusSystemNode::~CBonusSystemNode()
{
	detachFromAll();

	if(!children.empty())
	{
		while(!children.empty())
			children.front()->detachFrom(*this);
	}
}

void CBonusSystemNode::attachTo(CBonusSystemNode & parent)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.attach(this, &parent);
}

void CBonusSystemNode::attachToSource(const CBonusSystemNode & parent)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.attachSource(this, &parent);
}

void CBonusSystemNode::detachFrom(CBonusSystemNode & parent)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.detach(this, &parent);
}


void CBonusSystemNode::detachFromSource(const CBonusSystemNode & parent)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.detachSource(this, &parent);
}

void CBonusSystemNode::removeBonusesRecursive(const CSelector & s)
{
	removeBonuses(s);
	for(CBonusSystemNode * child : children)
		child->removeBonusesRecursive(s);
}

void CBonusSystemNode::reduceBonusDurations(const CSelector &s)
{
	BonusList bl;
	exportedBonuses.getBonuses(bl, s);
	for(const auto & b : bl)
	{
		b->turnsRemain--;
		if(b->turnsRemain <= 0)
			removeBonus(b);
	}

	for(CBonusSystemNode *child : children)
		child->reduceBonusDurations(s);
}

void CBonusSystemNode::addNewBonus(const std::shared_ptr<Bonus>& b)
{
	//turnsRemain shouldn't be zero for following durations
	if(Bonus::NTurns(b.get()) || Bonus::NDays(b.get()) || Bonus::OneWeek(b.get()))
	{
		assert(b->turnsRemain);
	}

	assert(!vstd::contains(exportedBonuses, b));
	exportedBonuses.push_back(b);
	exportBonus(b);
}

void CBonusSystemNode::accumulateBonus(const std::shared_ptr<Bonus>& b)
{
	auto bonus = exportedBonuses.getFirst(Selector::typeSubtypeValueType(b->type, b->subtype, b->valType)); //only local bonuses are interesting
	if(bonus)
		bonus->val += b->val;
	else
		addNewBonus(std::make_shared<Bonus>(*b)); //duplicate needed, original may get destroyed
}

void CBonusSystemNode::removeBonus(const std::shared_ptr<Bonus>& b)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation(storage).removeBonus(this, b);
}

void CBonusSystemNode::removeBonuses(const CSelector & selector)
{
	BonusList toRemove;
	exportedBonuses.getBonuses(toRemove, selector);
	for(const auto & bonus : toRemove)
		removeBonus(bonus);
}

bool CBonusSystemNode::actsAsBonusSourceOnly() const
{
	switch(nodeType)
	{
	case BonusNodeType::CREATURE:
	case BonusNodeType::ARTIFACT:
	case BonusNodeType::ARTIFACT_INSTANCE:
	case BonusNodeType::BOAT:
		return true;
	default:
		return false;
	}
}

void CBonusSystemNode::propagateBonus(const std::shared_ptr<Bonus> & b, const CBonusSystemNode & source)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.propagate(this, b, &source);
}

void CBonusSystemNode::unpropagateBonus(const std::shared_ptr<Bonus> & b)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.unpropagate(this, b);
}

void CBonusSystemNode::detachFromAll()
{
	while(!parentsToPropagate.empty())
		detachFrom(*parentsToPropagate.front());

	while(!parentsToInherit.empty())
		detachFromSource(*parentsToInherit.front());
}

bool CBonusSystemNode::isIndependentNode() const
{
	return parentsToInherit.empty() && parentsToPropagate.empty() && children.empty();
}

std::string CBonusSystemNode::nodeName() const
{
	return std::string("Bonus system node of type ") + typeid(*this).name();
}

std::string CBonusSystemNode::nodeShortInfo() const
{
	std::ostringstream str;
	str << "'" << typeid(* this).name() << "'";
	return str.str();
}

void CBonusSystemNode::getRedParents(TCNodes & out) const
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.redParents(this, out);
}

void CBonusSystemNode::getRedChildren(TNodes &out)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.redChildren(this, out);
}

void CBonusSystemNode::newRedDescendant(CBonusSystemNode & descendant) const
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.newDescendant(this, &descendant);
}

void CBonusSystemNode::removedRedDescendant(CBonusSystemNode & descendant) const
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.removedDescendant(this, &descendant);
}

void CBonusSystemNode::getRedAncestors(TCNodes &out) const
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation kernel(storage);
	kernel.redAncestors(this, out);
}

void CBonusSystemNode::exportBonus(const std::shared_ptr<Bonus> & b)
{
	BonusGraphMutationStorage storage;
	BonusGraphMutation(storage).exportBonus(this, b);
}

void CBonusSystemNode::exportBonuses()
{
	for(const auto & b : exportedBonuses)
		exportBonus(b);
}

BonusNodeType CBonusSystemNode::getNodeType() const
{
	return nodeType;
}

const TCNodesVector& CBonusSystemNode::getParentNodes() const
{
	return parentsToInherit;
}

BonusList & CBonusSystemNode::getExportedBonusList()
{
	return exportedBonuses;
}

const BonusList & CBonusSystemNode::getExportedBonusList() const
{
	return exportedBonuses;
}

void CBonusSystemNode::limitBonuses(const BonusList & allBonuses, BonusList & out) const
{
	BonusGraphAlgorithms::limit(allBonuses, out, [this](const Bonus & bonus, const BonusList & accepted, const BonusList & undecided)
	{
		BonusLimitationContext context = {bonus, *this, accepted, undecided};
		return bonus.limiter ? bonus.limiter->limit(context) : ILimiter::EDecision::ACCEPT;
	});
}

void CBonusSystemNode::nodeHasChanged()
{
	invalidateChildrenNodes(++globalCounter);
}

void CBonusSystemNode::invalidateChildrenNodes(int32_t changeCounter)
{
	if (nodeChanged == changeCounter)
		return;

	nodeChanged = changeCounter;

	for(CBonusSystemNode * child : children)
		child->invalidateChildrenNodes(changeCounter);
}

int32_t CBonusSystemNode::getTreeVersion() const
{
	return nodeChanged;
}
