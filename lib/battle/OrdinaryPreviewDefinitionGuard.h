/* Ordinary preview seed qualification prototype. GPL-2.0-or-later. */
#pragma once

#include "../CCreatureHandler.h"
#include "../bonuses/BonusGraphMutation.h"
#include "../bonuses/BonusGraphSnapshot.h"
#include "../bonuses/BonusEvaluationSnapshot.h"
#include "../bonuses/BonusEvaluationFacts.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

/// Opt-in export isolation check. Does not own definitions or provide capture exclusion.
class OrdinaryPreviewDefinitionGuard final
{
public:
	using NodeId = BonusGraphSnapshot::NodeId;
	struct Definition
	{
		NodeId node;
		const Creature * native;
	};

private:
	friend class OrdinaryPreviewDefinitionGuardTestPeer;
	BonusGraphSnapshot capturedGraph;
	std::vector<Definition> capturedDefinitions;
	std::vector<NodeId> mutationClosure;

	struct Topology
	{
		using Node = NodeId;
		using ConstNode = NodeId;
		using Bonus = BonusGraphSnapshot::BonusId;
		const std::vector<BonusGraphSnapshot::Node> & nodes;
		const auto & inheritanceParents(Node node) const { return nodes.at(node).inheritanceParents; }
		const auto & propagationParents(Node node) const { return nodes.at(node).propagationParents; }
		const auto & children(Node node) const { return nodes.at(node).children; }
		bool sourceOnly(Node node) const
		{
			switch(nodes.at(node).type)
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
	};

	static void requireAcyclic(const std::vector<std::vector<NodeId>> & edges, const char * kind)
	{
		std::vector<size_t> incoming(edges.size());
		for(const auto & outgoing : edges)
			for(auto target : outgoing)
				++incoming.at(target);
		std::vector<NodeId> pending;
		for(NodeId node = 0; node < incoming.size(); ++node)
			if(incoming.at(node) == 0)
				pending.push_back(node);
		for(size_t index = 0; index < pending.size(); ++index)
			for(auto target : edges.at(pending.at(index)))
				if(--incoming.at(target) == 0)
					pending.push_back(target);
		if(pending.size() != edges.size())
			throw std::invalid_argument(std::string("Ordinary preview seed has cyclic ") + kind);
	}

	void validate(size_t firstMutationRoot)
	{
		const auto & nodes = capturedGraph.nodes();
		Topology topology{nodes};
		BonusGraphMutation kernel(topology);
		std::vector<std::vector<NodeId>> inheritance(nodes.size()), children(nodes.size()), propagation(nodes.size());
		for(NodeId id = 0; id < nodes.size(); ++id)
		{
			const auto & node = nodes.at(id);
			for(const auto * edges : {&node.inheritanceParents, &node.propagationParents, &node.children})
			{
				std::set<NodeId> unique;
				for(auto target : *edges)
					if(target >= nodes.size() || !unique.insert(target).second)
						throw std::invalid_argument("Ordinary preview seed has invalid or duplicate edges");
			}
			for(auto parent : node.propagationParents)
			{
				if(std::find(node.inheritanceParents.begin(), node.inheritanceParents.end(), parent) == node.inheritanceParents.end())
					throw std::invalid_argument("Ordinary preview propagation parent lacks inheritance edge");
				const auto & siblings = nodes.at(parent).children;
				if(!node.hypothetical && std::find(siblings.begin(), siblings.end(), id) == siblings.end())
					throw std::invalid_argument("Ordinary preview propagation parent lacks child edge");
			}
			for(auto child : node.children)
			{
				const auto & descendant = nodes.at(child);
				if(descendant.hypothetical || std::find(descendant.propagationParents.begin(), descendant.propagationParents.end(), id) == descendant.propagationParents.end())
					throw std::invalid_argument("Ordinary preview child lacks propagation parent");
			}
			inheritance.at(id) = node.inheritanceParents;
			children.at(id) = node.children;
			std::set<NodeId> redChildren;
			kernel.redChildren(id, redChildren);
			propagation.at(id).assign(redChildren.begin(), redChildren.end());
		}
		requireAcyclic(inheritance, "inheritance");
		requireAcyclic(children, "mutation children");
		requireAcyclic(propagation, "propagation");

		std::set<NodeId> definitions;
		std::set<BonusGraphSnapshot::BonusId> definitionExports;
		for(const auto & definition : capturedDefinitions)
		{
			definitions.insert(definition.node);
			for(auto bonus : nodes.at(definition.node).exported)
				definitionExports.insert(bonus);
		}
		std::vector<NodeId> pending;
		for(NodeId node = 0; node < nodes.size(); ++node)
			if(nodes.at(node).type == BonusNodeType::STACK_BATTLE)
				pending.push_back(node);
		for(size_t index = firstMutationRoot; index < capturedGraph.roots().size(); ++index)
			pending.push_back(capturedGraph.roots().at(index));
		std::set<NodeId> visited;
		for(size_t index = 0; index < pending.size(); ++index)
		{
			const auto id = pending.at(index);
			if(!visited.insert(id).second)
				continue;
			if(definitions.contains(id))
				throw std::invalid_argument("Creature definition is reachable through ordinary mutation children");
			const auto & node = nodes.at(id);
			for(auto bonus : node.exported)
				if(definitionExports.contains(bonus))
					throw std::invalid_argument("Ordinary mutable export aliases a Creature export");
			pending.insert(pending.end(), node.children.begin(), node.children.end());
		}
		mutationClosure.assign(visited.begin(), visited.end());
	}

	void captureDefinition(NodeId id, const CBonusSystemNode & node)
	{
		const auto * creature = dynamic_cast<const Creature *>(&node);
		if(node.getNodeType() == BonusNodeType::CREATURE || creature)
		{
			if(!creature || node.getNodeType() != BonusNodeType::CREATURE || typeid(node) != typeid(CCreature))
				throw std::invalid_argument("Ordinary preview seed requires native CCreature definitions");
			capturedDefinitions.push_back({id, creature});
		}
	}

	OrdinaryPreviewDefinitionGuard() = default;

public:
	/// Include every future definition and non-stack recursive mutation root explicitly.
	/// Exclude native writers/cache readers and retain the complete source for this call.
	static OrdinaryPreviewDefinitionGuard captureForOrdinarySeed(
		const std::vector<const CBonusSystemNode *> & seedRoots,
		const std::vector<const CBonusSystemNode *> & additionalMutationRoots,
		const std::vector<TConstBonusListPtr> & extraLists = {})
	{
		if(seedRoots.empty())
			throw std::invalid_argument("Ordinary preview seed needs capture roots");
		OrdinaryPreviewDefinitionGuard result;
		auto roots = seedRoots;
		roots.insert(roots.end(), additionalMutationRoots.begin(), additionalMutationRoots.end());
		result.capturedGraph = BonusGraphSnapshot::capture(roots, [&](NodeId id, const CBonusSystemNode & node)
		{
			result.captureDefinition(id, node);
		}, extraLists);
		result.validate(seedRoots.size());
		return result;
	}

	/// One evaluation capture binds definitions in its own visitor; no snapshot import.
	static std::pair<BonusEvaluationSnapshot, OrdinaryPreviewDefinitionGuard> captureEvaluationForOrdinarySeed(
		const std::vector<const CBonusSystemNode *> & seedRoots,
		const std::vector<const CBonusSystemNode *> & additionalMutationRoots,
		const std::vector<TConstBonusListPtr> & extraLists = {})
	{
		if(seedRoots.empty())
			throw std::invalid_argument("Ordinary preview seed needs capture roots");
		OrdinaryPreviewDefinitionGuard guard;
		auto roots = seedRoots;
		roots.insert(roots.end(), additionalMutationRoots.begin(), additionalMutationRoots.end());
		const auto facts = BonusEvaluationFactCapture::captureDefinitions();
		auto evaluation = BonusEvaluationSnapshot::captureObserved(roots,
			[&](const CBonusSystemNode & node) { return facts.captureNode(node); }, extraLists,
			[&](NodeId id, const CBonusSystemNode & node) { guard.captureDefinition(id, node); });
		guard.capturedGraph = evaluation.graph();
		guard.validate(seedRoots.size());
		return {std::move(evaluation), std::move(guard)};
	}

	const BonusGraphSnapshot & graph() const { return capturedGraph; }
	/// Borrowed identity, not an ownership or global LIBRARY generation lease.
	const std::vector<Definition> & definitions() const { return capturedDefinitions; }
	const std::vector<NodeId> & mutableNodes() const { return mutationClosure; }
};
