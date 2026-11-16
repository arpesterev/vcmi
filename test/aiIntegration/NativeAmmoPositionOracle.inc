/*
 * Native remaining-ammunition position and path-risk scenarios.
 * GPL-2.0-or-later. Included after the unchanged native battle fixture.
 */
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
enum class AmmoPositionMode { Exhausted, LoadedBlocked, Forgotten, Mixed, WideMixed, Supplied };
enum class AmmoPathMode { Exhausted, Loaded, Engaged, Blind, AlreadyMoved, Flying, LastShot };
enum class AmmoOracleClass { Usable, Identity, ShootLegal, Counter };

bool ammoOracleArcher(CPlayerBattleCallback & view, const CStack & unit, AmmoOracleClass mode)
{
	if(!unit.isShooter())
		return false;
	switch(mode)
	{
	case AmmoOracleClass::Usable: return unit.shots.canUse(1);
	case AmmoOracleClass::Identity: return true;
	case AmmoOracleClass::ShootLegal: return view.battleCanShoot(&unit);
	case AmmoOracleClass::Counter: return unit.shots.available() > 0;
	}
	throw std::logic_error("Unknown ammunition oracle class");
}

struct AmmoAttackOption
{
	const CStack * enemy = nullptr;
	BattleHex from;
	BattleHex aim;
	double hitValue = 0;
	double positionValue = 0;
};

// Independent native exchange estimate in the admitted ordinary-melee domain.
// No FHeroes2 scoring, classifier, position, or tie-break helper is called.
double ammoExchange(CPlayerBattleCallback & view, const CStack & attacker,
	const CStack & defender, BattleHex from, BattleHex aim, int distance)
{
	BattleAttackInfo exchange(&attacker, &defender, distance, false);
	exchange.attackerPos = from;
	exchange.defenderPos = aim;
	DamageEstimation response;
	const auto hit = view.battleEstimateDamage(exchange, &response);
	const auto loss = [](const CStack & victim, const DamageEstimation & estimate)
	{
		const double health = victim.getAvailableHealth();
		return victim.estimateCombatValue() * std::min(health,
			(static_cast<double>(estimate.damage.min) + estimate.damage.max) / 2.0) / health;
	};
	return loss(defender, hit) - loss(attacker, response);
}

std::vector<AmmoAttackOption> ammoPositionOracle(CPlayerBattleCallback & view,
	const CStack & attacker, AmmoOracleClass mode)
{
	const auto routes = view.getReachability(&attacker);
	auto positions = view.battleGetAvailableHexes(routes, &attacker, false);
	positions.sort([](BattleHex a, BattleHex b) { return a.toInt() < b.toInt(); });
	auto enemies = view.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	std::sort(enemies.begin(), enemies.end(), [](const CStack * a, const CStack * b) { return a->unitId() < b->unitId(); });
	std::vector<AmmoAttackOption> options;
	for(const auto * enemy : enemies)
	{
		if(!enemy->alive() || enemy->isInvincible() || !view.battleCanAttackUnit(&attacker, enemy))
			continue;
		for(const auto & from : positions)
		{
			if(!from.isAvailable() || !routes.isReachable(from))
				continue;
			std::optional<AmmoAttackOption> bestAim;
			for(const auto & aim : view.meleeAttackHexes(&attacker, enemy, from))
			{
				if(!view.battleCanAttackFromHex(positions, &attacker, aim, from))
					continue;
				const double value = ammoExchange(view, attacker, *enemy, from, aim,
					std::min<int>(routes.distances[from.toInt()], attacker.getMovementRange()));
				if(!bestAim || value > bestAim->hitValue)
					bestAim = AmmoAttackOption{enemy, from, aim, value, 0};
			}
			if(bestAim)
				options.push_back(*bestAim);
		}
	}
	for(auto & option : options)
	{
		std::optional<double> melee;
		double ranged = 0;
		std::set<uint32_t> uniqueTargets;
		for(const auto & neighbor : options)
		{
			if(neighbor.from != option.from)
				continue;
			if(!uniqueTargets.insert(neighbor.enemy->unitId()).second)
				throw std::logic_error("Oracle counted a wide target more than once");
			if(ammoOracleArcher(view, *neighbor.enemy, mode))
				ranged += neighbor.hitValue;
			else if(!melee || neighbor.hitValue > *melee)
				melee = neighbor.hitValue;
		}
		option.positionValue = melee.value_or(0) + ranged;
	}
	return options;
}

AmmoAttackOption ammoBestPosition(const std::vector<AmmoAttackOption> & options)
{
	AmmoAttackOption best;
	for(const auto & option : options)
		if(!best.enemy || option.positionValue > best.positionValue
			|| (std::fabs(option.positionValue - best.positionValue) < 0.001 && option.hitValue > best.hitValue))
			best = option;
	return best;
}

struct AmmoNestedBonusReader
{
	std::vector<const Bonus *> bonuses;
	void operator&(const BonusParameters::storage_type & value)
	{
		if(const auto * event = std::get_if<BonusParametersOnCombatEvent>(&value))
			for(const auto & effect : event->effects)
				if(const auto * nested = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
					if(nested->bonus) bonuses.push_back(nested->bonus.get());
	}
};

struct AmmoDivideChildReader
{
	const DivideStackLevelUpdater * child = nullptr;
	void operator&(TimesHeroLevelUpdater &) {}
	void operator&(const std::shared_ptr<const DivideStackLevelUpdater> & value) { child = value.get(); }
};

// This fixture accepts only component types whose complete semantic fields
// were audited against CURRENT serialization. Unknown types fail closed.
struct AmmoSnapshotDomain
{
	std::set<const Bonus *> bonuses;
	std::set<const IUpdater *> updaters;
	std::set<const ILimiter *> limiters;
	std::set<const IPropagator *> propagators;

	void updater(const IUpdater * value)
	{
		if(!value || !updaters.insert(value).second) return;
		const auto & type = typeid(*value);
		if(type == typeid(TimesStackSizeUpdater))
			throw std::invalid_argument("Ammunition snapshot excludes TimesStackSizeUpdater: stepValue is not serialized");
		if(type == typeid(CompositeUpdater))
		{
			for(const auto & child : static_cast<const CompositeUpdater *>(value)->updaters) updater(child.get());
		}
		else if(type == typeid(TimesHeroLevelDivideStackLevelUpdater))
		{
			AmmoDivideChildReader nested;
			const_cast<TimesHeroLevelDivideStackLevelUpdater *>(
				static_cast<const TimesHeroLevelDivideStackLevelUpdater *>(value))->serialize(nested);
			updater(nested.child);
		}
		else if(type != typeid(IUpdater) && type != typeid(GrowsWithLevelUpdater)
			&& type != typeid(TimesHeroLevelUpdater) && type != typeid(TimesArmySizeUpdater)
			&& type != typeid(TimesStackLevelUpdater) && type != typeid(DivideStackLevelUpdater)
			&& type != typeid(OwnerUpdater))
			throw std::invalid_argument("Ammunition snapshot contains an unaudited updater type");
		if(CTypeList::getInstance().getTypeID(value) == 0)
			throw std::invalid_argument("Ammunition snapshot updater lacks native serialization registration");
	}

	void limiter(const ILimiter * value)
	{
		if(!value || !limiters.insert(value).second) return;
		const auto & type = typeid(*value);
		if(type == typeid(AllOfLimiter) || type == typeid(AnyOfLimiter) || type == typeid(NoneOfLimiter))
		{
			for(const auto & child : static_cast<const AggregateLimiter *>(value)->limiters) limiter(child.get());
		}
		else if(type != typeid(ILimiter) && type != typeid(CCreatureTypeLimiter)
			&& type != typeid(HasAnotherBonusLimiter) && type != typeid(TerrainLimiter)
			&& type != typeid(CreatureLevelLimiter) && type != typeid(FactionLimiter)
			&& type != typeid(CreatureAlignmentLimiter) && type != typeid(OppositeSideLimiter)
			&& type != typeid(RankRangeLimiter) && type != typeid(UnitOnHexLimiter)
			&& type != typeid(UnitAdjacentLimiter) && type != typeid(HasChargesLimiter))
			throw std::invalid_argument("Ammunition snapshot contains an unaudited limiter type");
		if(CTypeList::getInstance().getTypeID(value) == 0)
			throw std::invalid_argument("Ammunition snapshot limiter lacks native serialization registration");
	}

	void propagator(const IPropagator * value)
	{
		if(!value || !propagators.insert(value).second) return;
		if(typeid(*value) != typeid(IPropagator) && typeid(*value) != typeid(CPropagatorNodeType))
			throw std::invalid_argument("Ammunition snapshot contains an unaudited propagator type");
		if(CTypeList::getInstance().getTypeID(value) == 0)
			throw std::invalid_argument("Ammunition snapshot propagator lacks native serialization registration");
	}

	void bonus(const Bonus * value)
	{
		if(!value || !bonuses.insert(value).second) return;
		if(typeid(*value) != typeid(Bonus))
			throw std::invalid_argument("Ammunition snapshot contains an unaudited Bonus subtype");
		updater(value->updater.get());
		updater(value->propagationUpdater.get());
		limiter(value->limiter.get());
		propagator(value->propagator.get());
		if(value->parameters)
		{
			AmmoNestedBonusReader nested;
			const_cast<BonusParameters *>(value->parameters.get())->serialize(nested);
			for(const auto * child : nested.bonuses) bonus(child);
		}
	}
};

std::vector<std::byte> ammoBonusValues(const std::vector<const Bonus *> & roots)
{
	AmmoSnapshotDomain domain;
	for(const auto * root : roots) domain.bonus(root);
	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.oser & roots;
	// Admitted components preserve typed IDs and audited nested fields.
	// Bonus::serialize omits bonusOwner. Record that field for every reachable bonus as well.
	std::vector<PlayerColor> owners;
	std::set<const Bonus *> visited;
	const std::function<void(const Bonus *)> appendOwner = [&](const Bonus * bonus)
	{
		if(!bonus || !visited.insert(bonus).second) return;
		owners.push_back(bonus->bonusOwner);
		if(bonus->parameters)
		{
			AmmoNestedBonusReader nested;
			const_cast<BonusParameters *>(bonus->parameters.get())->serialize(nested);
			for(const auto * child : nested.bonuses) appendOwner(child);
		}
	};
	for(const auto * bonus : roots) appendOwner(bonus);
	serializer.oser & owners;
	std::cout << "AMMO_SNAPSHOT_DOMAIN scope=audited_per_list bonuses=" << domain.bonuses.size()
		<< " updaters=" << domain.updaters.size() << " limiters=" << domain.limiters.size()
		<< " propagators=" << domain.propagators.size() << " rejected_components=0\n";
	return serializer.extractBuffer();
}

struct AmmoLiveSnapshot
{
	CStack * identity;
	JsonNode state;
	std::vector<std::byte> bonuses;
	std::vector<const Bonus *> bonusIdentities;
	int shots;
	bool canUse;
	bool hasCart;
};

std::vector<AmmoLiveSnapshot> ammoSnapshot(BattleInfo & info)
{
	std::vector<AmmoLiveSnapshot> result;
	for(const auto & unit : info.stacks)
	{
		std::vector<const Bonus *> bonusIdentities;
		const auto all = unit->getAllBonuses([](const Bonus *) { return true; });
		for(const auto & bonus : *all)
			bonusIdentities.push_back(bonus.get());
		auto bonuses = ammoBonusValues(bonusIdentities);
		result.push_back({unit.get(), unit->save(), std::move(bonuses), std::move(bonusIdentities), unit->shots.available(),
			unit->shots.canUse(1), unit->unitHasAmmoCart(unit.get())});
	}
	return result;
}

void ammoCheckSnapshot(const std::vector<AmmoLiveSnapshot> & before, BattleInfo & info)
{
	const auto after = ammoSnapshot(info);
	ASSERT_EQ(after.size(), before.size());
	for(size_t i = 0; i < before.size(); ++i)
	{
		EXPECT_EQ(after[i].identity, before[i].identity);
		EXPECT_EQ(after[i].state, before[i].state);
		EXPECT_EQ(after[i].bonuses, before[i].bonuses);
		EXPECT_EQ(after[i].bonusIdentities, before[i].bonusIdentities);
		EXPECT_EQ(after[i].shots, before[i].shots);
		EXPECT_EQ(after[i].canUse, before[i].canUse);
		EXPECT_EQ(after[i].hasCart, before[i].hasCart);
	}
}
}

