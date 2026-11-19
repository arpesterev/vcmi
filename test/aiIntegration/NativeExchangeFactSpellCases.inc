// GPL-2.0-or-later. Include AFTER NativeRangedCensusCases.inc.
// These exercise actual server-generated spell records, never synthetic bonuses.
#include "homm3Exchange/PinnedExchange.h"

class NativeExchangeFactSpellBattle : public AIIntegrationNativeRangedCensusBattle
{
protected:
 void castReal(SpellID spellID, CStack * target)
 {
  const auto mana = attackerSideHero->mana;
  const auto packets = recorder.spellPackets;
  const auto * spell = spellID.toSpell();
  const int cost = censusView->battleGetSpellCost(spell, attackerSideHero);
  const auto action = BattleAction::makeHeroSpellcast(BattleSide::ATTACKER,
   {battle::Destination(target)}, spellID);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
   attackerSideHero->getOwner(), action));
  EXPECT_EQ(attackerSideHero->mana, mana - cost);
  EXPECT_GT(recorder.spellPackets, packets);
 }
 void exerciseSpell(SpellID spellID, bool expert)
 {
  namespace Ref = PinnedExchange;
  using namespace AIIntegration::Ports::HoMM3;
  prepareCensus(false,TerrainId(ETerrainId::SAND));
  ASSERT_FALSE(HasFatalFailure());
  teachBattleSpell(attackerSideHero, spellID, 6);
  setMagicMastery(attackerSideHero, expert ? MasteryLevel::EXPERT : MasteryLevel::BASIC);
  const bool negative = spellID == SpellID::CURSE || spellID == SpellID::DISRUPTING_RAY;
  auto * affected = negative ? targets[0] : shooters[0];
  castReal(spellID, affected);
  ASSERT_FALSE(HasFatalFailure());
  const auto mana = attackerSideHero->mana;
  const auto packets = recorder.spellPackets;
  const auto ownHealth = shooters[0]->getAvailableHealth();
  const auto enemyHealth = targets[0]->getAvailableHealth();
  Ref::manager = {};
  for(auto & ability : Ref::g_heroSpecificAbilities) ability = {};
  Ref::akCreatureTypeTraits[2] = {double(CreatureID(2).toCreature()->getFightValue()), 6, 3};
  Ref::akCreatureTypeTraits[6] = {double(CreatureID(6).toCreature()->getFightValue()), 10, 12};
  Ref::hero ownHero, enemyHero;
  Ref::army actor, target;
  actor.m_creatureType = 2; actor.controller = &ownHero;
  actor.flags = Ref::creatureShootingArmy; actor.shooting = true;
  actor.m_numTroops = 80; actor.m_monInfo.hitPoints = 10;
  actor.m_monInfo.damageLowBound = 2; actor.m_monInfo.damageHighBound = 3;
  actor.m_monInfo.attackSkill = 6; actor.m_monInfo.defenseSkill = 3;
  target.m_creatureType = 6; target.controller = &enemyHero;
  target.m_numTroops = 15; target.m_monInfo.hitPoints = 35;
  target.m_monInfo.damageLowBound = 6; target.m_monInfo.damageHighBound = 9;
  target.m_monInfo.attackSkill = 10; target.m_monInfo.defenseSkill = 12;
  const int amount = expert ? 6 : 3;
  if(spellID == SpellID::BLESS) { actor.m_spellInfluence[41]=6; actor.m_blessAmount=expert ? 1 : 0; }
  if(spellID == SpellID::CURSE) { target.m_spellInfluence[42]=6; target.m_curseAmount=expert ? 1 : 0; }
  if(spellID == SpellID::BLOODLUST) { actor.m_spellInfluence[43]=6; actor.m_bloodlustAmount=amount; }
  if(spellID == SpellID::PRECISION) { actor.m_spellInfluence[44]=6; actor.m_precisionAmount=amount; }
  if(spellID == SpellID::STONE_SKIN) actor.m_monInfo.defenseSkill += amount;
  if(spellID == SpellID::DISRUPTING_RAY) target.m_monInfo.defenseSkill -= expert ? 5 : 3;
  // Original source minima include the other unchanged swords. All modifiers
  // except Ray are nonnegative, so both minima remain zero in this fixture.
  const int lowAttack = 0;
  const int lowDefense = spellID == SpellID::DISRUPTING_RAY ? (expert ? -5 : -3) : 0;
  const auto ownModifiers = NativeExchangeFacts::modifiers(*censusView, *shooters[0]);
  const auto enemyModifiers = NativeExchangeFacts::modifiers(*censusView, *targets[0]);
  ASSERT_TRUE(ownModifiers) << spellID.getNum();
  ASSERT_TRUE(enemyModifiers) << spellID.getNum();
  EXPECT_EQ(ownModifiers->first, actor.getAttackModifier(nullptr, true));
  EXPECT_EQ(ownModifiers->second, actor.getDefenseModifier());
  EXPECT_EQ(enemyModifiers->first, target.getAttackModifier(nullptr, false));
  EXPECT_EQ(enemyModifiers->second, target.getDefenseModifier());
  const auto own = NativeExchangeFacts::unit(*censusView, *shooters[0],lowAttack,lowDefense);
  const auto enemy = NativeExchangeFacts::unit(*censusView,*targets[0],lowAttack,lowDefense);
  ASSERT_TRUE(own); ASSERT_TRUE(enemy);
  for(int channel=0;channel<2;++channel)
  {
   EXPECT_DOUBLE_EQ(own->lossUnitValue[channel],actor.getUnitCombatValue(lowAttack,lowDefense,channel,nullptr));
   EXPECT_DOUBLE_EQ(enemy->lossUnitValue[channel],target.getUnitCombatValue(lowAttack,lowDefense,channel,nullptr));
  }
  EXPECT_EQ(own->blessed,spellID==SpellID::BLESS);
  EXPECT_EQ(enemy->cursed,spellID==SpellID::CURSE);
  if(spellID==SpellID::BLESS) {EXPECT_EQ(own->blessAmount,expert ? 1 : 0);}
  if(spellID==SpellID::CURSE) {EXPECT_EQ(enemy->curseAmount,expert ? 1 : 0);}
  const auto attack=NativeExchangeFacts::rangedAttack(*censusView,*shooters[0],*targets[0]);
  ASSERT_TRUE(attack);
  EXPECT_EQ(attack->attack,actor.getAdjustedAttack(&target,true));
  EXPECT_EQ(attack->defense,target.getAdjustedDefense(&actor,true));
  SourceOrdinaryExchangeInput input;
  input.attacker=*own;input.defender=*enemy;input.attack[1]=*attack;input.ranged=true;
  const auto result=evaluateSourceOrdinaryExchange(input);
  ASSERT_TRUE(result);
  Ref::type_AI_combat_parameters parameters;
  parameters.m_lowestAttack=lowAttack;parameters.m_lowestDefense=lowDefense;
  EXPECT_EQ(result->value,parameters.getSimpleAttackEffect(actor,target,true,1));
  EXPECT_EQ(attackerSideHero->mana,mana);
  EXPECT_EQ(recorder.spellPackets,packets);
  EXPECT_EQ(shooters[0]->getAvailableHealth(),ownHealth);
  EXPECT_EQ(targets[0]->getAvailableHealth(),enemyHealth);
 }
 void passToNextOwnRound()
 {
  const int oldRound=censusView->battleGetRound();
  for(int i=0;i<64;++i)
  {
   const auto * active=battle()->battleActiveUnit();
   ASSERT_NE(active,nullptr);
   if(censusView->battleGetRound()>oldRound && active->unitSide()==BattleSide::ATTACKER) return;
   // Real player no-action command advances the engine without adding a
   // defensive-stance bonus that would contaminate the negative-stat case.
   ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
    battle()->sideToPlayer(active->unitSide()),BattleAction::makeNoAction(active)));
  }
  FAIL()<<"Native round advance did not reach the next attacker activation";
 }
};
#define NATIVE_FACT_SPELL_CASE(label,id) \
 TEST_F(NativeExchangeFactSpellBattle,label##Basic) { exerciseSpell(SpellID(SpellID::id),false); } \
 TEST_F(NativeExchangeFactSpellBattle,label##Expert) { exerciseSpell(SpellID(SpellID::id),true); }
NATIVE_FACT_SPELL_CASE(realBless,BLESS)
NATIVE_FACT_SPELL_CASE(realCurse,CURSE)
NATIVE_FACT_SPELL_CASE(realBloodlust,BLOODLUST)
NATIVE_FACT_SPELL_CASE(realPrecision,PRECISION)
NATIVE_FACT_SPELL_CASE(realStoneSkin,STONE_SKIN)
NATIVE_FACT_SPELL_CASE(realRay,DISRUPTING_RAY)
#undef NATIVE_FACT_SPELL_CASE

TEST_F(NativeExchangeFactSpellBattle, repeatedRealRayBelowUnclampedZeroIsUnavailable)
{
 using AIIntegration::Ports::HoMM3::NativeExchangeFacts;
 prepareCensus(false,TerrainId(ETerrainId::SAND));
 ASSERT_FALSE(HasFatalFailure());
 teachBattleSpell(attackerSideHero,SpellID::DISRUPTING_RAY,6);
 setMagicMastery(attackerSideHero,MasteryLevel::EXPERT);
 ASSERT_EQ(targets[0]->getDefense(false),12);
 for(int cast=1;cast<=3;++cast)
 {
  castReal(SpellID(SpellID::DISRUPTING_RAY),targets[0]);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(targets[0]->getDefense(false),std::max(0,12-5*cast));
  const auto modifiers=NativeExchangeFacts::modifiers(*censusView,*targets[0]);
  if(cast<3) { ASSERT_TRUE(modifiers); EXPECT_EQ(modifiers->second,-5*cast); }
  else
  {
   // Native getter clamps12-15 to0; the adapter must not fabricate a source
   // raw modifier of-12 or admit the negative source adjusted-stat domain.
   EXPECT_FALSE(modifiers);
   EXPECT_FALSE(NativeExchangeFacts::unit(*censusView,*targets[0],0,-15));
  }
  if(cast<3) { passToNextOwnRound(); ASSERT_FALSE(HasFatalFailure()); }
 }
}

namespace
{
void fillExchangeFactBaseline(PinnedExchange::army & actor, PinnedExchange::army & target,
 PinnedExchange::hero & ownHero, PinnedExchange::hero & enemyHero)
{
 namespace Ref = PinnedExchange;
 Ref::manager = {};
 for(auto & ability : Ref::g_heroSpecificAbilities) ability = {};
 Ref::akCreatureTypeTraits[2]={double(CreatureID(2).toCreature()->getFightValue()),6,3};
 Ref::akCreatureTypeTraits[6]={double(CreatureID(6).toCreature()->getFightValue()),10,12};
 actor.m_creatureType=2;actor.controller=&ownHero;actor.flags=Ref::creatureShootingArmy;actor.shooting=true;
 actor.m_numTroops=80;actor.m_monInfo.hitPoints=10;
 actor.m_monInfo.damageLowBound=2;actor.m_monInfo.damageHighBound=3;
 actor.m_monInfo.attackSkill=6;actor.m_monInfo.defenseSkill=3;
 target.m_creatureType=6;target.controller=&enemyHero;target.m_numTroops=15;
 target.m_monInfo.hitPoints=35;target.m_monInfo.damageLowBound=6;target.m_monInfo.damageHighBound=9;
 target.m_monInfo.attackSkill=10;target.m_monInfo.defenseSkill=12;
}
}

TEST_F(NativeExchangeFactSpellBattle, publicHeroStatsAndServerWoundedTopMatchPinnedCensus)
{
 namespace Ref=PinnedExchange;
 using namespace AIIntegration::Ports::HoMM3;
 prepareCensus(false,TerrainId(ETerrainId::SAND));
 ASSERT_FALSE(HasFatalFailure());
 attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK,4,ChangeValueMode::ABSOLUTE);
 attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE,2,ChangeValueMode::ABSOLUTE);
 defenderSideHero->setPrimarySkill(PrimarySkill::ATTACK,1,ChangeValueMode::ABSOLUTE);
 defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE,5,ChangeValueMode::ABSOLUTE);
 teachBattleSpell(attackerSideHero,SpellID::MAGIC_ARROW,1);
 setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
 const int injuries=recorder.injuryPackets;
 castReal(SpellID(SpellID::MAGIC_ARROW),targets[0]);
 ASSERT_FALSE(HasFatalFailure());
 ASSERT_GT(recorder.injuryPackets,injuries);
 ASSERT_EQ(targets[0]->getCount(),15);
 ASSERT_EQ(targets[0]->getAvailableHealth(),15*35-20);
 const auto mana=attackerSideHero->mana;
 const auto spellPackets=recorder.spellPackets;
 const auto injuryPackets=recorder.injuryPackets;
 Ref::hero ownHero,enemyHero;
 Ref::army actor,target;
 fillExchangeFactBaseline(actor,target,ownHero,enemyHero);
 actor.m_monInfo.attackSkill+=4;actor.m_monInfo.defenseSkill+=2;
 target.m_monInfo.attackSkill+=1;target.m_monInfo.defenseSkill+=5;
 Ref::type_AI_combat_parameters parameters;
 parameters.m_lowestAttack=1;parameters.m_lowestDefense=2;
 const auto ours=NativeExchangeFacts::unit(*censusView,*shooters[0],1,2);
 ASSERT_TRUE(ours);
 const auto ownModifiers=NativeExchangeFacts::modifiers(*censusView,*shooters[0]);
 ASSERT_TRUE(ownModifiers);
 EXPECT_EQ(ownModifiers->first,4);EXPECT_EQ(ownModifiers->second,2);
 for(int channel=0;channel<2;++channel)
  EXPECT_DOUBLE_EQ(ours->lossUnitValue[channel],actor.getUnitCombatValue(1,2,channel,nullptr));
 std::array<Ref::army,3> enemies;
 actor.clearAIValues();
 for(size_t j=0;j<targets.size();++j)
 {
  enemies[j]=target;enemies[j].index=j;enemies[j].m_topCreatureDamage=j==0 ? 20 : 0;
  const auto theirs=NativeExchangeFacts::unit(*censusView,*targets[j],1,2);
  const auto pair=NativeExchangeFacts::rangedAttack(*censusView,*shooters[0],*targets[j]);
  const auto modifier=NativeExchangeFacts::modifiers(*censusView,*targets[j]);
  ASSERT_TRUE(theirs);ASSERT_TRUE(pair);ASSERT_TRUE(modifier);
  EXPECT_EQ(modifier->first,1);EXPECT_EQ(modifier->second,5);
  EXPECT_EQ(theirs->topWound,j==0 ? 20 : 0);
  EXPECT_EQ(pair->attack,10);EXPECT_EQ(pair->defense,17);
  for(int channel=0;channel<2;++channel)
   EXPECT_DOUBLE_EQ(theirs->lossUnitValue[channel],enemies[j].getUnitCombatValue(1,2,channel,nullptr));
  SourceOrdinaryExchangeInput input;
  input.attacker=*ours;input.defender=*theirs;input.attack[1]=*pair;input.ranged=true;
  const auto actual=evaluateSourceOrdinaryExchange(input);
  ASSERT_TRUE(actual);
  const auto expected=parameters.getSimpleAttackEffect(actor,enemies[j],true,1);
  EXPECT_EQ(actual->value,expected)<<j;
  actor.considerAttack(&enemies[j],expected,1);
 }
 const auto rows=capture();
 ASSERT_TRUE(rows);ASSERT_TRUE(rows->rows[0]);ASSERT_TRUE(rows->rows[0]->target);
 ASSERT_NE(actor.AI_target,nullptr);
 EXPECT_EQ(rows->rows[0]->target->rosterIndex,actor.AI_target->index);
 EXPECT_EQ(rows->rows[0]->target->exchangeValue,actor.AI_target_value);
 EXPECT_EQ(rows->rows[0]->possibleTargets,static_cast<uint32_t>(actor.AI_possible_targets));
 EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,spellPackets);
 EXPECT_EQ(recorder.injuryPackets,injuryPackets);
 EXPECT_EQ(targets[0]->getAvailableHealth(),15*35-20);
}

TEST_F(NativeExchangeFactSpellBattle, canonicalOffenceAllMasteriesLeaveRangedExchangeAndCensusUnchanged)
{
 namespace Ref=PinnedExchange;
 using namespace AIIntegration::Ports::HoMM3;
 prepareCensus(false,TerrainId(ETerrainId::SAND));
 ASSERT_FALSE(HasFatalFailure());
 Ref::hero ownHero,enemyHero;
 Ref::army actor,target;
 fillExchangeFactBaseline(actor,target,ownHero,enemyHero);
 Ref::type_AI_combat_parameters parameters;
 std::optional<int32_t> baseline;
 std::optional<uint32_t> mask;
 std::optional<int32_t> chosen;
 for(int level=0;level<=3;++level)
 {
  SCOPED_TRACE("Offence="+std::to_string(level));
  attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::OFFENCE),level,ChangeValueMode::ABSOLUTE);
  defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::OFFENCE),level,ChangeValueMode::ABSOLUTE);
  ownHero.m_skillLevel[Ref::eSecSkillOffense]=level;
  enemyHero.m_skillLevel[Ref::eSecSkillOffense]=level;
  const auto ours=NativeExchangeFacts::unit(*censusView,*shooters[0],0,0);
  const auto theirs=NativeExchangeFacts::unit(*censusView,*targets[0],0,0);
  const auto pair=NativeExchangeFacts::rangedAttack(*censusView,*shooters[0],*targets[0]);
  ASSERT_TRUE(ours);ASSERT_TRUE(theirs);ASSERT_TRUE(pair);
  EXPECT_TRUE(pair->hasController);
  EXPECT_EQ(pair->controllerSkillFactor,0.0f);
  SourceOrdinaryExchangeInput input;
  input.attacker=*ours;input.defender=*theirs;input.attack[1]=*pair;input.ranged=true;
  const auto actual=evaluateSourceOrdinaryExchange(input);
  ASSERT_TRUE(actual);
  EXPECT_EQ(actual->value,parameters.getSimpleAttackEffect(actor,target,true,1));
  const auto rows=capture();
  ASSERT_TRUE(rows);ASSERT_TRUE(rows->rows[0]);ASSERT_TRUE(rows->rows[0]->target);
  if(!baseline)
  {
   baseline=actual->value;mask=rows->rows[0]->possibleTargets;
   chosen=rows->rows[0]->target->rosterIndex;
  }
  EXPECT_EQ(actual->value,*baseline);
  EXPECT_EQ(rows->rows[0]->target->exchangeValue,*baseline);
  EXPECT_EQ(rows->rows[0]->possibleTargets,*mask);
  EXPECT_EQ(rows->rows[0]->target->rosterIndex,*chosen);
 }
}
