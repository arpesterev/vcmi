#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "server/CGameHandler.h"

class OwnedArenaNativeParityTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 std::shared_ptr<HypotheticBattle> project() {
  return HypotheticBattle::captureOwnedFixture(*battle());
 }
 CStack * add(int x) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x,5), 10); }
 std::shared_ptr<CBattleCallback> callback;
 void compareRemoval(bool original, bool ghost, bool multiple) {
  auto * source = add(5); auto * clone = add(7); auto * second = add(9);
  source->cloneID = clone->unitId(); clone->cloned = true;
  if(multiple) second->cloneID = clone->unitId();
  if(ghost) clone->ghost = true;
  auto parent = project();
  auto child = parent->forkOwnedFixture();
  const auto id = original ? source->unitId() : clone->unitId();
  const auto version = source->getTreeVersion();
  child->removeUnit(id);
  auto childSource = child->battleGetUnitByID(source->unitId())->acquireState();
  auto childSecond = child->battleGetUnitByID(second->unitId())->acquireState();
  auto childClone = child->battleGetUnitByID(clone->unitId())->acquireState();
  EXPECT_EQ(source->cloneID, clone->unitId());
  EXPECT_EQ(parent->battleGetUnitByID(source->unitId())->acquireState()->cloneID, clone->unitId());
  EXPECT_EQ(source->getTreeVersion(), version);
  EXPECT_EQ(clone->ghost, ghost);
  battle()->removeUnit(id);
  EXPECT_EQ(childSource->cloneID, source->cloneID);
  EXPECT_EQ(childSecond->cloneID, second->cloneID);
  EXPECT_EQ(childClone->ghost, clone->ghost);
 }
};
TEST_F(OwnedArenaNativeParityTest, DirectCloneRemovalClearsOriginalLink) { compareRemoval(false,false,false); }
TEST_F(OwnedArenaNativeParityTest, OriginalRemovalAlsoRemovesClone) { compareRemoval(true,false,false); }
TEST_F(OwnedArenaNativeParityTest, CloneRemovalClearsAllReverseLinks) { compareRemoval(false,false,true); }
TEST_F(OwnedArenaNativeParityTest, AlreadyGhostRemovalKeepsLinks) { compareRemoval(false,true,true); }
TEST_F(OwnedArenaNativeParityTest, OriginalRemovalClearsOtherCloneLinks) { compareRemoval(true,false,true); }
TEST_F(OwnedArenaNativeParityTest, RemovingStackDetachesInheritedHypnotize)
{
 auto * stack = add(5);
 auto effect = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
 attackerSideHero->addNewBonus(effect);
 ASSERT_TRUE(stack->isHypnotized());
 auto projection = project();
 const auto version = stack->getTreeVersion();
 projection->removeUnit(stack->unitId());
 const bool projected = projection->battleGetUnitByID(stack->unitId())->isHypnotized();
 EXPECT_TRUE(stack->isHypnotized());
 EXPECT_FALSE(stack->ghost);
 EXPECT_EQ(stack->getTreeVersion(), version);
 battle()->removeUnit(stack->unitId());
 EXPECT_FALSE(stack->isHypnotized());
 EXPECT_EQ(projected, stack->isHypnotized());
}
TEST_F(OwnedArenaNativeParityTest, ProjectionPreservesEnumerationAfterMutation)
{
 auto * first = add(5); add(7); add(9);
 auto projection = project();
 auto native = battle()->getUnitsIf([](const battle::Unit *) { return true; });
 projection->moveUnit(first->unitId(), BattleHex(5,6));
 auto projected = projection->getUnitsIf([](const battle::Unit *) { return true; });
 ASSERT_EQ(native.size(), projected.size());
 for(size_t i=0;i<native.size();++i) EXPECT_EQ(native[i]->unitId(), projected[i]->unitId());
 EXPECT_EQ(first->getPosition(), BattleHex(5,5));
}

TEST_F(OwnedArenaNativeParityTest, LiveAmmoCartMakesShotsUnlimited)
{
 auto * shooter = add(5);
 addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 ASSERT_TRUE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); auto copy = p->getForUpdate(shooter->unitId());
 EXPECT_FALSE(shooter->shots.isLimited());
 EXPECT_EQ(copy->shots.isLimited(), shooter->shots.isLimited());
}
TEST_F(OwnedArenaNativeParityTest, DeadFirstCartSuppressesLiveCartAndArtifact)
{
 auto * shooter = add(5);
 auto * first = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(9,5), 1);
 int64_t damage = first->getAvailableHealth(); first->damage(damage);
 giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 ASSERT_FALSE(first->alive()); ASSERT_FALSE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); EXPECT_FALSE(p->unitHasAmmoCart(p->getForUpdate(shooter->unitId()).get()));
}
TEST_F(OwnedArenaNativeParityTest, GhostFirstCartSuppressesLiveCartAndArtifact)
{
 auto * shooter = add(5);
 auto * first = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(9,5), 1);
 battle()->removeUnit(first->unitId());
 giveArtifact(attackerSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 ASSERT_TRUE(first->ghost); ASSERT_FALSE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); EXPECT_FALSE(p->unitHasAmmoCart(p->getForUpdate(shooter->unitId()).get()));
}
TEST_F(OwnedArenaNativeParityTest, HypnotizedShooterUsesOriginalSideCart)
{
 auto * shooter = add(5); hypnotize(shooter);
 addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 ASSERT_TRUE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); EXPECT_EQ(p->unitHasAmmoCart(p->getForUpdate(shooter->unitId()).get()), shooter->unitHasAmmoCart(shooter));
}
TEST_F(OwnedArenaNativeParityTest, HypnotizedEnemyCartServesOriginalShooterSide)
{
 auto * shooter = add(5);
 auto * cart = addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(9,5), 1); hypnotize(cart);
 ASSERT_TRUE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); EXPECT_EQ(p->unitHasAmmoCart(p->getForUpdate(shooter->unitId()).get()), shooter->unitHasAmmoCart(shooter));
}
TEST_F(OwnedArenaNativeParityTest, ArtifactFallbackUsesEffectiveShooterOwner)
{
 auto * shooter = add(5); hypnotize(shooter);
 giveArtifact(defenderSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 ASSERT_TRUE(shooter->unitHasAmmoCart(shooter));
 auto p = project(); EXPECT_EQ(p->unitHasAmmoCart(p->getForUpdate(shooter->unitId()).get()), shooter->unitHasAmmoCart(shooter));
}

#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
TEST_F(OwnedArenaNativeParityTest, RemovingPropagatingSourceWithdrawsControlFromOtherStack)
{
 auto * source = add(5); auto * recipient = add(7);
 auto effect = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
 effect->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 source->addNewBonus(effect);
 attackerSideHero->nodeHasChanged();
 ASSERT_TRUE(recipient->isHypnotized());
 auto p = project(); const auto version = recipient->getTreeVersion();
 p->removeUnit(source->unitId());
 const bool projected = p->battleGetUnitByID(recipient->unitId())->isHypnotized();
 EXPECT_FALSE(source->ghost); EXPECT_TRUE(recipient->isHypnotized()); EXPECT_EQ(recipient->getTreeVersion(), version);
 battle()->removeUnit(source->unitId());
 ASSERT_FALSE(recipient->isHypnotized());
 EXPECT_EQ(projected, recipient->isHypnotized());
}
TEST_F(OwnedArenaNativeParityTest, MaterializedPropagationRetainsHistoricalValueAndIdentity)
{
 auto * source = add(5); auto * recipient = add(7); auto * secondRecipient = add(9);
 auto effect = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 1, BonusSourceID());
 effect->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 auto liveUpdater = std::make_shared<TimesStackSizeUpdater>();
 effect->propagationUpdater = liveUpdater;
 source->addNewBonus(effect);
 attackerSideHero->nodeHasChanged();
 auto selected = Selector::type()(BonusType::STACKS_SPEED).And(Selector::sourceType()(BonusSource::OTHER));
 auto before = recipient->getAllBonuses(selected);
 ASSERT_EQ(before->size(), 1); const auto value = before->front()->val;
 ASSERT_NE(before->front().get(), effect.get());
 int64_t damage = source->getMaxHealth(); source->damage(damage);
 ASSERT_EQ(source->getCount(), 9);
 auto native = recipient->getAllBonuses(selected);
 EXPECT_EQ(native->front().get(), before->front().get()); EXPECT_EQ(native->front()->val, value);
 auto p = project(); auto projected = p->getForUpdate(recipient->unitId())->getAllBonuses(selected);
 ASSERT_EQ(projected->size(), 1); EXPECT_EQ(projected->front()->val, value);
 // Snapshot components preserve alias partitions, not addresses into mutable live state.
 EXPECT_NE(projected->front()->propagationUpdater.get(), before->front()->propagationUpdater.get());
 auto secondNative = secondRecipient->getAllBonuses(selected);
 ASSERT_EQ(secondNative->size(), 1);
 EXPECT_EQ(secondNative->front()->val, value);
 EXPECT_EQ(secondNative->front()->propagationUpdater.get(), before->front()->propagationUpdater.get());
 auto secondProjected = p->getForUpdate(secondRecipient->unitId())->getAllBonuses(selected);
 ASSERT_EQ(secondProjected->size(), 1);
 EXPECT_EQ(secondProjected->front()->val, value);
 EXPECT_EQ(secondProjected->front()->propagationUpdater.get(), projected->front()->propagationUpdater.get());
 const auto * ownedUpdater = dynamic_cast<const TimesStackSizeUpdater *>(projected->front()->propagationUpdater.get());
 ASSERT_NE(ownedUpdater, nullptr);
 EXPECT_EQ(ownedUpdater->createUpdatedBonus(effect, *source)->val, 9);
 *liveUpdater = TimesStackSizeUpdater(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1, 3);
 EXPECT_EQ(liveUpdater->createUpdatedBonus(effect, *source)->val, 27);
 EXPECT_EQ(ownedUpdater->createUpdatedBonus(effect, *source)->val, 9);
 for(auto * unit : {recipient, secondRecipient})
 {
  auto retained = p->getForUpdate(unit->unitId())->getAllBonuses(selected);
  ASSERT_EQ(retained->size(), 1);
  EXPECT_EQ(retained->front()->val, value);
  EXPECT_EQ(retained->front()->propagationUpdater.get(), ownedUpdater);
 }
 auto retainedNative = recipient->getAllBonuses(selected);
 ASSERT_EQ(retainedNative->size(), 1);
 EXPECT_EQ(retainedNative->front().get(), before->front().get());
 EXPECT_EQ(retainedNative->front()->val, value);
 EXPECT_EQ(source->getCount(), 9);
}
TEST_F(OwnedArenaNativeParityTest, RemovedCartStopsSupplyingProjectedShotsWithoutChangingLiveShots)
{
 auto * shooter = add(5); auto * cart = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(7,5), 1);
 auto p = project(); auto copy = p->getForUpdate(shooter->unitId());
 ASSERT_FALSE(copy->shots.isLimited());
 p->removeUnit(cart->unitId());
 const bool projectedLimited = copy->shots.isLimited();
 EXPECT_FALSE(shooter->shots.isLimited()); EXPECT_FALSE(cart->ghost);
 battle()->removeUnit(cart->unitId());
 ASSERT_TRUE(shooter->shots.isLimited());
 EXPECT_EQ(projectedLimited, shooter->shots.isLimited());
}
TEST_F(OwnedArenaNativeParityTest, NestedProjectionPreservesMutatedParentOrder)
{
 auto * first = add(5); add(7);
 auto parent = project(); parent->moveUnit(first->unitId(), BattleHex(5,6));
 auto child = parent->forkOwnedFixture();
 child->moveUnit(first->unitId(), BattleHex(5,7));
 auto native = battle()->getUnitsIf([](const battle::Unit *) { return true; });
 auto projected = child->getUnitsIf([](const battle::Unit *) { return true; });
 ASSERT_EQ(native.size(), projected.size());
 for(size_t i=0;i<native.size();++i) EXPECT_EQ(native[i]->unitId(), projected[i]->unitId());
 EXPECT_EQ(first->getPosition(), BattleHex(5,5));
 EXPECT_EQ(parent->battleGetUnitByID(first->unitId())->getPosition(), BattleHex(5,6));
}
TEST_F(OwnedArenaNativeParityTest, NonmonotonicAdditionsRetainInsertionOrder)
{
 auto p = project();
 for(const auto id : {100u, 50u}) {
  battle::UnitInfo info; info.id=id; info.count=10; info.type=creatureByName("core:woodElf"); info.side=BattleSide::ATTACKER; info.position=BattleHex(id==100 ? 5 : 7,5); info.summoned=false;
  JsonNode data; info.save(data); p->addUnit(id,data); battle()->addUnit(id,data);
 }
 auto native=battle()->getUnitsIf([](const battle::Unit *) { return true; });
 auto projected=p->getUnitsIf([](const battle::Unit *) { return true; });
 ASSERT_EQ(native.size(),projected.size());
 for(size_t i=0;i<native.size();++i) EXPECT_EQ(native[i]->unitId(),projected[i]->unitId());
}
