#include "homm3Misfortune/PinnedMisfortune.h"
TEST(HoMM3MisfortuneSource, LuckDurationAndResistanceMatchPinnedBody)
{
 using namespace AIIntegration::Ports::HoMM3;
 for(int total:{1,59,1000,1000000})for(int luck=-3;luck<=3;++luck)
 for(int mastery=0;mastery<=3;++mastery)for(int rounds:{1,2,7})for(int turns:{1,2,6,10})
 for(bool done:{false,true})for(int success:{0,25,50,75,100})
 {
  PinnedMisfortune::army unit;unit.total=total;unit.luck=luck;unit.done=done;
  PinnedMisfortune::type_AI_spellcaster oracle;oracle.rounds=rounds;PinnedMisfortune::manager.chance=success/100.0;
  EXPECT_EQ(misfortuneSpellValue(total,luck,mastery>=2?2:1,spellDurationValue(turns,rounds,done),success),
   oracle.getMisfortuneValue(&unit,{mastery,turns,true}));
 }
}
class AIIntegrationNativeMisfortuneCasting : public AIIntegrationNativeBattle
{
protected:
 enum class Case { Ordinary,TwoFriend,Melee,Capped,Immune,Fortune,Misfortune,Resistant,ForecastWin,ForecastDead,Underdog };
 void exercise(int mastery,int baseLuck=2,Case scenario=Case::Ordinary)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  const bool mass=mastery==MasteryLevel::EXPERT;
  setArmy(attackerSideHero,CreatureID(2),scenario==Case::Underdog?1:400);
  setArmy(defenderSideHero,scenario==Case::TwoFriend?creatureByName("core:woodElf"):(scenario==Case::Melee?CreatureID(0):CreatureID(2)),200);
  if(scenario==Case::TwoFriend)ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(2).toCreature(),50));
  for(auto *hero:{attackerSideHero,defenderSideHero})hero->setSecSkillLevel(SecondarySkill::LEADERSHIP,0,ChangeValueMode::ABSOLUTE);
  if(mass)ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id,SlotID(1)),CreatureID(2).toCreature(),200));
  teachBattleSpell(attackerSideHero,SpellID::MISFORTUNE);
  attackerSideHero->setSecSkillLevel(SecondarySkill::FIRE_MAGIC,mastery,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  CStack *ally=nullptr;std::vector<CStack *> enemies;
  for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){if(u->unitSlot()==SlotID(0))ally=u.get();u->setPosition(BattleHex(87+34*u->unitSlot().getNum()));}
   else{enemies.push_back(u.get());u->setPosition(BattleHex(82+17*u->unitSlot().getNum()));}
  ASSERT_NE(ally,nullptr);ASSERT_FALSE(enemies.empty());
  for(auto *enemy:enemies)
  {
   auto luck=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::LUCK,BonusSource::OTHER,baseLuck-enemy->luckVal(),BonusSourceID());
   luck->bonusOwner=defenderSideHero->getOwner();enemy->addNewBonus(luck);ASSERT_EQ(enemy->luckVal(),baseLuck);

  }
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  battle()->activeStack=ally->unitId();
  auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  for(auto *enemy:enemies)if(scenario==Case::Melee)ASSERT_FALSE(view->battleCanShoot(enemy));else ASSERT_TRUE(view->battleCanShoot(enemy,ally->getPosition()));
  if(scenario==Case::TwoFriend)
  {
   int friendCount=0;for(const auto *unit:view->battleGetAllStacks())if(unit->unitSide()==BattleSide::ATTACKER){++friendCount;ASSERT_FALSE(unit->moved());ASSERT_FALSE(unit->defended());ASSERT_GT(enemies.front()->getInitiative(0),unit->getInitiative(0));}
   ASSERT_EQ(friendCount,2);
  }
  if(scenario==Case::Immune || scenario==Case::Fortune || scenario==Case::Misfortune || scenario==Case::Resistant)
   ASSERT_TRUE(AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally))<<"negative fixture requires positive baseline";
  for(auto *enemy:enemies)
  {
   if(scenario==Case::Immune)enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_LUCK,BonusSource::OTHER,1,BonusSourceID()));
   if(scenario==Case::Resistant)enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,100,BonusSourceID()));
   if(scenario==Case::Fortune||scenario==Case::Misfortune)
   {
    const SpellID spell=scenario==Case::Fortune?SpellID::FORTUNE:SpellID::MISFORTUNE;
    spells::BattleCast cast(battle(),scenario==Case::Fortune?defenderSideHero:attackerSideHero,spells::Mode::PASSIVE,spell.toSpell());
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
  std::vector<int> luckBefore;std::vector<int64_t> healthBefore;
  for(auto *enemy:enemies){luckBefore.push_back(enemy->luckVal());healthBefore.push_back(enemy->getAvailableHealth());}
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast);
  const auto repeated=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally,forecast);
  EXPECT_EQ(bool(selected),bool(repeated));EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,packets);
  for(size_t i=0;i<enemies.size();++i){EXPECT_EQ(enemies[i]->luckVal(),luckBefore[i]);EXPECT_EQ(enemies[i]->getAvailableHealth(),healthBefore[i]);}
  if((scenario!=Case::Ordinary && scenario!=Case::ForecastWin && scenario!=Case::TwoFriend) || baseLuck<(mastery>=MasteryLevel::ADVANCED?2:1)){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);ASSERT_TRUE(repeated);EXPECT_EQ(selected->spell,SpellID::MISFORTUNE);EXPECT_EQ(selected->toString(),repeated->toString());
  auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");policy->playerID=player;
  AutocombatPreferences prefs;prefs.enableSpellsUsage=true;policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(id,ally);ASSERT_TRUE(callback->action);ASSERT_TRUE(callback->spellAction);EXPECT_EQ(callback->action->toString(),selected->toString());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));
  const int amount=mastery>=MasteryLevel::ADVANCED?2:1;
  for(auto *enemy:enemies)EXPECT_EQ(enemy->luckVal(),std::max(-3,baseLuck-amount));
  EXPECT_LT(attackerSideHero->mana,mana);EXPECT_GT(recorder.spellPackets,packets);
 }
};
TEST_F(AIIntegrationNativeMisfortuneCasting,BasicTargeted){exercise(MasteryLevel::BASIC);}
TEST_F(AIIntegrationNativeMisfortuneCasting,AdvancedTargeted){exercise(MasteryLevel::ADVANCED);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ExpertMass){exercise(MasteryLevel::EXPERT);}
TEST_F(AIIntegrationNativeMisfortuneCasting,CrossingBelowZeroUnsupported){exercise(MasteryLevel::ADVANCED,1);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ZeroLuckConservesMana){exercise(MasteryLevel::ADVANCED,0);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ImmuneConservesMana){exercise(MasteryLevel::ADVANCED,2,Case::Immune);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ExistingFortuneCancellationUnsupported){exercise(MasteryLevel::ADVANCED,2,Case::Fortune);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ExistingMisfortuneConservesMana){exercise(MasteryLevel::ADVANCED,2,Case::Misfortune);}
TEST_F(AIIntegrationNativeMisfortuneCasting,FullResistanceConservesMana){exercise(MasteryLevel::ADVANCED,2,Case::Resistant);}
TEST_F(AIIntegrationNativeMisfortuneCasting,WinningForecastStillAllowsSourceMisfortune){exercise(MasteryLevel::ADVANCED,2,Case::ForecastWin);}
TEST_F(AIIntegrationNativeMisfortuneCasting,ProjectedDeadEnemyConservesMana){exercise(MasteryLevel::ADVANCED,2,Case::ForecastDead);}
TEST_F(AIIntegrationNativeMisfortuneCasting,UnderdogKillsOnlyExcluded){exercise(MasteryLevel::ADVANCED,2,Case::Underdog);}

TEST_F(AIIntegrationNativeMisfortuneCasting,PositiveLuckMeleeRemainsUnsupported){exercise(MasteryLevel::ADVANCED,2,Case::Melee);}
TEST_F(AIIntegrationNativeMisfortuneCasting,AdvancedTargetedWithAnotherFriendlyRemaining){exercise(MasteryLevel::ADVANCED,2,Case::TwoFriend);}
