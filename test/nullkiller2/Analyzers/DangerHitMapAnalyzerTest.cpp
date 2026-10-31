/* GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AI/Nullkiller2/Analyzers/DangerHitMapAnalyzer.h"
#include "AI/Nullkiller2/Behaviors/CaptureObjectsBehavior.h"
#include "nullkiller2/NullkillerTest.h"
#include "lib/gameState/CGameState.h"
#include "lib/networkPacks/PacksForClient.h"

#include <array>

TEST(Nullkiller2_Analyzers_DangerHitMap, urgencyCannotBeNegativeOnLongRoutes)
{
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, 0), 1000);
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, 1), 500);
	EXPECT_GT(NK2AI::discountedEnemyThreat(1000, 10), 0);
	EXPECT_FLOAT_EQ(NK2AI::discountedEnemyThreat(1000, std::numeric_limits<float>::quiet_NaN()), 1000);
}

TEST(Nullkiller2_Analyzers_DangerHitMap, checksBothKnownEnemiesAtEndOfTurn)
{
	NK2AI::HitMapNode map;
	map.fastestDanger.turn = 0;
	map.fastestDanger.danger = 2000;
	map.maximumDanger.turn = 0;
	map.maximumDanger.danger = 4000;
	CGHeroInstance hero(nullptr);
	ASSERT_TRUE(hero.setCreature(SlotID(0), CreatureID::ARCHER, 1));
	EXPECT_TRUE(NK2AI::isKnownUnsafeAtEndOfTurn(map, &hero, &hero, 0, 1.1f));
	map.fastestDanger.turn = 2;
	map.maximumDanger.turn = 2;
	EXPECT_FALSE(NK2AI::isKnownUnsafeAtEndOfTurn(map, &hero, &hero, 0, 1.1f));
}

TEST(Nullkiller2_Analyzers_DangerHitMap, findsPhysicalOvernightStopBeforeTurnChange)
{
	CGPathNode origin;
	origin.coord = int3(1, 1, 0);
	origin.turns = 0;
	CGPathNode dayZeroStop;
	dayZeroStop.coord = int3(3, 1, 0);
	dayZeroStop.turns = 0;
	dayZeroStop.theNodeBefore = &origin;
	CGPathNode followingMorning;
	followingMorning.coord = int3(4, 1, 0);
	followingMorning.turns = 1;
	followingMorning.theNodeBefore = &dayZeroStop;
	CGPathNode destination;
	destination.coord = int3(6, 1, 0);
	destination.turns = 1;
	destination.theNodeBefore = &followingMorning;

	const auto stops = NK2AI::collectPlannedOvernightStops(&destination);
	ASSERT_EQ(stops.size(), 1u);
	EXPECT_EQ(stops.front().first, dayZeroStop.coord);
	EXPECT_EQ(stops.front().second, 0);
}

TEST(Nullkiller2_Analyzers_DangerHitMap, laterNightsDoNotVetoTodaysSafeProgress)
{
	CGPathNode today;
	today.coord = int3(3, 1, 0);
	today.turns = 0;
	CGPathNode tomorrow;
	tomorrow.coord = int3(8, 1, 0);
	tomorrow.turns = 1;
	tomorrow.theNodeBefore = &today;
	CGPathNode destination;
	destination.coord = int3(12, 1, 0);
	destination.turns = 2;
	destination.theNodeBefore = &tomorrow;

	const auto stops = NK2AI::collectPlannedOvernightStops(&destination);
	ASSERT_EQ(stops.size(), 1u);
	EXPECT_EQ(stops.front().first, today.coord);
	EXPECT_EQ(stops.front().second, 0);
}

TEST(Nullkiller2_Analyzers_DangerHitMap, sameDayRouteHasNoIntermediateOvernightStop)
{
	CGPathNode origin;
	origin.turns = 0;
	CGPathNode destination;
	destination.turns = 0;
	destination.theNodeBefore = &origin;
	EXPECT_TRUE(NK2AI::collectPlannedOvernightStops(&destination).empty());
	EXPECT_TRUE(NK2AI::collectPlannedOvernightStops(nullptr).empty());
}

class Nullkiller2_DefenderInterception : public NullkillerTest
{
protected:
	void checkInterception(bool otherImmediateAttacker)
	{
		const PlayerColor player(0), enemy(1);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NK2DefenderInterception")
			.playerActive(player).playerActive(enemy)
			.town({6, 8, 0}, FactionID::CASTLE, player)
			.hero({5, 8, 0}, HeroTypeID(0), player)
			.heroGarrison({{CreatureID::ARCHER, 200}})
			.hero({10, 8, 0}, HeroTypeID(1), enemy)
			.heroGarrison({{CreatureID::ARCHER, 10}})
			.town({30, 30, 0}, FactionID::CASTLE, enemy);
		if(otherImmediateAttacker)
			builder.hero({8, 12, 0}, HeroTypeID(2), enemy)
				.heroGarrison({{CreatureID::ARCHER, 10}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		revealMap(player);
		const auto * defender = findHeroByOwner(player);
		const auto * attacker = findHeroAt({10, 8, 0});
		ASSERT_NE(defender, nullptr);
		ASSERT_NE(attacker, nullptr);
		for(const auto * hero : std::array<const CGHeroInstance *, 3>{defender, attacker, findHeroAt({8, 12, 0})})
		{
			if(hero)
			{
				SetMovePoints movement(hero->id, 2000);
				gameState()->apply(movement);
			}
		}
		const auto gateway = makeGateway(player);
		FowTilesType visibleTiles;
		NK2AI::foreach_visible_tile_pos(*gateway->cc, [&](const int3 & tile) { visibleTiles.insert(tile); });
		gateway->tileRevealed(visibleTiles);
		gateway->nullkiller->dangerHitMap->updateHitMap();
		preparePlanning(*gateway);
		const auto paths = gateway->nullkiller->pathfinder->getPathInfo(attacker->visitablePos(), false);
		const auto route = std::find_if(paths.begin(), paths.end(), [&](const NK2AI::AIPath & path)
		{
			return path.targetHero == defender && path.turn() == 0 && path.exchangeCount == 1
				&& !path.getFirstBlockedAction();
		});
		ASSERT_NE(route, paths.end());
		gateway->nullkiller->lockHero(defender, NK2AI::HeroLockedReason::DEFENCE);
		const auto goals = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({*route}, gateway->nullkiller.get(), attacker);
		ASSERT_EQ(goals.size(), 1u);
		EXPECT_EQ(goals.front()->invalid(), otherImmediateAttacker);

		auto overnight = *route;
		for(auto & node : overnight.nodes)
			node.turns = 1;
		const auto delayed = NK2AI::Goals::CaptureObjectsBehavior::getVisitGoals({overnight}, gateway->nullkiller.get(), attacker);
		ASSERT_EQ(delayed.size(), 1u);
		EXPECT_TRUE(delayed.front()->invalid());
	}
};

TEST_F(Nullkiller2_DefenderInterception, defenderCanDefeatTheThreatToday)
{
	checkInterception(false);
}

TEST_F(Nullkiller2_DefenderInterception, anotherImmediateAttackerKeepsDefenderAtHome)
{
	checkInterception(true);
}
