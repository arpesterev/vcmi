#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/json/JsonNode.h"

class HypotheticCopyStateTest : public BattleTestFixture
{
public:
 testing::StrictMock<EnvironmentMock> environment;
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 std::shared_ptr<HypotheticBattle> project()
 {
  return std::make_shared<HypotheticBattle>(&environment,
   std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER)));
 }
 CStack * add(int x) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x,5), 10); }
 void compare(const std::shared_ptr<StackWithBonuses> & actual, const std::shared_ptr<StackWithBonuses> & old)
 {
  EXPECT_EQ(actual->save(), old->save());
  EXPECT_EQ(actual->unitId(), old->unitId());
  EXPECT_EQ(actual->unitSide(), old->unitSide());
  EXPECT_EQ(actual->unitOwner(), old->unitOwner());
  EXPECT_EQ(actual->unitType(), old->unitType());
  EXPECT_EQ(actual->getTreeVersion(), old->getTreeVersion());
  EXPECT_EQ(actual->isHypnotized(), old->isHypnotized());
  EXPECT_EQ(actual->shots.available(), old->shots.available());
  EXPECT_EQ(actual->shots.isLimited(), old->shots.isLimited());
 }
};

TEST_F(HypotheticCopyStateTest, FirstCopyMatchesLegacyScalarAssignment)
{
 auto * source = add(5); auto incoming = source->acquireState();
 incoming->setPosition(BattleHex(8,5)); incoming->afterAttack(true, false);
 int64_t damage = incoming->getMaxHealth(); incoming->damage(damage);
 const auto nativeBefore = source->save();
 auto before = project(); auto after = project();
 auto old = before->getForUpdate(source->unitId()); *old = *incoming;
 auto copied = after->copyUnitState(source->unitId(), *incoming);
 compare(copied, old); EXPECT_EQ(source->save(), nativeBefore);
 EXPECT_EQ(after->getForUpdate(source->unitId()).get(), copied.get());
}

TEST_F(HypotheticCopyStateTest, RepeatedAndSelfCopyPreserveAliasAndRevision)
{
 auto * source = add(5); auto before = project(); auto after = project();
 auto old = before->getForUpdate(source->unitId()); auto retained = after->getForUpdate(source->unitId());
 auto incoming = source->acquireState(); incoming->cloneID = 17; incoming->ghostPending = true;
 incoming->setTurnStatus(battle::UnitTurnStatus::WAITING);
 const auto revision = retained->getTreeVersion();
 for(int i=0;i<2;++i)
 {
  *old = *incoming; auto copied = after->copyUnitState(source->unitId(), *incoming);
  EXPECT_EQ(copied.get(), retained.get()); compare(copied, old);
 }
 *old = *old; EXPECT_EQ(after->copyUnitState(source->unitId(), *retained).get(), retained.get());
 compare(retained, old); EXPECT_EQ(retained->getTreeVersion(), revision);
}

TEST_F(HypotheticCopyStateTest, DestinationIdentityAndInheritedControlArePreserved)
{
 auto * source = add(5);
 auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:woodElf"), BattleHex(7,5), 10);
 auto destinationMarker = std::make_shared<Bonus>(BonusDuration::PERMANENT,
  BonusType::MORALE, BonusSource::OTHER, 13, BonusSourceID());
 auto incomingMarker = std::make_shared<Bonus>(BonusDuration::PERMANENT,
  BonusType::LUCK, BonusSource::OTHER, 29, BonusSourceID());
 source->addNewBonus(destinationMarker); other->addNewBonus(incomingMarker);
 const CSelector destinationSelected([](const Bonus * bonus) { return bonus->type == BonusType::MORALE && bonus->source == BonusSource::OTHER && bonus->val == 13; });
 const CSelector incomingSelected([](const Bonus * bonus) { return bonus->type == BonusType::LUCK && bonus->source == BonusSource::OTHER && bonus->val == 29; });
 attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
  BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
 ASSERT_TRUE(source->isHypnotized()); ASSERT_FALSE(other->isHypnotized());
 ASSERT_EQ(source->getAllBonuses(destinationSelected)->size(), 1);
 ASSERT_TRUE(source->getAllBonuses(incomingSelected)->empty());
 ASSERT_EQ(other->getAllBonuses(incomingSelected)->size(), 1);
 ASSERT_TRUE(other->getAllBonuses(destinationSelected)->empty());
 auto incoming = other->acquireState(); incoming->setPosition(BattleHex(10,5));
 auto before = project(); auto after = project();
 auto old = before->getForUpdate(source->unitId()); *old = *incoming;
 auto copied = after->copyUnitState(source->unitId(), *incoming);
 compare(copied, old); EXPECT_EQ(copied->unitId(), source->unitId()); EXPECT_TRUE(copied->isHypnotized());
 EXPECT_EQ(copied->getAllBonuses(destinationSelected)->size(), 1);
 EXPECT_TRUE(copied->getAllBonuses(incomingSelected)->empty());
 EXPECT_FALSE(incoming->isHypnotized()); EXPECT_FALSE(other->isHypnotized());
}

TEST_F(HypotheticCopyStateTest, ScalarDeathDoesNotAddLifecycleOrSiblingMutation)
{
 auto * source = add(5); auto * clone = add(7); source->cloneID = clone->unitId(); clone->cloned = true;
 auto incoming = source->acquireState(); int64_t damage = incoming->getAvailableHealth(); incoming->damage(damage);
 auto before = project(); auto after = project(); auto sibling = project();
 auto old = before->getForUpdate(source->unitId()); *old = *incoming;
 auto copied = after->copyUnitState(source->unitId(), *incoming);
 compare(copied, old); EXPECT_FALSE(copied->alive());
 EXPECT_EQ(after->battleGetUnitByID(clone->unitId())->acquireState()->save(), before->battleGetUnitByID(clone->unitId())->acquireState()->save());
 EXPECT_EQ(sibling->battleGetUnitByID(source->unitId())->acquireState()->save(), source->save());
 EXPECT_TRUE(source->alive()); EXPECT_TRUE(clone->alive());
}

TEST_F(HypotheticCopyStateTest, DestinationShotEnvironmentAndExpiryOrderMatchLegacy)
{
 auto * source = add(5); addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 ASSERT_FALSE(source->shots.isLimited());
 source->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_ATTACK,
  BonusType::MORALE, BonusSource::OTHER, 13, BonusSourceID()));
 source->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
  BonusType::LUCK, BonusSource::OTHER, 29, BonusSourceID()));
 const CSelector attackEffect([](const Bonus * bonus) { return bonus->type == BonusType::MORALE && bonus->source == BonusSource::OTHER && bonus->val == 13; });
 const CSelector ownAttackEffect([](const Bonus * bonus) { return bonus->type == BonusType::LUCK && bonus->source == BonusSource::OTHER && bonus->val == 29; });
 ASSERT_EQ(source->getAllBonuses(attackEffect)->size(), 1);
 ASSERT_EQ(source->getAllBonuses(ownAttackEffect)->size(), 1);
 auto incoming = source->acquireState();
 auto before = project(); auto after = project();
 auto old = before->getForUpdate(source->unitId()); *old = *incoming;
 auto copied = after->copyUnitState(source->unitId(), *incoming);
 compare(copied, old);
 EXPECT_EQ(copied->getAllBonuses(attackEffect)->size(), 1);
 EXPECT_EQ(copied->getAllBonuses(ownAttackEffect)->size(), 1);
 old->afterAttack(true, false); copied->afterAttack(true, false); compare(copied, old);
 EXPECT_EQ(copied->getAllBonuses(attackEffect)->size(), 1);
 EXPECT_EQ(copied->getAllBonuses(ownAttackEffect)->size(), 1);
 old->removeUnitBonus(Bonus::UntilAttack); copied->removeUnitBonus(Bonus::UntilAttack); compare(copied, old);
 EXPECT_TRUE(copied->getAllBonuses(attackEffect)->empty());
 EXPECT_EQ(copied->getAllBonuses(ownAttackEffect)->size(), 1);
 EXPECT_TRUE(old->getAllBonuses(attackEffect)->empty());
 EXPECT_EQ(old->getAllBonuses(ownAttackEffect)->size(), 1);
 old->removeUnitBonus(Bonus::UntilOwnAttack); copied->removeUnitBonus(Bonus::UntilOwnAttack); compare(copied, old);
 EXPECT_TRUE(copied->getAllBonuses(ownAttackEffect)->empty());
 EXPECT_TRUE(old->getAllBonuses(ownAttackEffect)->empty());
 EXPECT_EQ(source->getAllBonuses(attackEffect)->size(), 1);
 EXPECT_EQ(source->getAllBonuses(ownAttackEffect)->size(), 1);
}
