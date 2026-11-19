#include "homm3Weakness/PinnedWeakness.h"
TEST(HoMM3WeaknessSource,PositiveHypotheticalAttackBoostMatchesPinnedBodies)
{
 using namespace AIIntegration::Ports::HoMM3;
 for(int total:{1,59,1000,1000000})for(int attack:{0,1,6,20})for(int mastery=0;mastery<=3;++mastery)
 for(int damage:{1,3,101})for(int health:{1,100,1000})for(int rounds:{1,3})for(int turns:{1,6})for(bool done:{false,true})
 {
  PinnedWeakness::army target;target.health=health;
  PinnedWeakness::army unit;unit.total=total;unit.damage=std::min(damage,health);unit.done=done;unit.target=&target;
  unit.originalAttack=unit.m_monInfo.attackSkill=attack;unit.oldEstimate=100;
  unit.newEstimate=100+std::min(attack,mastery>=2?6:3)*5;
  PinnedWeakness::type_AI_spellcaster oracle;oracle.rounds=rounds;
  const auto actual=weaknessSpellValue(total,unit.damage,health,unit.oldEstimate,unit.newEstimate,spellDurationValue(turns,rounds,done));
  ASSERT_TRUE(actual);EXPECT_EQ(*actual,oracle.getWeaknessValue(&unit,{mastery,turns}));
 }
}
class AIIntegrationNativeWeakness : public AIIntegrationNativeBattle
{
protected:
 enum class Case{Ordinary,Existing,Resistant,ForecastWin,ForecastDead,Underdog,Overkill};
 void exercise(int mastery,Case scenario=Case::Ordinary)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());const bool mass=mastery==MasteryLevel::EXPERT;
  setArmy(attackerSideHero,CreatureID(2),scenario==Case::Underdog||scenario==Case::Overkill?1:400);
  setArmy(defenderSideHero,CreatureID(2),200);
  if(mass){ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id,SlotID(1)),CreatureID(2).toCreature(),200));}
  teachBattleSpell(attackerSideHero,SpellID::WEAKNESS);attackerSideHero->setSecSkillLevel(SecondarySkill::WATER_MAGIC,mastery,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  CStack *active=nullptr;std::vector<CStack *> enemies;
  for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){active=u.get();u->setPosition(BattleHex(87));}else{enemies.push_back(u.get());u->setPosition(BattleHex(82+17*u->unitSlot().getNum()));}
  ASSERT_NE(active,nullptr);ASSERT_FALSE(enemies.empty());battle()->activeStack=active->unitId();
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  if(scenario==Case::Ordinary)for(auto *enemy:enemies)
  {
   namespace P=AIIntegration::Ports::HoMM3;
   const auto facts=P::NativeExchangeFacts::unit(*view,*enemy,0,0);ASSERT_TRUE(facts);
   const auto pair=P::NativeExchangeFacts::rangedAttack(*view,*enemy,*active);ASSERT_TRUE(pair);
   ASSERT_FALSE(pair->lucky);ASSERT_FALSE(pair->wallPenalty);ASSERT_EQ(pair->controllerSkillFactor,0);ASSERT_EQ(pair->defenderReduction,1);
   ASSERT_EQ(pair->attack,enemy->getAttack(true));ASSERT_EQ(pair->defense,active->getDefense(true));
   // Independent ordinary source strike arithmetic: truncate attack bonus,
   // then distance reduction. The estimate starts at fixed100, not100troops.
   const auto original=[&](int base,int attack,int limit)
   {
    int bonus=attack>=active->getDefense(true)?static_cast<int>(base*std::min((attack-active->getDefense(true))*0.05,3.0)):0;
    double reduction=attack<active->getDefense(true)?std::max(1.0-(active->getDefense(true)-attack)*0.025,0.3):1.0;
    if(pair->distancePenalty)reduction*=0.5;
    return std::min(limit,std::max(1,static_cast<int>((base+bonus)*reduction)));
   };
   P::SourceOrdinaryUnit fixed;fixed.minimumDamage=fixed.maximumDamage=100;
   auto increased=*pair;increased.attack+=std::min(mastery>=MasteryLevel::ADVANCED?6:3,pair->attack);
   const auto oldEstimate=P::OrdinaryExchangeDetail::damage(fixed,1,INT32_MAX,*pair,true);
   const auto newEstimate=P::OrdinaryExchangeDetail::damage(fixed,1,INT32_MAX,increased,true);
   const auto actualAverage=P::OrdinaryExchangeDetail::damage(*facts,enemy->getAvailableHealth(),active->getAvailableHealth(),*pair,true);
   ASSERT_TRUE(oldEstimate);ASSERT_TRUE(newEstimate);ASSERT_TRUE(actualAverage);
   EXPECT_EQ(*oldEstimate,original(100,enemy->getAttack(true),INT32_MAX));
   EXPECT_EQ(*newEstimate,original(100,enemy->getAttack(true)+std::min(mastery>=MasteryLevel::ADVANCED?6:3,enemy->getAttack(true)),INT32_MAX));
   const int base=static_cast<int>(enemy->getCount()*(enemy->getMinDamage(true)+enemy->getMaxDamage(true))/2.0);
   EXPECT_EQ(*actualAverage,original(base,enemy->getAttack(true),active->getAvailableHealth()));
  }
  if(scenario==Case::Existing||scenario==Case::Resistant){ASSERT_TRUE(AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active));}
  for(auto *enemy:enemies)
  {
   ASSERT_TRUE(view->battleCanShoot(enemy,active->getPosition()));
   if(scenario==Case::Resistant)enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,100,BonusSourceID()));
   if(scenario==Case::Existing){spells::BattleCast cast(battle(),attackerSideHero,spells::Mode::PASSIVE,SpellID(SpellID::WEAKNESS).toSpell());cast.setSpellLevel(MasteryLevel::ADVANCED);cast.setEffectDuration(6);cast.cast(gameHandler->spellEnv.get(),spells::Target{battle::Destination(enemy)});ASSERT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::WEAKNESS)))));}
  }
  HoMM3Forecast::PrefixProjection forecast;
  if(scenario==Case::ForecastWin){forecast.outcome=HoMM3Forecast::PrefixOutcome{};forecast.outcome->winLikely=true;}
  if(scenario==Case::ForecastDead){forecast.outcome=HoMM3Forecast::PrefixOutcome{};forecast.outcome->side=HoMM3Forecast::Side::ATTACKER;for(auto *enemy:enemies){forecast.outcome->initial[enemy->unitId()].liveHealth=enemy->getAvailableHealth();forecast.outcome->health[enemy->unitId()]=0;}}
  const auto mana=attackerSideHero->mana;const auto packets=recorder.spellPackets;std::vector<int> beforeAttack;std::vector<int64_t> beforeHealth;
  for(auto *enemy:enemies){beforeAttack.push_back(enemy->getAttack(false));beforeHealth.push_back(enemy->getAvailableHealth());}
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active,forecast),repeated=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active,forecast);
  EXPECT_EQ(bool(selected),bool(repeated));EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,packets);
  for(size_t i=0;i<enemies.size();++i){EXPECT_EQ(enemies[i]->getAttack(false),beforeAttack[i]);EXPECT_EQ(enemies[i]->getAvailableHealth(),beforeHealth[i]);}
  if(scenario==Case::Resistant)
  {
   // The isolated source price has no resistance multiplier, but native
   // ResistanceCondition excludes a 100%-resistant hostile target entirely.
   // Do not confuse scalar valuation with the engine's legal target domain.
   spells::BattleCast legality(view.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::WEAKNESS).toSpell());
   const auto mechanics=SpellID(SpellID::WEAKNESS).toSpell()->battleMechanics(&legality);
   for(auto *enemy:enemies){ASSERT_EQ(enemy->magicResistance(),100);EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{battle::Destination(enemy)}));}
   EXPECT_FALSE(selected);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,packets);return;
  }
  if(scenario!=Case::Ordinary){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);ASSERT_TRUE(repeated);EXPECT_EQ(selected->spell,SpellID::WEAKNESS);EXPECT_EQ(selected->toString(),repeated->toString());
  auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");policy->playerID=player;AutocombatPreferences prefs;prefs.enableSpellsUsage=true;policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(id,active);ASSERT_TRUE(callback->action);ASSERT_TRUE(callback->spellAction);EXPECT_EQ(callback->action->toString(),selected->toString());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));
  for(size_t i=0;i<enemies.size();++i)EXPECT_EQ(enemies[i]->getAttack(false),std::max(0,beforeAttack[i]-(mastery>=MasteryLevel::ADVANCED?6:3)));
  EXPECT_LT(attackerSideHero->mana,mana);EXPECT_GT(recorder.spellPackets,packets);
 }
};
TEST_F(AIIntegrationNativeWeakness,BasicTargetedCast){exercise(MasteryLevel::BASIC);}
TEST_F(AIIntegrationNativeWeakness,AdvancedTargetedCast){exercise(MasteryLevel::ADVANCED);}
TEST_F(AIIntegrationNativeWeakness,ExpertMassCast){exercise(MasteryLevel::EXPERT);}
TEST_F(AIIntegrationNativeWeakness,ExistingWeaknessConservesMana){exercise(MasteryLevel::ADVANCED,Case::Existing);}
TEST_F(AIIntegrationNativeWeakness,NativeFullResistanceExcludesTargetBeforeSourcePrice){exercise(MasteryLevel::ADVANCED,Case::Resistant);}
TEST_F(AIIntegrationNativeWeakness,WinningForecastConservesMana){exercise(MasteryLevel::ADVANCED,Case::ForecastWin);}
TEST_F(AIIntegrationNativeWeakness,ProjectedDeadEnemyConservesMana){exercise(MasteryLevel::ADVANCED,Case::ForecastDead);}
TEST_F(AIIntegrationNativeWeakness,UnderdogKillsOnlyExcluded){exercise(MasteryLevel::ADVANCED,Case::Underdog);}

class AIIntegrationNativeWeaknessMelee : public AIIntegrationNativeBattle
{
protected:
 void exercise(bool exposedShooter)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  setArmy(attackerSideHero,CreatureID(0),400);setArmy(defenderSideHero,CreatureID(0),200);
  if(exposedShooter){ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(2).toCreature(),50));}
  teachBattleSpell(attackerSideHero,SpellID::WEAKNESS);attackerSideHero->setSecSkillLevel(SecondarySkill::WATER_MAGIC,MasteryLevel::ADVANCED,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);
  battle()->obstacles.clear();CStack *ally=nullptr,*enemy=nullptr,*shooter=nullptr;
  for(const auto &u:battle()->stacks)
  {
   if(u->unitSide()==BattleSide::DEFENDER){enemy=u.get();u->setPosition(BattleHex(89));}
   else if(u->unitSlot()==SlotID(0)){ally=u.get();u->setPosition(BattleHex(87));}
   else{shooter=u.get();u->setPosition(BattleHex(124));}
  }
  ASSERT_NE(ally,nullptr);ASSERT_NE(enemy,nullptr);battle()->activeStack=ally->unitId();
  const int beforeAttack=enemy->getAttack(false);
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  const auto own=AIIntegration::Ports::HoMM3::captureNativeOrdinaryCensus(*view,BattleSide::ATTACKER,environment.get(),true);
  const auto opposing=AIIntegration::Ports::HoMM3::captureNativeOrdinaryCensus(*view,BattleSide::DEFENDER,environment.get(),true);
  ASSERT_TRUE(own);ASSERT_TRUE(opposing);ASSERT_TRUE(own->rows.at(0));ASSERT_TRUE(opposing->rows.at(0));
  ASSERT_TRUE(own->rows.at(0)->target);ASSERT_LE(own->rows.at(0)->target->attackTime,1);
  ASSERT_EQ(own->enemyIds.at(own->rows.at(0)->target->rosterIndex),enemy->unitId());
  ASSERT_NE(opposing->rows.at(0)->possibleTargets&1u,0u);
  if(exposedShooter){ASSERT_NE(shooter,nullptr);ASSERT_TRUE(view->battleCanShoot(shooter));ASSERT_NE(opposing->rows.at(0)->possibleTargets&2u,0u);}
  // Geometry was explicitly constructed above; this direct-policy test supplies
  // the static-map certificate. It does not assert fresh factory lifecycle.
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,{},true);
  if(!exposedShooter){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::WEAKNESS);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));EXPECT_EQ(enemy->getAttack(false),std::max(0,beforeAttack-6));
 }
};
TEST_F(AIIntegrationNativeWeaknessMelee, CommittedOneOnOneMeleeDoesNotSetAttackBit){exercise(false);}
TEST_F(AIIntegrationNativeWeaknessMelee, ReachableUncommittedFriendlyPermitsWeakness){exercise(true);}

TEST(HoMM3WeaknessSource,InvalidInputsAndSourceOverflowFailClosed)
{
 using AIIntegration::Ports::HoMM3::weaknessSpellValue;
 EXPECT_FALSE(weaknessSpellValue(1000,0,100,100,120,1));
 EXPECT_FALSE(weaknessSpellValue(1000,10,100,0,120,1));
 EXPECT_FALSE(weaknessSpellValue(1000,101,100,100,120,1));
 EXPECT_FALSE(weaknessSpellValue(-1,10,100,100,120,1));
 EXPECT_FALSE(weaknessSpellValue(1000,10,100,100,120,std::numeric_limits<double>::quiet_NaN()));
 EXPECT_FALSE(weaknessSpellValue(1000,INT32_MAX,INT32_MAX,1,INT32_MAX,1));
 const auto capped=weaknessSpellValue(1000,100,100,100,120,1);ASSERT_TRUE(capped);EXPECT_EQ(*capped,0);
}
