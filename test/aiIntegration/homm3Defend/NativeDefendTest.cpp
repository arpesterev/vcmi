// GPL-2.0-or-later. Native canonical metadata only; no full domain certification.
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../../AI/Integration/Ports/HoMM3NativeDefendFacts.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../server/CGameHandler.h"

using namespace AIIntegration::Ports::HoMM3;
class NativeDefendFixture : public BattleTestFixture
{
protected:
 void setOriginalTroop(const char * name)
 {
  const StackLocation slot(attackerSideHero->id,SlotID(0));
  ASSERT_TRUE(gameHandler->changeStackType(slot,creatureByName(name).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(slot,10,ChangeValueMode::ABSOLUTE));
 }
 CStack * originalTroop()
 {
  for(const auto & unit:battle()->stacks)
   if(unit->unitSide()==BattleSide::ATTACKER && unit->unitSlot()==SlotID(0)) return unit.get();
  return nullptr;
 }
};
class NativeDefendMetadataTest : public NativeDefendFixture, public ::testing::WithParamInterface<const char *> {};
TEST_P(NativeDefendMetadataTest, ActualServerDefendMatchesStrictMetadata)
{
 startGame(); setOriginalTroop(GetParam()); ASSERT_FALSE(HasFatalFailure()); startBattle();
 auto * unit=originalTroop();
 ASSERT_NE(unit,nullptr);
 const auto pre=NativeDefendFacts::extract(*unit,1);
 ASSERT_TRUE(pre);
 EXPECT_EQ(pre->stance,DefendUnitFacts::Stance::Absent);
 ASSERT_TRUE(act(BattleAction::makeDefend(unit)));
 const auto post=NativeDefendFacts::extract(*unit,1);
 ASSERT_TRUE(post);
 const auto d=pre->underlyingDefense;
 EXPECT_EQ(post->underlyingDefense,d);
 EXPECT_EQ(post->displayedDefense,d+std::max<int64_t>(d/5,1));
 EXPECT_EQ(post->defenseProvenance,pre->defenseProvenance);
 EXPECT_EQ(post->stance,d<5 ? DefendUnitFacts::Stance::CanonicalAddOneZeroExtra
  : DefendUnitFacts::Stance::CanonicalPercent20ZeroExtra);
}
INSTANTIATE_TEST_SUITE_P(Core,NativeDefendMetadataTest,::testing::Values("core:peasant","core:griffin","core:angel"));

class NativeDefendMetadataRejectionTest : public NativeDefendFixture {};
TEST_F(NativeDefendMetadataRejectionTest, RealDefendWithExtraStanceRemainsUnavailable)
{
 startGame(); setOriginalTroop("core:angel"); ASSERT_FALSE(HasFatalFailure()); startBattle();
 auto * unit=originalTroop();
 ASSERT_NE(unit,nullptr);
 ASSERT_TRUE(NativeDefendFacts::extract(*unit,1));
 unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::DEFENSIVE_STANCE,BonusSource::OTHER,3,BonusSourceID()));
 ASSERT_TRUE(act(BattleAction::makeDefend(unit)));
 EXPECT_FALSE(NativeDefendFacts::extract(*unit,1));
}

TEST_F(NativeDefendMetadataRejectionTest, RealHeroDefenseProviderIsPreservedExactly)
{
 startGame();
 attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE,4,ChangeValueMode::ABSOLUTE);
 startBattle();
 CStack * unit=nullptr;
 for(const auto & candidate:battle()->stacks)
  if(candidate->unitSide()==BattleSide::ATTACKER && candidate->unitSlot().getNum()>=0) {unit=candidate.get(); break;}
 ASSERT_NE(unit,nullptr);
 const auto pre=NativeDefendFacts::extract(*unit,1);
 ASSERT_TRUE(pre);
 EXPECT_EQ(pre->underlyingDefense,unit->unitType()->getDefense(false)+4);
 bool provider=false;
 for(size_t i=3;i+2<pre->defenseProvenance.size();i+=3)
  if(pre->defenseProvenance[i]==1)
  { provider=true; EXPECT_EQ(pre->defenseProvenance[i+1],attackerSideHero->id.getNum()); EXPECT_EQ(pre->defenseProvenance[i+2],4); }
 EXPECT_TRUE(provider);
 ASSERT_TRUE(act(BattleAction::makeDefend(unit)));
 const auto post=NativeDefendFacts::extract(*unit,1);
 ASSERT_TRUE(post);
 EXPECT_EQ(post->defenseProvenance,pre->defenseProvenance);
}

TEST_F(NativeDefendMetadataRejectionTest, SameOriginalGriffinOnSandIsAcceptedTerrainControl)
{
 startGame(); setOriginalTroop("core:griffin"); ASSERT_FALSE(HasFatalFailure()); startBattle(ETerrainId::SAND);
 auto * unit=originalTroop(); ASSERT_NE(unit,nullptr);
 ASSERT_EQ(unit->unitType()->getId(),creatureByName("core:griffin"));
 ASSERT_EQ(unit->getDefense(false),unit->unitType()->getDefense(false));
 const auto records=unit->getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE)));
 for(const auto & record:*records) ASSERT_NE(record->source,BonusSource::TERRAIN_NATIVE);
 ASSERT_TRUE(NativeDefendFacts::extract(*unit,1));
}
TEST_F(NativeDefendMetadataRejectionTest, NativeTerrainRemainsExplicitlyUnsupported)
{
 // Independent native context, same original troop/hero setup as SAND control.
 // Do not mutate BattleInfo root bonuses behind its owned effective model.
 startGame(); setOriginalTroop("core:griffin"); ASSERT_FALSE(HasFatalFailure()); startBattle(ETerrainId::GRASS);
 auto * unit=originalTroop(); ASSERT_NE(unit,nullptr);
 ASSERT_EQ(unit->unitType()->getId(),creatureByName("core:griffin"));
 ASSERT_EQ(unit->getDefense(false),unit->unitType()->getDefense(false)+1);
 const auto records=unit->getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE)));
 int terrain=0;
 for(const auto & record:*records) if(record->source==BonusSource::TERRAIN_NATIVE) ++terrain;
 ASSERT_EQ(terrain,1);
 EXPECT_FALSE(NativeDefendFacts::extract(*unit,1));
}
TEST_F(NativeDefendMetadataRejectionTest, ActualCanonicalStoneSkinBeforeDefendIsSupported)
{
 startGame(); setOriginalTroop("core:griffin"); ASSERT_FALSE(HasFatalFailure()); startBattle();
 auto * unit=originalTroop();
 ASSERT_NE(unit,nullptr);
 ASSERT_TRUE(NativeDefendFacts::extract(*unit,1));
 teachSpell(attackerSideHero,spellByName("core:stoneSkin"));
 attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,5,ChangeValueMode::ABSOLUTE);
 ASSERT_TRUE(castOn(attackerSideHero,spellByName("core:stoneSkin"),unit));
 const auto after=NativeDefendFacts::extract(*unit,1);
 ASSERT_TRUE(after);
 EXPECT_EQ(after->stance,DefendUnitFacts::Stance::Absent);
 ASSERT_TRUE(act(BattleAction::makeDefend(unit)));
 EXPECT_TRUE(NativeDefendFacts::extract(*unit,1));
}

class NativeDefendMalformedTest : public NativeDefendFixture, public ::testing::WithParamInterface<int> {};
TEST_P(NativeDefendMalformedTest, RejectsMalformedOrDuplicateStanceMetadata)
{
 startGame(); setOriginalTroop("core:griffin"); ASSERT_FALSE(HasFatalFailure()); startBattle();
 auto * unit=originalTroop();
 ASSERT_NE(unit,nullptr);
 ASSERT_TRUE(NativeDefendFacts::extract(*unit,1));
 ASSERT_TRUE(act(BattleAction::makeDefend(unit)));
 ASSERT_TRUE(NativeDefendFacts::extract(*unit,1));
 auto bad=std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN,BonusType::PRIMARY_SKILL,BonusSource::OTHER,20,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE),BonusValueType::PERCENT_TO_ALL);
 if(GetParam()!=0)
 {
  const auto originals=unit->getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,BonusSubtypeID(PrimarySkill::DEFENSE)));
  for(const auto & old:*originals)
   if(old->source==BonusSource::OTHER && old->valType==BonusValueType::PERCENT_TO_ALL) unit->removeBonus(old);
 }
 switch(GetParam())
 {
 case 0: break; // duplicate exact percent record
 case 1: bad->stacking="unverified"; break;
 case 2: bad->targetSourceID=BonusSourceID(ObjectInstanceID(123)); break;
 case 3: bad->bonusOwner=PlayerColor(0); break;
 case 4: bad->turnsRemain=1; break;
 case 5: bad->sid=BonusSourceID(ObjectInstanceID(123)); break;
 case 6: bad->duration=BonusDuration::N_TURNS; break;
 case 7: bad->val=21; break;
 }
 unit->addNewBonus(bad);
 EXPECT_FALSE(NativeDefendFacts::extract(*unit,1));
}
INSTANTIATE_TEST_SUITE_P(Metadata,NativeDefendMalformedTest,::testing::Range(0,8));
