#include "OwnedFrameBaseline.inc"
class OwnedFrameAddTest : public OwnedAmmoTest
{
public:
 battle::UnitInfo info(uint32_t id, CreatureID creature, BattleSide side, int count, BattleHex position)
 { battle::UnitInfo v;v.id=id;v.type=creature;v.side=side;v.count=count;v.position=position;v.summoned=true;return v; }
 void append(HypotheticBattle & frame,battle::UnitInfo value)
 { JsonNode data;value.save(data);frame.addUnit(value.id,data);battle()->addUnit(value.id,data); }
 const battle::Unit * native(uint32_t id)
 { for(auto *u:battle()->getUnitsIf([](const battle::Unit *){return true;}))if(u->unitId()==id)return u;return nullptr; }
};
TEST_F(OwnedFrameAddTest, EmptySideAndNonmonotonicIDsKeepNativeInsertionOrder)
{
 add(5);auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 auto value=info(900,creatureByName("core:woodElf"),BattleSide::DEFENDER,10,BattleHex(12,5));append(*frame,value);
 value.id=37;value.position=BattleHex(10,5);append(*frame,value);
 auto owned=frame->getUnitsIf([](const battle::Unit *){return true;});auto real=battle()->getUnitsIf([](const battle::Unit *){return true;});ASSERT_EQ(owned.size(),real.size());
 for(size_t i=0;i<owned.size();++i){EXPECT_EQ(owned[i]->unitId(),real[i]->unitId());EXPECT_EQ(owned[i]->unitOwner(),real[i]->unitOwner());EXPECT_EQ(owned[i]->getCount(),real[i]->getCount());EXPECT_EQ(owned[i]->getFirstHPleft(),real[i]->getFirstHPleft());EXPECT_EQ(owned[i]->getPosition(),real[i]->getPosition());}
 auto child=frame->forkOwnedFixture();child->removeUnit(900);EXPECT_TRUE(frame->getForUpdate(900)->alive());EXPECT_TRUE(native(900)->alive());EXPECT_FALSE(child->getForUpdate(900)->alive());
}
TEST_F(OwnedFrameAddTest, NewlyAddedCartSuppliesOnlyItsFrameAndRemovalMatchesNative)
{
 auto *shooter=add(5);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto sibling=frame->forkOwnedFixture();
 append(*frame,info(900,CreatureID::AMMO_CART,BattleSide::ATTACKER,1,BattleHex(7,5)));
 EXPECT_FALSE(frame->getForUpdate(shooter->unitId())->shots.isLimited());EXPECT_TRUE(sibling->getForUpdate(shooter->unitId())->shots.isLimited());EXPECT_FALSE(shooter->shots.isLimited());
 frame->removeUnit(900);battle()->removeUnit(900);EXPECT_TRUE(frame->getForUpdate(shooter->unitId())->shots.isLimited());EXPECT_TRUE(shooter->shots.isLimited());
}
TEST_F(OwnedFrameAddTest, HealthInitializationReadsActualPartialCountBeforeFinalPosition)
{
 auto b=bonus(BonusType::STACK_HEALTH,1);auto updater=std::make_shared<TimesStackSizeUpdater>(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),1,3);b->updater=updater;attackerSideHero->addNewBonus(b);
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto value=info(900,creatureByName("core:woodElf"),BattleSide::ATTACKER,10,BattleHex(5,5));append(*frame,value);
 auto owned=frame->getForUpdate(900);auto *real=native(900);ASSERT_NE(real,nullptr);
 EXPECT_EQ(owned->health.getCount(),real->getCount());EXPECT_EQ(owned->getFirstHPleft(),real->getFirstHPleft());EXPECT_EQ(owned->getPosition(),real->getPosition());
 // Native add initialization may leave a count-dependent cache warm at partial
 // health. A real move operation invalidates it; no manual nodeHasChanged call.
 battle()->moveUnit(900,value.position);frame->moveUnit(900,value.position);
 EXPECT_EQ(owned->getMaxHealth(),real->getMaxHealth());
}
TEST_F(OwnedFrameAddTest, ZeroCountAndInvalidWideGeometryAreRepresentable)
{
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());
 auto value=info(900,creatureByName("core:greenDragon"),BattleSide::DEFENDER,0,BattleHex::INVALID);append(*frame,value);
 auto unit=frame->getForUpdate(900);ASSERT_NE(unit,nullptr);EXPECT_EQ(unit->getCount(),0);EXPECT_FALSE(unit->alive());EXPECT_EQ(unit->getCount(),native(900)->getCount());EXPECT_EQ(unit->getPosition(),native(900)->getPosition());
}
