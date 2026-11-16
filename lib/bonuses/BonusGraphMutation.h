/*
 * BonusGraphMutation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <set>
#include <vector>

template<class Value, class Predicate>
void eraseBonusGraphMatches(std::vector<Value> & values, Predicate predicate)
{
	std::erase_if(values, predicate);
}

/// Storage supplies owned IDs or native handles; edge and materialization order are preserved.
template<class Storage>
class BonusGraphMutation
{
	Storage & storage;
	using Node = typename Storage::Node;
	using ConstNode = typename Storage::ConstNode;
	using Bonus = typename Storage::Bonus;

public:
	explicit BonusGraphMutation(Storage & storage): storage(storage) {}

	void redParents(ConstNode node, std::set<ConstNode> & out) const
	{
		for(auto parent : storage.inheritanceParents(node))
			if(storage.sourceOnly(parent))
				out.insert(parent);
		if(!storage.sourceOnly(node))
			for(auto child : storage.children(node))
				out.insert(child);
	}

	void redChildren(Node node, std::set<Node> & out) const
	{
		for(auto parent : storage.propagationParents(node))
			if(!storage.sourceOnly(parent))
				out.insert(parent);
		if(storage.sourceOnly(node))
			for(auto child : storage.children(node))
				out.insert(child);
	}

	void redAncestors(ConstNode node, std::set<ConstNode> & out) const
	{
		redParents(node, out);
		std::set<ConstNode> parents;
		redParents(node, parents);
		for(auto parent : parents)
			redAncestors(parent, out);
	}

	void propagate(Node node, const Bonus & bonus, ConstNode source)
	{
		if(storage.shouldAttach(bonus, node))
		{
			auto materialized = storage.materialize(bonus, source);
			storage.appendActive(node, materialized);
			storage.propagated(node, materialized);
		}
		std::set<Node> children;
		redChildren(node, children);
		for(auto child : children)
			propagate(child, bonus, source);
	}

	void unpropagate(Node node, const Bonus & bonus)
	{
		if(storage.shouldAttach(bonus, node))
		{
			auto & active = storage.active(node);
			const auto before = active.size();
			if(storage.hasPropagationUpdater(bonus))
			{
				storage.eraseActiveMatching(node, [&](const auto & candidate)
				{
					return storage.samePropagationUpdater(candidate, bonus);
				});
			}
			else
			{
				for(size_t i = 0; i < active.size(); ++i)
					if(active[i] == bonus)
					{
						storage.eraseActive(node, i);
						break;
					}
			}
			storage.unpropagated(node, bonus, before != active.size());
		}
		std::set<Node> children;
		redChildren(node, children);
		for(auto child : children)
			unpropagate(child, bonus);
	}

	void newDescendant(ConstNode node, Node descendant)
	{
		for(const auto & bonus : storage.exported(node))
			if(storage.hasPropagator(bonus))
				propagate(descendant, bonus, node);
		std::set<ConstNode> ancestors;
		redAncestors(node, ancestors);
		for(auto ancestor : ancestors)
			for(const auto & bonus : storage.exported(ancestor))
				if(storage.hasPropagator(bonus))
					propagate(descendant, bonus, node);
	}

	void removedDescendant(ConstNode node, Node descendant)
	{
		for(const auto & bonus : storage.exported(node))
			if(storage.hasPropagator(bonus))
				unpropagate(descendant, bonus);
		std::set<ConstNode> ancestors;
		redAncestors(node, ancestors);
		for(auto ancestor : ancestors)
			for(const auto & bonus : storage.exported(ancestor))
				if(storage.hasPropagator(bonus))
					unpropagate(descendant, bonus);
	}

	void exportBonus(Node node, const Bonus & bonus)
	{
		if(storage.hasPropagator(bonus))
			propagate(node, bonus, node);
		else
		{
			storage.appendActive(node, bonus);
			storage.changed(node);
		}
	}

	void removeBonus(Node node, const Bonus & bonus)
	{
		storage.eraseExportedFirst(node, bonus);
		if(storage.hasPropagator(bonus))
			unpropagate(node, bonus);
		else
		{
			auto & active = storage.active(node);
			for(size_t i = 0; i < active.size(); ++i)
				if(active[i] == bonus)
				{
					storage.eraseActive(node, i);
					break;
				}
			storage.changed(node);
		}
	}

	void attachSource(Node node, ConstNode parent)
	{
		storage.appendInheritance(node, parent);
		storage.beginEdgeChange();
		if(!storage.hypothetical(node) && storage.sourceOnly(parent))
			newDescendant(parent, node);
		storage.invalidate(node);
	}

	void attach(Node node, Node parent)
	{
		storage.appendPropagation(node, parent);
		attachSource(node, parent);
		if(!storage.hypothetical(node))
		{
			if(!storage.sourceOnly(parent))
				newDescendant(node, parent);
			storage.appendChild(parent, node);
		}
	}

	void detachSource(Node node, ConstNode parent)
	{
		storage.requireInheritance(node, parent);
		storage.beginEdgeChange();
		if(!storage.hypothetical(node) && storage.sourceOnly(parent))
			removedDescendant(parent, node);
		storage.eraseInheritance(node, parent);
		storage.invalidate(node);
	}

	void detach(Node node, Node parent)
	{
		storage.requirePropagation(node, parent);
		if(!storage.hypothetical(node) && !storage.sourceOnly(parent))
			removedDescendant(node, parent);
		detachSource(node, parent);
		storage.erasePropagation(node, parent);
		if(!storage.hypothetical(node))
			storage.eraseChild(parent, node);
		storage.changed(parent);
	}
};
