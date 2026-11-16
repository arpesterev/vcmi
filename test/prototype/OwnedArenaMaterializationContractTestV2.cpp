#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"

class OwnedArenaMaterializationContractTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x,5), 10); }
 static CSelector selected()
 {
  return [](const Bonus * b) { return b->type == BonusType::STACKS_SPEED && b->source == BonusSource::OTHER &&
   (b->sid == ArtifactID(777) || b->sid == ArtifactID(778) || b->sid == ArtifactID(779)); };
 }
 static std::shared_ptr<Bonus> effect(int id, const std::shared_ptr<TimesStackSizeUpdater> & updater,
  const std::shared_ptr<CPropagatorNodeType> & propagator)
 {
  auto b=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,1,ArtifactID(id));
  b->propagator=propagator; b->propagationUpdater=updater; return b;
 }
 static void compare(const TConstBonusListPtr & expected,const TConstBonusListPtr & actual)
 {
  ASSERT_EQ(actual->size(),expected->size());
  for(size_t i=0;i<expected->size();++i)
  {
   const auto &e=(*expected)[i]; const auto &a=(*actual)[i];
   EXPECT_EQ(a->sid,e->sid); EXPECT_EQ(a->type,e->type); EXPECT_EQ(a->source,e->source);
   EXPECT_EQ(a->val,e->val); EXPECT_EQ(a->valType,e->valType); EXPECT_EQ(a->duration,e->duration);
   for(size_t j=0;j<expected->size();++j)
   {
    EXPECT_EQ(a.get()==(*actual)[j].get(),e.get()==(*expected)[j].get());
    EXPECT_EQ(a->propagationUpdater.get()==(*actual)[j]->propagationUpdater.get(),
     e->propagationUpdater.get()==(*expected)[j]->propagationUpdater.get());
    EXPECT_EQ(a->propagator.get()==(*actual)[j]->propagator.get(),
     e->propagator.get()==(*expected)[j]->propagator.get());
   }
  }
 }
};

TEST_F(OwnedArenaMaterializationContractTest, HistoricalValuesOwnedAliasPartitionsAndWithdrawalIsolation)
{
 auto *first=add(3); auto *second=add(5); auto *third=add(7); auto *recipient=add(9);
 auto shared=std::make_shared<TimesStackSizeUpdater>();
 auto distinct=std::make_shared<TimesStackSizeUpdater>();
 auto propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 auto a=effect(777,shared,propagator),b=effect(778,shared,propagator),c=effect(779,distinct,propagator);
 first->addNewBonus(a); second->addNewBonus(b); third->addNewBonus(c);
 auto historical=recipient->getAllBonuses(selected()); ASSERT_EQ(historical->size(),3);
 for(const auto &value:*historical) EXPECT_EQ(value->val,10);
 auto ha=historical->getFirst(Selector::source(BonusSource::OTHER,ArtifactID(777)));
 auto hb=historical->getFirst(Selector::source(BonusSource::OTHER,ArtifactID(778)));
 auto hc=historical->getFirst(Selector::source(BonusSource::OTHER,ArtifactID(779)));
 ASSERT_NE(ha,nullptr); ASSERT_NE(hb,nullptr); ASSERT_NE(hc,nullptr);
 EXPECT_NE(ha.get(),hb.get()); EXPECT_NE(hb.get(),hc.get()); EXPECT_NE(ha.get(),hc.get());
 EXPECT_EQ(ha->propagationUpdater.get(),hb->propagationUpdater.get());
 EXPECT_NE(ha->propagationUpdater.get(),hc->propagationUpdater.get());
 EXPECT_EQ(ha->propagator.get(),hc->propagator.get());
 int64_t damage=first->getMaxHealth(); first->damage(damage); ASSERT_EQ(first->getCount(),9);
 auto nativeAfterDamage=recipient->getAllBonuses(selected()); compare(historical,nativeAfterDamage);
 for(size_t i=0;i<historical->size();++i) EXPECT_EQ((*historical)[i].get(),(*nativeAfterDamage)[i].get());

 auto parent=HypotheticBattle::captureOwnedFixture(*battle());
 auto child=parent->forkOwnedFixture(); auto sibling=parent->forkOwnedFixture();
 auto read=[&](const std::shared_ptr<HypotheticBattle> & frame){return frame->getForUpdate(recipient->unitId())->getAllBonuses(selected());};
 auto owned=read(parent); compare(historical,owned); compare(historical,read(child)); compare(historical,read(sibling));
 for(const auto &value:*owned)
  for(const auto &live:*historical)
  {
   EXPECT_NE(value.get(),live.get());
   EXPECT_NE(value->propagationUpdater.get(),live->propagationUpdater.get());
   EXPECT_NE(value->propagator.get(),live->propagator.get());
  }

 auto mutableResult=read(child); ASSERT_EQ(mutableResult->size(),3);
 mutableResult->front()->val=999; mutableResult->front()->propagationUpdater.reset(); mutableResult->front()->propagator.reset();
 compare(historical,read(child)); compare(historical,read(parent)); compare(historical,read(sibling));
 compare(historical,recipient->getAllBonuses(selected()));

 // Editing live updater parameters must not edit the captured component graph or replay historical exports.
 *shared=TimesStackSizeUpdater(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),1,3);
 *distinct=TimesStackSizeUpdater(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),1,4);
 EXPECT_EQ(shared->createUpdatedBonus(a,*first)->val,27);
 EXPECT_EQ(distinct->createUpdatedBonus(c,*first)->val,36);
 const auto retainedAfterNativeMutation = read(parent);
 for(const auto &value:*retainedAfterNativeMutation)
 {
  const auto *updater=dynamic_cast<const TimesStackSizeUpdater*>(value->propagationUpdater.get());
  ASSERT_NE(updater,nullptr); EXPECT_EQ(updater->createUpdatedBonus(a,*first)->val,9); EXPECT_EQ(value->val,10);
 }
 auto beforeFirstVersion=first->getTreeVersion(); auto beforeRecipientVersion=recipient->getTreeVersion();
 child->removeUnit(first->unitId());
 auto childValues=read(child); ASSERT_EQ(childValues->size(),1); EXPECT_EQ(childValues->front()->sid,BonusSourceID(ArtifactID(779))); EXPECT_EQ(childValues->front()->val,10);
 sibling->removeUnit(third->unitId());
 auto siblingValues=read(sibling); ASSERT_EQ(siblingValues->size(),2);
 for(const auto &value:*siblingValues) { EXPECT_NE(value->sid,BonusSourceID(ArtifactID(779))); EXPECT_EQ(value->val,10); }
 compare(historical,read(parent)); compare(historical,recipient->getAllBonuses(selected()));
 EXPECT_FALSE(first->ghost); EXPECT_FALSE(third->ghost); EXPECT_EQ(first->getTreeVersion(),beforeFirstVersion); EXPECT_EQ(recipient->getTreeVersion(),beforeRecipientVersion);

 // Native removal withdraws the shared updater identity group, including the other source's materialization.
 battle()->removeUnit(first->unitId());
 auto nativeAfterWithdrawal=recipient->getAllBonuses(selected()); compare(nativeAfterWithdrawal,childValues);
 ASSERT_EQ(nativeAfterWithdrawal->size(),1); EXPECT_EQ(nativeAfterWithdrawal->front()->sid,BonusSourceID(ArtifactID(779)));
 EXPECT_NE(second->getExportedBonusList().getFirst(Selector::source(BonusSource::OTHER,ArtifactID(778))),nullptr);
 compare(historical,read(parent)); ASSERT_EQ(read(sibling)->size(),2);
}
