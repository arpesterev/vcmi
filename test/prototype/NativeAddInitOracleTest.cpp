#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Propagators.h"
class NativeAddInitOracleTest : public BattleTestFixture
{
public:
 void SetUp()override{BattleTestFixture::SetUp();startGame();startBattle();}
 static CSelector selected(){return Selector::source(BonusSource::OTHER,ArtifactID(777));}
 CStack*packetAdd(uint32_t id=100){battle::UnitInfo info;info.id=id;info.count=10;info.type=creatureByName("core:woodElf");info.side=BattleSide::ATTACKER;info.position=BattleHex(5,5);info.summoned=false;BattleUnitsChanged p;p.battleID=BattleID(0);p.changedStacks.emplace_back(id,UnitChanges::EOperation::ADD);info.save(p.changedStacks.back().data);gameHandler->sendAndApply(p);return battle()->getStack(id,false);}
 struct AddedBonus {CCreature*c;std::shared_ptr<Bonus>b;~AddedBonus(){c->removeBonus(b);}};
 AddedBonus install(bool geometry,bool health=false){auto*c=LIBRARY->creh->objects.at(creatureByName("core:woodElf").getNum()).get();auto b=std::make_shared<Bonus>(BonusDuration::PERMANENT,health?BonusType::STACK_HEALTH:BonusType::STACKS_SPEED,BonusSource::OTHER,1,ArtifactID(777));if(geometry)b->limiter=std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(5,5),BattleHex(6,5)});else b->updater=std::make_shared<TimesStackSizeUpdater>();c->addNewBonus(b);return {c,b};}
 void trace(const char*name,const CStack*s,int direct,int named){std::cout<<"ADD_INIT|"<<name<<"|count="<<s->health.getCount()<<"|firstHP="<<s->health.getFirstHPleft()<<"|maxHP="<<s->getMaxHealth()<<"|position="<<s->position.toInt()<<"|node_revision="<<s->getTreeVersion()<<"|battle_revision="<<battle()->getTreeVersion()<<"|direct="<<direct<<"|named="<<named<<'\n';}
};
TEST_F(NativeAddInitOracleTest, CompleteAddPacketPublishesFinalCountToOrdinaryAndNamedQueries)
{
 auto installed=install(false);auto*s=packetAdd();ASSERT_NE(s,nullptr);ASSERT_EQ(s->health.getCount(),10);EXPECT_EQ(s->position,BattleHex(5,5));auto direct=s->getAllBonuses(selected());auto named=s->getAllBonuses(selected(),"init-oracle-count");ASSERT_EQ(direct->size(),1);ASSERT_EQ(named->size(),1);trace("count-immediate",s,direct->front()->val,named->front()->val);EXPECT_EQ(direct->front()->val,10);EXPECT_EQ(named->front()->val,10);auto version=s->getTreeVersion();auto firstHP=s->health.getFirstHPleft();
 battle()->moveUnit(s->unitId(),BattleHex(6,5));auto after=s->getAllBonuses(selected());auto afterNamed=s->getAllBonuses(selected(),"init-oracle-count");ASSERT_EQ(after->size(),1);EXPECT_EQ(after->front()->val,10);EXPECT_EQ(afterNamed->front()->val,10);EXPECT_GT(s->getTreeVersion(),version);EXPECT_EQ(s->health.getCount(),10);EXPECT_EQ(s->health.getFirstHPleft(),firstHP);trace("count-after-public-move",s,after->front()->val,afterNamed->front()->val);
}
TEST_F(NativeAddInitOracleTest, CompleteAddPacketPublishesFinalPositionToGeometryLimiter)
{
 auto installed=install(true);auto*s=packetAdd();ASSERT_NE(s,nullptr);auto direct=s->getAllBonuses(selected());auto named=s->getAllBonuses(selected(),"init-oracle-position");trace("position-immediate",s,direct->totalValue(),named->totalValue());EXPECT_EQ(direct->totalValue(),1);EXPECT_EQ(named->totalValue(),1);battle()->moveUnit(s->unitId(),BattleHex(6,5));auto after=s->getAllBonuses(selected());auto afterNamed=s->getAllBonuses(selected(),"init-oracle-position");EXPECT_EQ(after->totalValue(),1);EXPECT_EQ(afterNamed->totalValue(),1);trace("position-after-public-move",s,after->totalValue(),afterNamed->totalValue());
}
TEST_F(NativeAddInitOracleTest, EpochRefreshDoesNotRewritePartialCountHealthInitialization)
{
 auto*c=creatureByName("core:woodElf").toCreature();auto baseHealth=c->getMaxHealth();auto installed=install(false,true);auto*s=packetAdd();ASSERT_NE(s,nullptr);EXPECT_EQ(s->health.getCount(),10);EXPECT_EQ(s->health.getFirstHPleft(),baseHealth+9);auto before=s->health.getFirstHPleft();trace("health-immediate",s,s->getAllBonuses(selected())->totalValue(),s->getAllBonuses(selected(),"init-oracle-health")->totalValue());battle()->moveUnit(s->unitId(),BattleHex(6,5));EXPECT_EQ(s->health.getFirstHPleft(),before);EXPECT_EQ(s->getAllBonuses(selected())->totalValue(),10);trace("health-after-public-move",s,s->getAllBonuses(selected())->totalValue(),s->getAllBonuses(selected(),"init-oracle-health")->totalValue());
}
TEST_F(NativeAddInitOracleTest, AddEpochDoesNotReplayExistingHistoricalPropagationMaterialization)
{
 auto*source=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(9,5),10);auto effect=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,ArtifactID(777));effect->propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);effect->propagationUpdater=std::make_shared<TimesStackSizeUpdater>();source->addNewBonus(effect);auto raw=attackerSideHero->getLocalBonus(selected());ASSERT_NE(raw,nullptr);ASSERT_EQ(raw->val,10);int64_t damage=source->getMaxHealth();source->damage(damage);ASSERT_EQ(source->getCount(),9);auto*added=packetAdd();ASSERT_NE(added,nullptr);auto after=attackerSideHero->getLocalBonus(selected());EXPECT_EQ(after.get(),raw.get());EXPECT_EQ(after->val,10);EXPECT_EQ(after->propagationUpdater.get(),raw->propagationUpdater.get());std::cout<<"ADD_INIT|historical-propagation|source_count=9|value="<<after->val<<"|same_materialized_identity="<<(after.get()==raw.get())<<'\n';
}
