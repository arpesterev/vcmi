#include "homm3Fortune/PinnedFortune.h"
TEST(HoMM3FortuneSource,SignedLuckOddDamageAndDurationMatchPinnedBodies)
{
 using namespace AIIntegration::Ports::HoMM3;
 for(int total:{1,59,1000,1000000})for(int luck=-3;luck<=3;++luck)for(int mastery=0;mastery<=3;++mastery)
 for(int damage:{0,1,3,24,101})for(int health:{1,3,100,1000})for(int rounds:{1,3})for(int turns:{1,6})for(bool done:{false,true})
 {
  PinnedFortune::army enemy;enemy.health=health;
  PinnedFortune::army own;own.total=total;own.luck=luck;own.damage=damage;own.done=done;own.target=&enemy;
  PinnedFortune::type_AI_spellcaster oracle;oracle.rounds=rounds;
  const auto actual=fortuneSpellValue(total,luck,mastery>=2?2:1,damage,health,spellDurationValue(turns,rounds,done));
  ASSERT_TRUE(actual);EXPECT_EQ(*actual,oracle.getFortuneValue(&own,{mastery,turns}));
 }
}
class AIIntegrationNativeFortune : public AIIntegrationNativeBattle
{
protected:
 enum class Case { Ordinary,TwoFriend,Nonzero,ExistingFortune,ExistingMisfortune,Immune,Overkill };
 void exercise(int mastery,Case scenario=Case::Ordinary)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());const bool mass=mastery==MasteryLevel::EXPERT;
  setArmy(attackerSideHero,CreatureID(2),200);setArmy(defenderSideHero,CreatureID(2),scenario==Case::Overkill?1:1000);
  if(mass || scenario==Case::TwoFriend)ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(2).toCreature(),100));
  teachBattleSpell(attackerSideHero,SpellID::FORTUNE);attackerSideHero->setSecSkillLevel(SecondarySkill::AIR_MAGIC,mastery,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  std::vector<CStack *> allies;CStack *enemy=nullptr;
  for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){allies.push_back(u.get());u->setPosition(BattleHex(87+34*u->unitSlot().getNum()));}else{enemy=u.get();u->setPosition(BattleHex(82));}
  ASSERT_FALSE(allies.empty());ASSERT_NE(enemy,nullptr);auto *active=allies.front();battle()->activeStack=active->unitId();
  for(auto *u:allies)ASSERT_EQ(u->luckVal(),0);
  if(scenario==Case::TwoFriend){ASSERT_EQ(allies.size(),2u);ASSERT_FALSE(allies.back()->moved());ASSERT_FALSE(allies.back()->defended());}
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  for(auto *u:allies)ASSERT_TRUE(view->battleCanShoot(u,enemy->getPosition()));
  if(scenario==Case::Nonzero||scenario==Case::ExistingFortune||scenario==Case::ExistingMisfortune||scenario==Case::Immune)
   ASSERT_TRUE(AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active));
  for(auto *u:allies)
  {
   if(scenario==Case::Nonzero)u->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::LUCK,BonusSource::OTHER,1,BonusSourceID()));
   if(scenario==Case::Immune)u->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_LUCK,BonusSource::OTHER,1,BonusSourceID()));
   if(scenario==Case::ExistingFortune||scenario==Case::ExistingMisfortune)
   {
    const SpellID spell=scenario==Case::ExistingFortune?SpellID::FORTUNE:SpellID::MISFORTUNE;
    spells::BattleCast cast(battle(),scenario==Case::ExistingFortune?attackerSideHero:defenderSideHero,spells::Mode::PASSIVE,spell.toSpell());
    cast.setSpellLevel(MasteryLevel::ADVANCED);cast.setEffectDuration(6);cast.cast(gameHandler->spellEnv.get(),spells::Target{battle::Destination(u)});
    ASSERT_TRUE(u->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(spell))));
   }
  }
  const auto mana=attackerSideHero->mana;const auto packets=recorder.spellPackets;
  std::vector<int> luckBefore;std::vector<int64_t> healthBefore;
  for(auto *u:allies){luckBefore.push_back(u->luckVal());healthBefore.push_back(u->getAvailableHealth());}
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active);
  const auto repeated=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active);
  EXPECT_EQ(bool(selected),bool(repeated));EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(recorder.spellPackets,packets);
  for(size_t i=0;i<allies.size();++i){EXPECT_EQ(allies[i]->luckVal(),luckBefore[i]);EXPECT_EQ(allies[i]->getAvailableHealth(),healthBefore[i]);}
  if(scenario!=Case::Ordinary && scenario!=Case::TwoFriend){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);ASSERT_TRUE(repeated);EXPECT_EQ(selected->spell,SpellID::FORTUNE);EXPECT_EQ(selected->toString(),repeated->toString());
  auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");policy->playerID=player;AutocombatPreferences prefs;prefs.enableSpellsUsage=true;policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(id,active);ASSERT_TRUE(callback->action);ASSERT_TRUE(callback->spellAction);EXPECT_EQ(callback->action->toString(),selected->toString());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));
  for(auto *u:allies)EXPECT_EQ(u->luckVal(),mass||u==active?(mastery>=MasteryLevel::ADVANCED?2:1):0);
  EXPECT_LT(attackerSideHero->mana,mana);EXPECT_GT(recorder.spellPackets,packets);
 }
};
TEST_F(AIIntegrationNativeFortune,BasicTargetedCast){exercise(MasteryLevel::BASIC);}
TEST_F(AIIntegrationNativeFortune,AdvancedTargetedCast){exercise(MasteryLevel::ADVANCED);}
TEST_F(AIIntegrationNativeFortune,ExpertMassCast){exercise(MasteryLevel::EXPERT);}
TEST_F(AIIntegrationNativeFortune,BasicActiveBuffWithAnotherFriendlyRemaining){exercise(MasteryLevel::BASIC,Case::TwoFriend);}
TEST_F(AIIntegrationNativeFortune,AdvancedActiveBuffWithAnotherFriendlyRemaining){exercise(MasteryLevel::ADVANCED,Case::TwoFriend);}
TEST_F(AIIntegrationNativeFortune,NonzeroLuckUnsupported){exercise(MasteryLevel::ADVANCED,Case::Nonzero);}
TEST_F(AIIntegrationNativeFortune,ExistingFortuneConservesMana){exercise(MasteryLevel::ADVANCED,Case::ExistingFortune);}
TEST_F(AIIntegrationNativeFortune,MisfortuneCancellationUnsupported){exercise(MasteryLevel::ADVANCED,Case::ExistingMisfortune);}
TEST_F(AIIntegrationNativeFortune,LuckImmuneConservesMana){exercise(MasteryLevel::ADVANCED,Case::Immune);}
TEST_F(AIIntegrationNativeFortune,OverkillHasNoFortuneGain){exercise(MasteryLevel::ADVANCED,Case::Overkill);}
class AIIntegrationNativeFortuneMelee : public AIIntegrationNativeBattle
{
protected:
 void exercise(bool distant,bool certified)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());setArmy(attackerSideHero,CreatureID(0),200);setArmy(defenderSideHero,CreatureID(0),1000);
  teachBattleSpell(attackerSideHero,SpellID::FORTUNE);attackerSideHero->setSecSkillLevel(SecondarySkill::AIR_MAGIC,MasteryLevel::ADVANCED,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  CStack *active=nullptr,*enemy=nullptr;for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){active=u.get();u->setPosition(BattleHex(87));}else{enemy=u.get();u->setPosition(BattleHex(distant?82:89));}
  ASSERT_NE(active,nullptr);ASSERT_NE(enemy,nullptr);battle()->activeStack=active->unitId();ASSERT_EQ(active->luckVal(),0);
  const auto id=battle()->getBattleID();const auto player=attackerSideHero->getOwner();auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());const auto view=callback->getBattle(id);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  const auto census=AIIntegration::Ports::HoMM3::captureNativeOrdinaryCensus(*view,BattleSide::ATTACKER,environment.get(),true);ASSERT_TRUE(census);ASSERT_TRUE(census->rows.at(0));
  if(!distant){ASSERT_TRUE(census->rows.at(0)->target);ASSERT_LE(census->rows.at(0)->target->attackTime,1);}
  else if(census->rows.at(0)->target)ASSERT_GT(census->rows.at(0)->target->attackTime,1);
  const auto selected=AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*active,{},certified);
  if(distant||!certified){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);EXPECT_EQ(selected->spell,SpellID::FORTUNE);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*selected));EXPECT_EQ(active->luckVal(),2);
 }
};
TEST_F(AIIntegrationNativeFortuneMelee,CertifiedImmediateMeleeTargetCast){exercise(false,true);}
TEST_F(AIIntegrationNativeFortuneMelee,UncertifiedMeleeTargetDeclined){exercise(false,false);}
TEST_F(AIIntegrationNativeFortuneMelee,RemoteSourceTargetHasNoFortuneValue){exercise(true,true);}
TEST(HoMM3FortuneSource,InvalidAndSourceOverflowInputsFailClosed)
{
 using AIIntegration::Ports::HoMM3::fortuneSpellValue;
 EXPECT_FALSE(fortuneSpellValue(-1,0,1,3,100,1));
 EXPECT_FALSE(fortuneSpellValue(1000,4,1,3,100,1));
 EXPECT_FALSE(fortuneSpellValue(1000,0,3,3,100,1));
 EXPECT_FALSE(fortuneSpellValue(1000,0,1,-1,100,1));
 EXPECT_FALSE(fortuneSpellValue(1000,0,1,3,0,1));
 EXPECT_FALSE(fortuneSpellValue(1000,0,1,3,100,std::numeric_limits<double>::quiet_NaN()));
 EXPECT_FALSE(fortuneSpellValue(1000,0,1,INT32_MAX,INT32_MAX,1));
 const auto overkill=fortuneSpellValue(1000,0,2,100,1,1);ASSERT_TRUE(overkill);EXPECT_EQ(*overkill,0);
}
