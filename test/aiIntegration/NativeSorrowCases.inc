#include "homm3Sorrow/PinnedSorrow.h"
TEST(HoMM3SorrowSource, MoraleDurationAndResistanceMatchPinnedBody)
{
 using namespace AIIntegration::Ports::HoMM3;
 for(int total:{1,59,1000,1000000})for(int morale=-3;morale<=3;++morale)
 for(int mastery=0;mastery<=3;++mastery)for(int rounds:{1,2,7})for(int turns:{1,2,6,10})
 for(bool done:{false,true})for(int success:{0,25,50,75,100})
 {
  PinnedSorrow::army unit;unit.total=total;unit.morale=morale;unit.done=done;
  PinnedSorrow::type_AI_spellcaster oracle;oracle.rounds=rounds;PinnedSorrow::manager.chance=success/100.0;
  EXPECT_EQ(sorrowSpellValue(total,morale,mastery>=2?2:1,spellDurationValue(turns,rounds,done),success),
   oracle.getSorrowValue(&unit,{mastery,turns,true}));
 }
}
class AIIntegrationNativeSorrowCasting : public AIIntegrationNativeBattle
{
protected:
 enum class Case { Ordinary,Capped,Immune,Mirth,Sorrow,Resistant,ForecastWin,ForecastDead,Underdog };
 void exercise(int mastery,int baseMorale=0,Case scenario=Case::Ordinary)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  const bool mass=mastery==MasteryLevel::EXPERT;
  setArmy(attackerSideHero,CreatureID(2),scenario==Case::Underdog?1:400);
  setArmy(defenderSideHero,CreatureID(2),200);
  for(auto *hero:{attackerSideHero,defenderSideHero})hero->setSecSkillLevel(SecondarySkill::LEADERSHIP,0,ChangeValueMode::ABSOLUTE);
  if(mass){ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id,SlotID(1)),CreatureID(2).toCreature(),200));}
  teachBattleSpell(attackerSideHero,SpellID::SORROW);
  attackerSideHero->setSecSkillLevel(SecondarySkill::EARTH_MAGIC,mastery,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  CStack *ally=nullptr;std::vector<CStack *> enemies;
  for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){ally=u.get();u->setPosition(BattleHex(87));}
   else{enemies.push_back(u.get());u->setPosition(BattleHex(82+17*u->unitSlot().getNum()));}
  ASSERT_NE(ally,nullptr);ASSERT_FALSE(enemies.empty());
  for(auto *enemy:enemies)
  {
   auto morale=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MORALE,BonusSource::OTHER,baseMorale-enemy->moraleVal(),BonusSourceID());
   morale->bonusOwner=defenderSideHero->getOwner();enemy->addNewBonus(morale);ASSERT_EQ(enemy->moraleVal(),baseMorale);

  }
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  battle()->activeStack=ally->unitId();
  auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  for(auto *enemy:enemies)ASSERT_TRUE(view->battleCanShoot(enemy,ally->getPosition()));
  if(scenario==Case::Immune || scenario==Case::Mirth || scenario==Case::Sorrow || scenario==Case::Resistant)
   {ASSERT_TRUE(AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally))<<"negative fixture requires positive baseline";}
  for(auto *enemy:enemies)
  {
   if(scenario==Case::Immune)enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_MORALE,BonusSource::OTHER,1,BonusSourceID()));
   if(scenario==Case::Resistant)enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,100,BonusSourceID()));
   if(scenario==Case::Mirth||scenario==Case::Sorrow)
   {
    const SpellID spell=scenario==Case::Mirth?SpellID::MIRTH:SpellID::SORROW;
    spells::BattleCast cast(battle(),scenario==Case::Mirth?defenderSideHero:attackerSideHero,spells::Mode::PASSIVE,spell.toSpell());
    cast.setSpellLevel(MasteryLevel::ADVANCED);cast.setEffectDuration(6);
    cast.cast(gameHandler->spellEnv.get(),spells::Target{battle::Destination(enemy)});
    ASSERT_TRUE(enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(spell))));
   }
  }
  HoMM3Forecast::PrefixProjection forecast;
  if(scenario==Case::ForecastWin){forecast.outcome=HoMM3Forecast::PrefixOutcome{};forecast.outcome->winLikely=true;}
  if(scenario==Case::ForecastDead)
  {
   forecast.outcome=HoMM3Forecast::PrefixOutcome{};forecast.outcome->side=HoMM3Forecast::Side::ATTACKER;
   for(auto *enemy:enemies){forecast.outcome->initial[enemy->unitId()].liveHealth=enemy->getAvailableHealth();forecast.outcome->health[enemy->unitId()]=0;}
  }
  const auto mana=attackerSideHero->mana;const auto packets=recorder.spellPackets;
  std::vector<int> moraleBefore;std::vector<int64_t> healthBefore;
  for(auto *enemy:enemies){moraleBefore.push_back(enemy->moraleVal());healthBefore.push_back(enemy->getAvailableHealth());}
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast);
  const auto repeated=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast);
  EXPECT_EQ(bool(selected),bool(repeated));EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,packets);
  for(size_t i=0;i<enemies.size();++i){EXPECT_EQ(enemies[i]->moraleVal(),moraleBefore[i]);EXPECT_EQ(enemies[i]->getAvailableHealth(),healthBefore[i]);}
  if(scenario!=Case::Ordinary || baseMorale==-3){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);ASSERT_TRUE(repeated);EXPECT_EQ(selected->spell,SpellID::SORROW);EXPECT_EQ(selected->toString(),repeated->toString());
  auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");policy->playerID=player;
  AutocombatPreferences prefs;prefs.enableSpellsUsage=true;policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(id,ally);ASSERT_TRUE(callback->action);ASSERT_TRUE(callback->spellAction);EXPECT_EQ(callback->action->toString(),selected->toString());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));
  const int amount=mastery>=MasteryLevel::ADVANCED?2:1;
  for(auto *enemy:enemies)EXPECT_EQ(enemy->moraleVal(),std::max(-3,baseMorale-amount));
  EXPECT_LT(attackerSideHero->mana,mana);EXPECT_GT(recorder.spellPackets,packets);
 }
};
TEST_F(AIIntegrationNativeSorrowCasting,BasicTargeted){exercise(MasteryLevel::BASIC);}
TEST_F(AIIntegrationNativeSorrowCasting,AdvancedTargeted){exercise(MasteryLevel::ADVANCED);}
TEST_F(AIIntegrationNativeSorrowCasting,ExpertMass){exercise(MasteryLevel::EXPERT);}
TEST_F(AIIntegrationNativeSorrowCasting,PositiveMoraleCrossesZero){exercise(MasteryLevel::ADVANCED,1);}
TEST_F(AIIntegrationNativeSorrowCasting,NegativeMoraleCapConservesMana){exercise(MasteryLevel::ADVANCED,-3);}
TEST_F(AIIntegrationNativeSorrowCasting,ImmuneConservesMana){exercise(MasteryLevel::ADVANCED,0,Case::Immune);}
TEST_F(AIIntegrationNativeSorrowCasting,ExistingMirthCancellationUnsupported){exercise(MasteryLevel::ADVANCED,0,Case::Mirth);}
TEST_F(AIIntegrationNativeSorrowCasting,ExistingSorrowConservesMana){exercise(MasteryLevel::ADVANCED,0,Case::Sorrow);}
TEST_F(AIIntegrationNativeSorrowCasting,FullResistanceConservesMana){exercise(MasteryLevel::ADVANCED,0,Case::Resistant);}
TEST_F(AIIntegrationNativeSorrowCasting,WinningForecastConservesMana){exercise(MasteryLevel::ADVANCED,0,Case::ForecastWin);}
TEST_F(AIIntegrationNativeSorrowCasting,ProjectedDeadEnemyConservesMana){exercise(MasteryLevel::ADVANCED,0,Case::ForecastDead);}
TEST_F(AIIntegrationNativeSorrowCasting,UnderdogKillsOnlyExcluded){exercise(MasteryLevel::ADVANCED,0,Case::Underdog);}
class AIIntegrationNativeSorrowMelee : public AIIntegrationNativeBattle
{
protected:
 void exercise(bool exposedShooter)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  setArmy(attackerSideHero,CreatureID(0),400);setArmy(defenderSideHero,CreatureID(0),200);
  if(exposedShooter){ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(2).toCreature(),50));}
  teachBattleSpell(attackerSideHero,SpellID::SORROW);attackerSideHero->setSecSkillLevel(SecondarySkill::EARTH_MAGIC,MasteryLevel::ADVANCED,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);
  battle()->obstacles.clear();CStack *ally=nullptr,*enemy=nullptr,*shooter=nullptr;
  for(const auto &u:battle()->stacks)
  {
   if(u->unitSide()==BattleSide::DEFENDER){enemy=u.get();u->setPosition(BattleHex(89));}
   else if(u->unitSlot()==SlotID(0)){ally=u.get();u->setPosition(BattleHex(87));}
   else{shooter=u.get();u->setPosition(BattleHex(124));}
  }
  ASSERT_NE(ally,nullptr);ASSERT_NE(enemy,nullptr);battle()->activeStack=ally->unitId();
  enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MORALE,BonusSource::OTHER,-enemy->moraleVal(),BonusSourceID()));
  ASSERT_EQ(enemy->moraleVal(),0);
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
  ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::SORROW);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));EXPECT_EQ(enemy->moraleVal(),-2);
 }
};
TEST_F(AIIntegrationNativeSorrowMelee, CommittedOneOnOneMeleeDoesNotSetAttackBit){exercise(false);}
TEST_F(AIIntegrationNativeSorrowMelee, ReachableUncommittedFriendlyPermitsSorrow){exercise(true);}
