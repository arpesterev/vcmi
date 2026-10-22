/*
 * BattleCallerFixture.h, part of VCMI test experiments.
 * GPL-2.0-or-later; see license.txt in the VCMI repository root.
 *
 * Source-level draft. Not compiled or executed. Not a production header.
 */
#pragma once
#include "../mock/BattleFake.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_Environment.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "AI/BattleAI/BattleExchangeVariant.h"
#include "lib/CCreatureHandler.h"
#include "lib/json/JsonNode.h"

namespace test
{
namespace callerprobe
{
using namespace ::testing;

struct UnitSpec
{
	uint32_t id;
	BattleSide side;
	BattleHex position;
	int count;
	int health;
	int damage;
	int speed;
	bool shooter;
};

struct SourceUnit
{
	NiceMock<UnitInfoMock> info;
	BonusBearerMock bonuses;
	NiceMock<UnitEnvironmentMock> environment;
	::battle::CUnitStateDetached state;

	explicit SourceUnit(const UnitSpec & spec) : state(&info, &bonuses)
	{
		ON_CALL(info, unitId()).WillByDefault(Return(spec.id));
		ON_CALL(info, unitSide()).WillByDefault(Return(spec.side));
		ON_CALL(info, unitOwner()).WillByDefault(Return(PlayerColor(spec.side == BattleSide::ATTACKER ? 0 : 1)));
		ON_CALL(info, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(0).toCreature()));
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(spec.count));
		ON_CALL(environment, unitHasAmmoCart(_)).WillByDefault(Return(false));
		add(BonusType::STACK_HEALTH, spec.health);
		add(BonusType::CREATURE_DAMAGE, spec.damage, BonusCustomSubtype::creatureDamageBoth);
		add(BonusType::STACKS_SPEED, spec.speed);
		add(BonusType::NO_MELEE_PENALTY, 1);
		// No ranged-distance penalty: mode tests isolate retaliation, not cached damage.
		add(BonusType::NO_DISTANCE_PENALTY, 1);
		if(spec.shooter)
		{
			add(BonusType::SHOOTER, 1);
			add(BonusType::SHOTS, 100);
		}
		state.localInit(&environment);
		state.position = spec.position;
	}

	void add(BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID())
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			type, BonusSource::CREATURE_ABILITY, value, BonusSourceID(), subtype));
	}
};

// Lifetime order is deliberate: child battle/cache die before their borrowed sources.
struct Scenario
{
	std::shared_ptr<NiceMock<EnvironmentMock>> environment = std::make_shared<NiceMock<EnvironmentMock>>();
	BonusBearerMock battleBonuses;
	std::vector<std::unique_ptr<SourceUnit>> sources;
	std::shared_ptr<NiceMock<test::battle::BattleFake>> callback;
	std::shared_ptr<HypotheticBattle> state;
	DamageCache originalCache;
	DamageCache cache{&originalCache};
	size_t populatedPairs = 0;

	explicit Scenario(const std::vector<UnitSpec> & specs)
	{
#if SCRIPTING_ENABLED
		callback = std::make_shared<NiceMock<test::battle::BattleFake>>(std::shared_ptr<scripting::PoolMock>());
#else
		callback = std::make_shared<NiceMock<test::battle::BattleFake>>();
#endif
		for(const auto & spec : specs)
			sources.emplace_back(new SourceUnit(spec));
		ON_CALL(*callback, getUnitsIf(_)).WillByDefault(Invoke(
			[this](const ::battle::UnitFilter & filter)
			{
				::battle::Units units;
				for(const auto & source : sources)
					if(filter(&source->state))
						units.push_back(&source->state);
				return units;
			}));
		ON_CALL(*callback, getActiveStackID()).WillByDefault(Return(static_cast<int32_t>(specs.front().id)));
		ON_CALL(*callback, getBonusBearer()).WillByDefault(Return(&battleBonuses));
		ON_CALL(*callback, getSidePlayer(_)).WillByDefault(Invoke(
			[](BattleSide side) { return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); }));
		ON_CALL(*callback, getTacticDist()).WillByDefault(Return(0));
		ON_CALL(*callback, getRound()).WillByDefault(Return(1));
		callback->setupEmptyBattlefield();
		state = std::make_shared<HypotheticBattle>(environment.get(), callback);
		for(const auto & spec : specs)
			state->getForUpdate(spec.id);
	}

	Scenario(const Scenario &) = delete;
	Scenario & operator=(const Scenario &) = delete;

	void populateCache()
	{
		const auto units = state->battleAliveUnits();
		for(const auto * attacker : units)
			for(const auto * defender : units)
				if(attacker->unitSide() != defender->unitSide())
				{
					originalCache.cacheDamage(attacker, defender, state);
					cache.cacheDamage(attacker, defender, state);
					++populatedPairs;
				}
	}

	std::vector<JsonNode> snapshot()
	{
		std::vector<JsonNode> result;
		for(const auto & source : sources)
		{
			JsonNode value;
			source->state.save(value);
			result.push_back(value);
			state->getForUpdate(source->state.unitId())->save(value);
			result.push_back(value);
		}
		return result;
	}

	BattleExchangeEvaluator evaluator(int rounds)
	{
		return BattleExchangeEvaluator(callback, environment, 1.0f, rounds);
	}
};

inline std::vector<UnitSpec> modeProbeSpecs()
{
	// A and B are melee candidates near D. E is an unblocked ranged enemy.
	return {
		{1, BattleSide::ATTACKER, BattleHex(5, 5), 10, 100, 10, 20, false},
		{2, BattleSide::ATTACKER, BattleHex(6, 6), 10, 100, 9, 4, false},
		{3, BattleSide::DEFENDER, BattleHex(6, 5), 10, 100, 4, 5, false},
		{4, BattleSide::DEFENDER, BattleHex(12, 3), 10, 100, 10, 15, true}
	};
}

inline std::vector<UnitSpec> stateProbeSpecs(int attackerDamage, int defenderDamage)
{
	return {
		{1, BattleSide::ATTACKER, BattleHex(5, 5), 10, 100, attackerDamage, 20, false},
		{2, BattleSide::ATTACKER, BattleHex(6, 6), 10, 100, 9, 4, false},
		{3, BattleSide::DEFENDER, BattleHex(6, 5), 10, 100, defenderDamage, 15, false}
	};
}

inline std::vector<UnitSpec> benchmarkSpecs(int totalStacks, bool mixed)
{
	std::vector<UnitSpec> specs;
	const int perSide = totalStacks / 2;
	for(int side = 0; side < 2; ++side)
		for(int i = 0; i < perSide; ++i)
		{
			const int x = side == 0 ? 3 + i / 7 : 11 - i / 7;
			const int y = 2 + i % 7;
			specs.push_back({static_cast<uint32_t>(specs.size() + 1),
				side == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER,
				BattleHex(x, y), 20, 100, 8 + i % 5, 8 + i % 4,
				mixed && i % 3 == 0});
		}
	return specs;
}
}
}

