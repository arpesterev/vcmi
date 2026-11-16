#include "OwnedStateSyncTest.inc"
#include "lib/battle/AmmoCartQuery.h"
class OwnedAmmoTest : public OwnedStateSyncTest
{
public:
 CStack *cart(BattleSide side,int x){return addStack(side,CreatureID::AMMO_CART,BattleHex(x,5),1);}
 void parity(const OwnedBattleBonusModel & model,const CStack *unit){EXPECT_EQ(model.unitHasAmmoCart(unit->unitId()),unit->unitHasAmmoCart(unit));EXPECT_EQ(model.unitEffectiveOwner(unit->unitId()),unit->unitEffectiveOwner(unit));}
};
TEST_F(OwnedAmmoTest, LiveCartThenRemovalSuppressesLaterCartAndArtifact)
{
 auto *shooter=add(5);auto *first=cart(BattleSide::ATTACKER,7);cart(BattleSide::ATTACKER,9);giveArtifact(attackerSideHero,ArtifactID::AMMO_CART,ArtifactPosition::MACH2);auto parent=OwnedBattleBonusModel::capture(*battle());auto child=parent;ASSERT_TRUE(child.unitHasAmmoCart(shooter->unitId()));parity(child,shooter);ASSERT_TRUE(child.removeUnit(first->unitId()));EXPECT_FALSE(child.unitHasAmmoCart(shooter->unitId()));EXPECT_TRUE(parent.unitHasAmmoCart(shooter->unitId()));EXPECT_TRUE(shooter->unitHasAmmoCart(shooter));battle()->removeUnit(first->unitId());parity(child,shooter);
}
TEST_F(OwnedAmmoTest, DeadFirstCartSuppressesLiveCartAndArtifact)
{
 auto *shooter=add(5);auto *first=cart(BattleSide::ATTACKER,7);cart(BattleSide::ATTACKER,9);int64_t damage=first->getAvailableHealth();first->damage(damage);giveArtifact(attackerSideHero,ArtifactID::AMMO_CART,ArtifactPosition::MACH2);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_FALSE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);
}
TEST_F(OwnedAmmoTest, HypnotizedShooterStillUsesOriginalSideCart)
{
 auto *shooter=add(5);hypnotize(shooter);cart(BattleSide::ATTACKER,7);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_TRUE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);
}
TEST_F(OwnedAmmoTest, HypnotizedEnemyCartMatchesOriginalShooterSide)
{
 auto *shooter=add(5);auto *enemy=cart(BattleSide::DEFENDER,9);hypnotize(enemy);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_TRUE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);
}
TEST_F(OwnedAmmoTest, ArtifactFallbackUsesEffectiveOwner)
{
 auto *shooter=add(5);hypnotize(shooter);giveArtifact(defenderSideHero,ArtifactID::AMMO_CART,ArtifactPosition::MACH2);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_TRUE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);
}
TEST_F(OwnedAmmoTest, SameUnitIdentityShortcutPrecedesEffectiveOwnerMismatch)
{
 auto *self=cart(BattleSide::ATTACKER,7);hypnotize(self);auto model=OwnedBattleBonusModel::capture(*battle());EXPECT_TRUE(model.unitHasAmmoCart(self->unitId()));parity(model,self);
}
TEST_F(OwnedAmmoTest, PropagatingSourceRemovalChangesCartOwnerAndSupply)
{
 auto *shooter=add(5);auto *source=addStack(BattleSide::DEFENDER,creatureByName("core:woodElf"),BattleHex(10,5),10);cart(BattleSide::DEFENDER,12);auto effect=bonus(BonusType::HYPNOTIZED,1);effect->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);source->addNewBonus(effect);auto model=OwnedBattleBonusModel::capture(*battle());ASSERT_TRUE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);model.removeUnit(source->unitId());EXPECT_FALSE(model.unitHasAmmoCart(shooter->unitId()));EXPECT_TRUE(shooter->unitHasAmmoCart(shooter));battle()->removeUnit(source->unitId());parity(model,shooter);
}
TEST_F(OwnedAmmoTest, PublicCartHealthMutationRefreshesSupplyWithoutFlags)
{
 auto *shooter=add(5);auto *supply=cart(BattleSide::ATTACKER,7);auto model=OwnedBattleBonusModel::capture(*battle());ASSERT_TRUE(model.unitHasAmmoCart(shooter->unitId()));supply->health.reset();EXPECT_TRUE(model.synchronizeUnits(observations(model)));EXPECT_FALSE(model.unitHasAmmoCart(shooter->unitId()));parity(model,shooter);
}
TEST_F(OwnedAmmoTest, SharedKernelRetainsMatchBeforeTypeAndStopsOnFirstDeadCart)
{
 std::vector<int> inputs{1,2,3};std::vector<int> trace;bool fallback=false;
 auto result=AmmoCartQuery::evaluate(inputs,[&](int i){trace.push_back(i*10);return i!=1;},[&](int i){trace.push_back(i*10+1);return true;},[&](int i){trace.push_back(i*10+2);return false;},[&]{fallback=true;return true;});EXPECT_FALSE(result);EXPECT_FALSE(fallback);EXPECT_EQ(trace,(std::vector<int>{10,20,21,22}));
}
