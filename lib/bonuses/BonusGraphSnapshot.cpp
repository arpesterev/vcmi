/*
 * BonusGraphSnapshot.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BonusGraphSnapshot.h"
#include "BonusParameters.h"
#include "CBonusSystemNode.h"
#include "Propagators.h"
#include "Updaters.h"

class BonusGraphSnapshotCapture
{
	std::map<const Bonus *, std::shared_ptr<Bonus>> bonusCopies;
	std::map<const IUpdater *, std::shared_ptr<IUpdater>> updaterCopies;
	std::map<const ILimiter *, std::shared_ptr<ILimiter>> limiterCopies;
	std::map<const IPropagator *, std::shared_ptr<IPropagator>> propagatorCopies;
	std::map<const BonusParameters *, std::shared_ptr<BonusParameters>> parameterCopies;
	std::set<const void *> inProgress;

	class CopyGuard
	{
		std::set<const void *> & active;
		const void * source;
	public:
		CopyGuard(std::set<const void *> & active, const void * source) : active(active), source(source)
		{
			if(!active.insert(source).second)
				throw std::runtime_error("Cyclic bonus component graph cannot be captured");
		}
		~CopyGuard() { active.erase(source); }
	};

	template<typename T, typename Base>
	static bool copyExactType(const std::shared_ptr<const Base> & source, std::shared_ptr<Base> & result)
	{
		const auto * sourcePtr = source.get();
		if(typeid(*sourcePtr) != typeid(T))
			return false;
		result = std::make_shared<T>(*static_cast<const T *>(source.get()));
		return true;
	}

	std::shared_ptr<IUpdater> updater(const TUpdaterPtr & source)
	{
		if(!source)
			return nullptr;
		const auto * sourcePtr = source.get();
		CopyGuard guard(inProgress, source.get());
		if(auto found = updaterCopies.find(source.get()); found != updaterCopies.end())
			return found->second;
		std::shared_ptr<IUpdater> result;
		if(!(copyExactType<IUpdater>(source, result)
			|| copyExactType<GrowsWithLevelUpdater>(source, result)
			|| copyExactType<TimesHeroLevelUpdater>(source, result)
			|| copyExactType<TimesStackSizeUpdater>(source, result)
			|| copyExactType<TimesArmySizeUpdater>(source, result)
			|| copyExactType<TimesStackLevelUpdater>(source, result)
			|| copyExactType<DivideStackLevelUpdater>(source, result)
			|| copyExactType<TimesHeroLevelDivideStackLevelUpdater>(source, result)
			|| copyExactType<OwnerUpdater>(source, result)
			|| copyExactType<CompositeUpdater>(source, result)))
			throw std::runtime_error("Unsupported bonus updater in snapshot: " + std::string(typeid(*sourcePtr).name()));
		updaterCopies.emplace(source.get(), result);
		if(auto * aggregate = dynamic_cast<CompositeUpdater *>(result.get()))
			for(auto & child : aggregate->updaters)
				child = updater(child);
		if(auto * specialty = dynamic_cast<TimesHeroLevelDivideStackLevelUpdater *>(result.get()))
			specialty->divideStackLevel = std::dynamic_pointer_cast<DivideStackLevelUpdater>(updater(specialty->divideStackLevel));
		return result;
	}

	std::shared_ptr<ILimiter> limiter(const TLimiterPtr & source)
	{
		if(!source)
			return nullptr;
		const auto * sourcePtr = source.get();
		CopyGuard guard(inProgress, source.get());
		if(auto found = limiterCopies.find(source.get()); found != limiterCopies.end())
			return found->second;
		std::shared_ptr<ILimiter> result;
		if(!(copyExactType<ILimiter>(source, result)
			|| copyExactType<AllOfLimiter>(source, result)
			|| copyExactType<AnyOfLimiter>(source, result)
			|| copyExactType<NoneOfLimiter>(source, result)
			|| copyExactType<CCreatureTypeLimiter>(source, result)
			|| copyExactType<HasAnotherBonusLimiter>(source, result)
			|| copyExactType<TerrainLimiter>(source, result)
			|| copyExactType<CreatureLevelLimiter>(source, result)
			|| copyExactType<FactionLimiter>(source, result)
			|| copyExactType<CreatureAlignmentLimiter>(source, result)
			|| copyExactType<OppositeSideLimiter>(source, result)
			|| copyExactType<RankRangeLimiter>(source, result)
			|| copyExactType<UnitOnHexLimiter>(source, result)
			|| copyExactType<UnitAdjacentLimiter>(source, result)
			|| copyExactType<HasChargesLimiter>(source, result)))
			throw std::runtime_error("Unsupported bonus limiter in snapshot: " + std::string(typeid(*sourcePtr).name()));
		limiterCopies.emplace(source.get(), result);
		if(auto * aggregate = dynamic_cast<AggregateLimiter *>(result.get()))
			for(auto & child : aggregate->limiters)
				child = limiter(child);
		return result;
	}

	std::shared_ptr<IPropagator> propagator(const TPropagatorPtr & source)
	{
		if(!source)
			return nullptr;
		const auto * sourcePtr = source.get();
		if(auto found = propagatorCopies.find(source.get()); found != propagatorCopies.end())
			return found->second;
		std::shared_ptr<IPropagator> result;
		if(!(copyExactType<IPropagator>(source, result) || copyExactType<CPropagatorNodeType>(source, result)))
			throw std::runtime_error("Unsupported bonus propagator in snapshot: " + std::string(typeid(*sourcePtr).name()));
		propagatorCopies.emplace(source.get(), result);
		return result;
	}

	std::shared_ptr<BonusParameters> parameters(const TBonusParametersPtr & source)
	{
		if(!source)
			return nullptr;
		CopyGuard guard(inProgress, source.get());
		if(auto found = parameterCopies.find(source.get()); found != parameterCopies.end())
			return found->second;
		auto result = std::make_shared<BonusParameters>(*source);
		parameterCopies.emplace(source.get(), result);
		if(auto * onCombat = std::get_if<BonusParametersOnCombatEvent>(&result->data_))
			for(auto & effect : onCombat->effects)
				if(auto * effectBonus = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
					effectBonus->bonus = bonus(effectBonus->bonus);
		return result;
	}

public:
	std::shared_ptr<Bonus> bonus(const std::shared_ptr<const Bonus> & source)
	{
		if(!source)
			return nullptr;
		const auto * sourcePtr = source.get();
		CopyGuard guard(inProgress, source.get());
		if(auto found = bonusCopies.find(source.get()); found != bonusCopies.end())
			return found->second;
		if(typeid(*sourcePtr) != typeid(Bonus))
			throw std::runtime_error("Unsupported bonus subtype in snapshot: " + std::string(typeid(*sourcePtr).name()));
		auto result = std::make_shared<Bonus>(*source);
		bonusCopies.emplace(source.get(), result);
		result->updater = updater(source->updater);
		result->propagationUpdater = updater(source->propagationUpdater);
		result->limiter = limiter(source->limiter);
		result->propagator = propagator(source->propagator);
		result->parameters = parameters(source->parameters);
		return result;
	}
};

BonusGraphSnapshot BonusGraphSnapshot::capture(const std::vector<const CBonusSystemNode *> & roots, const NodeVisitor & visitor, const std::vector<TConstBonusListPtr> & extraLists)
{
	std::set<const CBonusSystemNode *> component;
	std::vector<const CBonusSystemNode *> pending(roots);
	for(size_t i = 0; i < pending.size(); ++i)
	{
		const auto * node = pending[i];
		if(!node)
			throw std::invalid_argument("Null bonus graph root or edge");
		if(!component.insert(node).second)
			continue;
		for(const auto * parent : node->parentsToInherit)
			pending.push_back(parent);
		for(const auto * parent : node->parentsToPropagate)
			pending.push_back(parent);
		for(const auto * child : node->children)
			pending.push_back(child);
	}

	BonusGraphSnapshot result;
	result.capturedIdentity = std::make_shared<const CaptureIdentity>();
	std::map<const CBonusSystemNode *, NodeId> nodeIds;
	for(const auto * node : component)
		nodeIds.emplace(node, nodeIds.size());
	for(const auto * root : roots)
		result.capturedRoots.push_back(nodeIds.at(root));

	BonusGraphSnapshotCapture copies;
	std::map<const Bonus *, BonusId> bonusIds;
	auto captureBonuses = [&](const BonusList & source)
	{
		std::vector<BonusId> ids;
		for(const auto & bonus : source)
		{
			if(!bonus)
				throw std::invalid_argument("Null bonus in graph list");
			auto [entry, inserted] = bonusIds.emplace(bonus.get(), bonusIds.size());
			if(inserted)
				result.capturedBonuses.push_back(copies.bonus(bonus));
			ids.push_back(entry->second);
		}
		return ids;
	};
	auto captureEdges = [&](const auto & source)
	{
		std::vector<NodeId> ids;
		for(const auto * node : source)
			ids.push_back(nodeIds.at(node));
		return ids;
	};
	for(const auto * node : component)
	{
		result.capturedNodes.push_back({node->nodeType, node->isHypotheticNode,
			captureEdges(node->parentsToInherit), captureEdges(node->parentsToPropagate), captureEdges(node->children),
			captureBonuses(node->bonuses), captureBonuses(node->exportedBonuses)});
	}
	// Graph BonusIds remain independent from cache-only value roots.
	std::map<const Bonus *, ValueId> valueIds;
	for(const auto & [source, id] : bonusIds)
		valueIds.emplace(source, id);
	result.capturedArenaValues = result.capturedBonuses;
	for(BonusId id = 0; id < result.capturedBonuses.size(); ++id)
		result.capturedGraphValueIds.push_back(id);
	auto captureValues = [&](const BonusList & list)
	{
		std::vector<ValueId> ids;
		for(const auto & value : list)
		{
			if(!value)
				throw std::invalid_argument("Null bonus in native cache");
			auto [entry, inserted] = valueIds.emplace(value.get(), valueIds.size());
			if(inserted)
				result.capturedArenaValues.push_back(copies.bonus(value));
			ids.push_back(entry->second);
		}
		return ids;
	};
	for(const auto * node : component)
	{
		std::shared_lock lock(node->sync);
		NodeCache cache;
		cache.epoch = node->nodeChanged.load();
		cache.valid = node->cachedLast == cache.epoch;
		if(cache.valid)
		{
			cache.accepted = captureValues(node->cachedBonuses);
			std::map<std::string, TConstBonusListPtr> requests;
			for(const auto & request : node->cachedRequests)
				if(request.second.first == cache.epoch)
					requests.emplace(request.first, request.second.second);
			for(const auto & [key, list] : requests)
			{
				if(!list)
					throw std::invalid_argument("Null native request cache list");
				cache.requests.emplace(key, captureValues(*list));
			}
		}
		result.capturedNodeCaches.push_back(std::move(cache));
	}
	for(const auto & list : extraLists)
	{
		if(!list)
			throw std::invalid_argument("Null extra capture list");
		result.extraListValues.push_back(captureValues(*list));
	}
	// Fact visitors may query native bonuses. Preserve the cache epoch first.
	if(visitor)
		for(const auto * node : component)
			visitor(nodeIds.at(node), *node);
	return result;
}

const std::vector<BonusGraphSnapshot::Node> & BonusGraphSnapshot::nodes() const
{
	return capturedNodes;
}

const std::vector<BonusGraphSnapshot::NodeId> & BonusGraphSnapshot::roots() const
{
	return capturedRoots;
}

const std::vector<std::shared_ptr<const Bonus>> & BonusGraphSnapshot::bonuses() const
{
	return capturedBonuses;
}

bool BonusGraphSnapshot::sameOrigin(const BonusGraphSnapshot & other) const
{
	return capturedIdentity && capturedIdentity == other.capturedIdentity;
}

const std::vector<std::shared_ptr<const Bonus>> & BonusGraphSnapshot::capturedValues() const
{
	return capturedArenaValues;
}

const std::vector<BonusGraphSnapshot::ValueId> & BonusGraphSnapshot::graphValueIds() const
{
	return capturedGraphValueIds;
}

const std::vector<BonusGraphSnapshot::NodeCache> & BonusGraphSnapshot::nodeCaches() const
{
	return capturedNodeCaches;
}

std::vector<std::shared_ptr<Bonus>> BonusGraphSnapshot::detachValues(const std::vector<std::shared_ptr<const Bonus>> & values)
{
	BonusGraphSnapshotCapture copier;
	std::vector<std::shared_ptr<Bonus>> result;
	result.reserve(values.size());
	for(const auto & value : values)
		result.push_back(copier.bonus(value));
	return result;
}

const std::shared_ptr<const BonusGraphSnapshot::MutationBatch> & BonusGraphSnapshot::mutationBatch() const
{
	return capturedMutationBatch;
}

const std::vector<std::vector<BonusGraphSnapshot::ValueId>> & BonusGraphSnapshot::capturedExtraLists() const
{
	return extraListValues;
}
