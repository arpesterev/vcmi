/*
 * CUnitStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitInfo.h"
#include "mock/mock_UnitEnvironment.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/bonuses/OwnedBonusGraphSession.h"
#include "../../lib/CCreatureHandler.h"

namespace test
{
using namespace ::testing;

static const int32_t DEFAULT_HP = 123;
static const int32_t DEFAULT_AMOUNT = 100;
static const int32_t DEFAULT_SPEED = 10;
static const BattleHex DEFAULT_POSITION = BattleHex(5, 5);
static const int DEFAULT_ATTACK = 58;
static const int DEFAULT_DEFENCE = 63;

class UnitStateTest : public Test
{
public:
	UnitInfoMock infoMock;
	UnitEnvironmentMock envMock;
	BonusBearerMock bonusMock;

	const CCreature * pikeman;

	battle::CUnitStateDetached subject;

	bool hasAmmoCart;

	UnitStateTest()
		:infoMock(),
		envMock(),
		bonusMock(),
		subject(&infoMock, &bonusMock),
		hasAmmoCart(false)
	{
		pikeman = CreatureID(0).toCreature();
	}

	void setDefaultExpectations()
	{
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY, DEFAULT_SPEED, BonusSourceID()));

		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::CREATURE_ABILITY, DEFAULT_ATTACK, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK)));
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::CREATURE_ABILITY, DEFAULT_DEFENCE, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));

		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, DEFAULT_HP, BonusSourceID()));

		EXPECT_CALL(infoMock, unitBaseAmount()).WillRepeatedly(Return(DEFAULT_AMOUNT));
		EXPECT_CALL(infoMock, unitType()).WillRepeatedly(Return(pikeman));

		EXPECT_CALL(envMock, unitHasAmmoCart(_)).WillRepeatedly(Return(hasAmmoCart));
	}

	void makeShooter(int32_t ammo)
	{
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOTS, BonusSource::CREATURE_ABILITY, ammo, BonusSourceID()));
	}

	void initUnit()
	{
		subject.localInit(&envMock);
		subject.position = DEFAULT_POSITION;
	}
};

TEST_F(UnitStateTest, initialRegular)
{
	setDefaultExpectations();
	initUnit();

	EXPECT_TRUE(subject.alive());
	EXPECT_TRUE(subject.ableToRetaliate());
	EXPECT_FALSE(subject.isGhost());
	EXPECT_FALSE(subject.isDead());
	EXPECT_FALSE(subject.isTurret());
	EXPECT_TRUE(subject.isValidTarget(true));
	EXPECT_TRUE(subject.isValidTarget(false));

	EXPECT_FALSE(subject.isClone());
	EXPECT_FALSE(subject.hasClone());

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_FALSE(subject.canShoot());
	EXPECT_FALSE(subject.isShooter());

	EXPECT_EQ(subject.getCount(), DEFAULT_AMOUNT);
	EXPECT_EQ(subject.getFirstHPleft(), DEFAULT_HP);
	EXPECT_EQ(subject.getKilled(), 0);
	EXPECT_EQ(subject.getAvailableHealth(), DEFAULT_HP * DEFAULT_AMOUNT);
	EXPECT_EQ(subject.getTotalHealth(), subject.getAvailableHealth());

	EXPECT_EQ(subject.getPosition(), DEFAULT_POSITION);

	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(123456), DEFAULT_SPEED);

	EXPECT_TRUE(subject.canMove());
	EXPECT_TRUE(subject.canMove(123456));
	EXPECT_FALSE(subject.defended());
	EXPECT_FALSE(subject.defended(123456));
	EXPECT_FALSE(subject.moved());
	EXPECT_FALSE(subject.moved(123456));
	EXPECT_TRUE(subject.willMove());
	EXPECT_TRUE(subject.willMove(123456));
	EXPECT_FALSE(subject.waited());
	EXPECT_FALSE(subject.waited(123456));

	EXPECT_EQ(subject.getTotalAttacks(true), 1);
	EXPECT_EQ(subject.getTotalAttacks(false), 1);
}

TEST_F(UnitStateTest, canShoot)
{
	setDefaultExpectations();
	makeShooter(1);
	initUnit();

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());

	subject.afterAttack(true, false);

	EXPECT_FALSE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());
}

TEST_F(UnitStateTest, canShootWithAmmoCart)
{
	hasAmmoCart = true;
	setDefaultExpectations();
	makeShooter(1);
	initUnit();

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());

	subject.afterAttack(true, false);

	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());
}

TEST_F(UnitStateTest, getAttack)
{
	setDefaultExpectations();

	EXPECT_EQ(subject.getAttack(false), DEFAULT_ATTACK);
	EXPECT_EQ(subject.getAttack(true), DEFAULT_ATTACK);
}

TEST_F(UnitStateTest, getDefense)
{
	setDefaultExpectations();

	EXPECT_EQ(subject.getDefense(false), DEFAULT_DEFENCE);
	EXPECT_EQ(subject.getDefense(true), DEFAULT_DEFENCE);
}

// Frenzy no longer moves these two - what it converts depends on the unit being attacked, so it is
// resolved by the damage calculator and covered by DamageCalculatorTest.

TEST_F(UnitStateTest, additionalAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 42);
	EXPECT_EQ(subject.getTotalAttacks(true), 42);
}

TEST_F(UnitStateTest, additionalMeleeAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 42);
	EXPECT_EQ(subject.getTotalAttacks(true), 1);
}

TEST_F(UnitStateTest, hypnotized)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 41, BonusSourceID());

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_TRUE(subject.isHypnotized());
}

TEST_F(UnitStateTest, additionalRangedAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 1);
	EXPECT_EQ(subject.getTotalAttacks(true), 42);
}

TEST_F(UnitStateTest, getMinDamage)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 30, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth);
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -20, BonusSourceID(), BonusCustomSubtype::creatureDamageMin);
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -10, BonusSourceID(), BonusCustomSubtype::creatureDamageMin);
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		bonusMock.addNewBonus(bonus);

	}

	EXPECT_EQ(subject.getMinDamage(false), 20);
	EXPECT_EQ(subject.getMinDamage(true), 10);
}

TEST_F(UnitStateTest, getMaxDamage)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 30, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth);
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -20, BonusSourceID(), BonusCustomSubtype::creatureDamageMax);
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -10, BonusSourceID(), BonusCustomSubtype::creatureDamageMax);
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getMaxDamage(false), 20);
	EXPECT_EQ(subject.getMaxDamage(true), 10);
}

TEST_F(UnitStateTest, BonusCacheSnapshotDoesNotInitializeOrWarmColdUnit)
{
 const auto before = subject.captureBonusCaches();
 subject.bindEnvironment(&envMock);
 const auto after = subject.captureBonusCaches();
 for(size_t i = 0; i < before.unit.size(); ++i)
 {
  EXPECT_EQ(before.unit[i].version, after.unit[i].version);
  EXPECT_EQ(before.unit[i].value, after.unit[i].value);
 }
 EXPECT_FALSE(after.speed.list);
 EXPECT_FALSE(after.immobilized.list);
 EXPECT_EQ(before.combat.entry.version, after.combat.entry.version);
 EXPECT_EQ(before.casts.version, after.casts.version);
 EXPECT_EQ(before.shots.version, after.shots.version);
}

TEST_F(UnitStateTest, BonusCacheRestorePreservesDestinationTarget)
{
 setDefaultExpectations();
 EXPECT_EQ(subject.getMaxHealth(), DEFAULT_HP);
 const auto snapshot = subject.captureBonusCaches();
 BonusBearerMock destinationBonuses;
 auto health = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
  BonusSource::CREATURE_ABILITY, 77, BonusSourceID());
 destinationBonuses.addNewBonus(health);
 battle::CUnitStateDetached destination(&infoMock, &destinationBonuses);
 destination.bindEnvironment(&envMock);
 destination.restoreBonusCaches(snapshot);
 // Its different epoch must query the destination bearer, never the source.
 EXPECT_EQ(destination.getMaxHealth(), 77);
 EXPECT_EQ(subject.getMaxHealth(), DEFAULT_HP);
}

TEST(BonusProxySnapshotTest, DerivedVariantAndWarmScalarSurviveRestore)
{
 BonusBearerMock first, second;
 BonusDerivedValueCache source(&first), destination(&second);
 int calls = 0;
 EXPECT_EQ(source.getValue([&] { ++calls; return 42; }, 17), 42);
 destination.restore(source.snapshot());
 EXPECT_EQ(destination.getValue([&] { ++calls; return 99; }, 17), 42);
 EXPECT_EQ(calls, 1);
 EXPECT_EQ(destination.getValue([&] { ++calls; return 99; }, 18), 99);
 EXPECT_EQ(calls, 2);
 first.addNewBonus(std::make_shared<Bonus>());
 EXPECT_EQ(destination.getValue([&] { ++calls; return 55; }, 18), 99);
 EXPECT_EQ(calls, 2);
}

TEST(BonusProxySnapshotTest, PerTurnMembershipSharesDurationAliasesAcrossCaptureAndFork)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
 auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID());
 bonus->turnsRemain = 10;
 native.addNewBonus(bonus);
 const auto selector = Selector::type()(BonusType::STACKS_SPEED);
 BonusCachePerTurn source(&native, selector, BonusCacheMode::VALUE);
 EXPECT_EQ(source.getValue(0), 7);
 auto state = source.snapshot();
 auto evaluation = BonusEvaluationSnapshot::capture({&native}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType(); facts.originalOwner = node.getOwner(); return facts;
 }, {state.list});
 const auto node = evaluation.graph().roots().front();
 auto session = std::make_shared<OwnedBonusGraphSession>(OwnedBonusGraphSession::capture(evaluation));
 state.membership = std::make_shared<const std::vector<OwnedBonusValueId>>(session->capturedMembership(evaluation.graph().capturedExtraLists().front()));
 state.list.reset();
 class Bearer final : public IBonusBearer
 {
 public:
  std::shared_ptr<OwnedBonusGraphSession> session;
  size_t node;
  Bearer(std::shared_ptr<OwnedBonusGraphSession> value, size_t id) : session(std::move(value)), node(id) {}
  TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override { return session->query(node, selector, key); }
  int32_t getTreeVersion() const override { return static_cast<int32_t>(session->nodeVersion(node)); }
 } owned(session, node), sibling(std::make_shared<OwnedBonusGraphSession>(session->fork()), node);
 BonusCachePerTurn projected(&owned, selector, BonusCacheMode::VALUE), forked(&sibling, selector, BonusCacheMode::VALUE);
 projected.bindOwnedSession(session, node); projected.restore(state);
 forked.bindOwnedSession(sibling.session, node); forked.restore(state);
 bonus->turnsRemain = 1;
 session->setTurnsRemain(evaluation.graph().nodes().at(node).exported.front(), 1);
 EXPECT_EQ(projected.getValue(0), source.getValue(0));
 EXPECT_EQ(projected.getValue(9), source.getValue(9));
 EXPECT_EQ(projected.getValue(9), 0);
 EXPECT_EQ(forked.getValue(9), 7);
}

}
