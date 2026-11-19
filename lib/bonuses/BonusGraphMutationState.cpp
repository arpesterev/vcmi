/*
 * BonusGraphMutationState.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BonusGraphMutationState.h"
#include "BonusGraphMutation.h"
#include "Propagators.h"
#include "Updaters.h"

class OwnedBonusGraphMutationStorage
{
	BonusGraphMutationState & state;
	static void require(bool condition)
	{
		if(!condition)
			throw std::logic_error("Invalid owned bonus graph edge mutation");
	}
	template<class T> static void append(std::vector<T> & values, T value)
	{
		require(std::find(values.begin(), values.end(), value) == values.end());
		values.push_back(value);
	}
	template<class T> static void erase(std::vector<T> & values, T value)
	{
		auto position = std::find(values.begin(), values.end(), value);
		require(position != values.end());
		values.erase(position);
	}
public:
	using Node = BonusGraphMutationState::NodeId;
	using ConstNode = Node;
	using Bonus = BonusGraphMutationState::BonusId;
	explicit OwnedBonusGraphMutationStorage(BonusGraphMutationState & state): state(state) {}
	const auto & inheritanceParents(Node node) const { return state.ownedNodes.at(node).inheritanceParents; }
	const auto & propagationParents(Node node) const { return state.ownedNodes.at(node).propagationParents; }
	const auto & children(Node node) const { return state.ownedNodes.at(node).children; }
	const auto & exported(Node node) const { return state.ownedNodes.at(node).exported; }
	auto & active(Node node) { return state.ownedNodes.at(node).materialized; }
	bool sourceOnly(Node node) const
	{
		switch(state.ownedNodes.at(node).type)
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
	bool hypothetical(Node node) const { return state.ownedNodes.at(node).hypothetical; }
	bool hasPropagator(Bonus bonus) const { return bool(state.ownedBonuses.at(bonus)->propagator); }
	bool hasPropagationUpdater(Bonus bonus) const { return bool(state.ownedBonuses.at(bonus)->propagationUpdater); }
	bool samePropagationUpdater(Bonus left, Bonus right) const
	{
		const auto & updater = state.ownedBonuses.at(left)->propagationUpdater;
		return updater && updater == state.ownedBonuses.at(right)->propagationUpdater;
	}
	bool shouldAttach(Bonus bonus, Node node) const
	{
		const auto & propagator = state.ownedBonuses.at(bonus)->propagator;
		const auto * propagatorPtr = propagator.get();
		if(typeid(*propagatorPtr) == typeid(IPropagator))
			return false;
		if(typeid(*propagatorPtr) != typeid(CPropagatorNodeType))
			throw std::logic_error("Unsupported owned bonus propagator");
		return matchesBonusPropagationType(state.ownedNodes.at(node).type, propagator->getPropagatorType());
	}
	Bonus materialize(Bonus bonus, Node source)
	{
		const auto original = state.ownedBonuses.at(bonus);
		if(!original->propagationUpdater)
			return bonus;
		auto input = std::make_shared<::Bonus>(*original);
		auto materialized = original->propagationUpdater->createUpdatedBonus(input, state.updaterContexts.at(source));
		if(materialized == input)
			return bonus;
		const auto result = state.ownedBonuses.size();
		state.ownedBonuses.push_back(std::move(materialized));
		return result;
	}
	void eraseExportedFirst(Node node, Bonus bonus)
	{
		auto & values = state.ownedNodes.at(node).exported;
		auto it = std::find(values.begin(), values.end(), bonus);
		if(it != values.end())
			values.erase(it);
	}
	void appendActive(Node node, Bonus bonus) { active(node).push_back(bonus); }
	void eraseActive(Node node, size_t index) { auto & values = active(node); values.erase(values.begin() + index); }
	template<class Predicate> void eraseActiveMatching(Node node, Predicate predicate)
	{
		eraseBonusGraphMatches(active(node), predicate);
	}
	void propagated(Node node, Bonus) { invalidate(node); }
	void unpropagated(Node node, Bonus, bool removed) { if(removed) invalidate(node); }
	void beginEdgeChange() {}
	void invalidate(Node node)
	{
		std::set<Node> visited;
		const auto visit = [&](const auto & self, Node current) -> void
		{
			if(!visited.insert(current).second) return;
			state.invalidatedNodes.push_back(current);
			for(auto child : children(current)) self(self, child);
		};
		visit(visit, node);
	}
	void changed(Node node) { invalidate(node); }
	void appendInheritance(Node node, Node parent) { append(state.ownedNodes.at(node).inheritanceParents, parent); }
	void appendPropagation(Node node, Node parent) { append(state.ownedNodes.at(node).propagationParents, parent); }
	void appendChild(Node parent, Node node) { append(state.ownedNodes.at(parent).children, node); }
	void requireInheritance(Node node, Node parent) const { const auto & values = inheritanceParents(node); require(std::find(values.begin(), values.end(), parent) != values.end()); }
	void requirePropagation(Node node, Node parent) const { const auto & values = propagationParents(node); require(std::find(values.begin(), values.end(), parent) != values.end()); }
	void eraseInheritance(Node node, Node parent) { erase(state.ownedNodes.at(node).inheritanceParents, parent); }
	void erasePropagation(Node node, Node parent) { erase(state.ownedNodes.at(node).propagationParents, parent); }
	void eraseChild(Node parent, Node node) { erase(state.ownedNodes.at(parent).children, node); }
};

BonusGraphMutationState::BonusGraphMutationState(const BonusGraphSnapshot & graph, std::vector<SnapshotBonusUpdaterContext> contexts):
	ownedNodes(graph.nodes()), capturedRoots(graph.roots()), capturedIdentity(graph.capturedIdentity), ownedBonuses(graph.bonuses()), updaterContexts(std::move(contexts)), captureMetadata(graph)
{
	if(updaterContexts.size() != ownedNodes.size())
		throw std::invalid_argument("Missing owned bonus updater contexts");
	for(NodeId node = 0; node < ownedNodes.size(); ++node)
		if(updaterContexts.at(node).nodeType() != ownedNodes.at(node).type)
			throw std::invalid_argument("Mismatched owned bonus updater context type");
}

const std::vector<BonusGraphSnapshot::Node> & BonusGraphMutationState::nodes() const { return ownedNodes; }
const std::vector<std::shared_ptr<const Bonus>> & BonusGraphMutationState::bonuses() const { return ownedBonuses; }
uint64_t BonusGraphMutationState::revision() const { return committedRevision; }

void BonusGraphMutationState::validateAttachment(NodeId node, NodeId parent, bool sourceOnly) const
{
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	storage.appendInheritance(node, parent);
	if(!sourceOnly)
	{
		storage.appendPropagation(node, parent);
		if(!storage.hypothetical(node))
			storage.appendChild(parent, node);
	}
	BonusGraphMutation kernel(storage);
	for(bool red : {false, true})
	{
		std::vector<int> color(ownedNodes.size());
		std::function<void(NodeId)> visit = [&](NodeId current)
		{
			if(color.at(current) == 1)
				throw std::logic_error("Cyclic owned bonus graph attachment");
			if(color.at(current) == 2)
				return;
			color.at(current) = 1;
			if(red)
			{
				std::set<NodeId> children;
				kernel.redChildren(current, children);
				for(auto child : children)
					visit(child);
			}
			else
				for(auto ancestor : storage.inheritanceParents(current))
					visit(ancestor);
			color.at(current) = 2;
		};
		for(NodeId current = 0; current < ownedNodes.size(); ++current)
			visit(current);
	}
}

void BonusGraphMutationState::attach(NodeId node, NodeId parent)
{
	validateAttachment(node, parent, false);
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).attach(node, parent);
	trial.committedRevision++;
	*this = std::move(trial);
}
void BonusGraphMutationState::attachSource(NodeId node, NodeId parent)
{
	validateAttachment(node, parent, true);
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).attachSource(node, parent);
	trial.committedRevision++;
	*this = std::move(trial);
}
void BonusGraphMutationState::detach(NodeId node, NodeId parent)
{
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).detach(node, parent);
	trial.committedRevision++;
	*this = std::move(trial);
}
void BonusGraphMutationState::detachSource(NodeId node, NodeId parent)
{
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).detachSource(node, parent);
	trial.committedRevision++;
	*this = std::move(trial);
}

void BonusGraphMutationState::addExistingBonus(NodeId node, BonusId bonus)
{
	const auto & value = ownedBonuses.at(bonus);
	if((::Bonus::NTurns(value.get()) || ::Bonus::NDays(value.get()) || ::Bonus::OneWeek(value.get())) && !value->turnsRemain)
		throw std::logic_error("Zero-duration owned bonus export");
	auto trial = *this;
	auto & exported = trial.ownedNodes.at(node).exported;
	if(std::find(exported.begin(), exported.end(), bonus) != exported.end())
		throw std::logic_error("Duplicate owned bonus export");
	exported.push_back(bonus);
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).exportBonus(node, bonus);
	trial.committedRevision++;
	*this = std::move(trial);
}
void BonusGraphMutationState::removeBonus(NodeId node, BonusId bonus)
{
	(void)ownedBonuses.at(bonus);
	auto trial = *this;
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation(storage).removeBonus(node, bonus);
	trial.committedRevision++;
	*this = std::move(trial);
}

BonusGraphSnapshot BonusGraphMutationState::snapshot() const
{
	BonusGraphSnapshot result=captureMetadata;
	result.capturedIdentity = capturedIdentity;
	result.capturedNodes = ownedNodes;
	result.capturedRoots = capturedRoots;
	result.capturedBonuses = ownedBonuses;
	result.capturedMutationBatch = std::make_shared<const BonusGraphSnapshot::MutationBatch>(BonusGraphSnapshot::MutationBatch{invalidatedNodes});
	return result;
}

BonusGraphMutationState::NodeId BonusGraphMutationState::appendBattleLeaf(SnapshotBonusUpdaterContext context, NodeId army, NodeId creature)
{
	const auto armyType = ownedNodes.at(army).type;
	if((armyType != BonusNodeType::ARMY && armyType != BonusNodeType::HERO && armyType != BonusNodeType::TOWN)
		|| ownedNodes.at(army).hypothetical || ownedNodes.at(creature).type != BonusNodeType::CREATURE
		|| ownedNodes.at(creature).hypothetical || context.nodeType() != BonusNodeType::STACK_BATTLE
		|| context.owner() != updaterContexts.at(army).owner())
		throw std::invalid_argument("Invalid owned battle leaf parents or updater context");
	auto trial = *this;
	const NodeId node = trial.ownedNodes.size();
	trial.ownedNodes.push_back({BonusNodeType::STACK_BATTLE, false, {}, {}, {}, {}, {}});
	trial.updaterContexts.push_back(std::move(context));
	OwnedBonusGraphMutationStorage storage(trial);
	BonusGraphMutation kernel(storage);
	kernel.attach(node, army);
	kernel.attachSource(node, creature);
	trial.committedRevision++;
	*this = std::move(trial);
	return node;
}

void BonusGraphMutationState::replaceOwnedValues(std::vector<std::shared_ptr<const Bonus>> values)
{
 if(values.size()!=ownedBonuses.size())throw std::invalid_argument("Owned graph value count changed");
 auto trial=*this;trial.ownedBonuses=std::move(values);++trial.committedRevision;*this=std::move(trial);
}
BonusGraphMutationState::BonusId BonusGraphMutationState::addOwnedRoot(NodeId node,const Bonus & value)
{
 auto trial=*this;const auto id=trial.ownedBonuses.size();trial.ownedBonuses.push_back(std::make_shared<const Bonus>(value));trial.addExistingBonus(node,id);*this=std::move(trial);return id;
}
