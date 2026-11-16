/* Unmodified public Archery skill -> source valuation; GPL-2.0-or-later. */
#include "../../lib/entities/hero/CHero.h"

class AIIntegrationFHeroesArchery : public AIIntegrationFHeroesOrdinaryGeometry
{
protected:
 static unsigned percent(int level){return level==1?10:level==2?25:50;}
 static AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit skilledFacts(const CStack & u,unsigned bonus)
 {auto value=facts(u);value.archeryPercent=bonus;return value;}
 void archeryPursuit(int level,bool fallback)
 {
  setup(0,2);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(36));
  auto * alternative=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(138),23);ASSERT_NE(alternative,nullptr);
  defenderSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  auto attack=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,20,BonusSourceID());attack->subtype=BonusSubtypeID(PrimarySkill::ATTACK);enemy->addNewBonus(attack);
  for(const auto * u:{enemy,alternative}){ASSERT_TRUE(view->battleCanShoot(u));ASSERT_FALSE(view->battleHasShootingPenalty(u,actor->getPosition()));}
  const auto a=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*enemy,percent(level)),facts(*actor));
  const auto b=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*alternative,percent(level)),facts(*actor));ASSERT_GT(a.potential,b.potential);
  const auto native=[&](const CStack *u){const auto d=view->battleEstimateDamage(BattleAttackInfo(u,actor,8,true));return(double(d.damage.min)+d.damage.max)/2;};ASSERT_LT(native(enemy),native(alternative));
  const auto routes=view->getReachability(actor);
  const auto shortest=[&](const CStack * u){int best=999;for(const auto & hex:u->getAttackableHexes(actor))if(hex.isValid()&&view->isMeleeAttackPossible(actor,u,hex))best=std::min(best,int(routes.distances[hex.toInt()]));return best;};
  ASSERT_LT(shortest(enemy),GameConstants::BFIELD_SIZE);ASSERT_EQ(shortest(enemy),shortest(alternative));ASSERT_GT(shortest(enemy),actor->getMovementRange());
  if(fallback) alternative->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy,alternative});ASSERT_EQ(bool(source),!fallback);
  if(source){EXPECT_EQ(source->at(enemy->unitId()).potential,a.potential);EXPECT_EQ(source->at(alternative->unitId()).potential,b.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,0u);}
  chooseAndApply([&](const BattleAction & action){
   ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);const auto dest=action.target.front().hexValue;
   EXPECT_LT(BattleHex::getDistance(dest,(fallback?alternative:enemy)->getPosition()),BattleHex::getDistance(dest,(fallback?enemy:alternative)->getPosition()));
   std::cout<<"FHEROES_ARCHERY level="<<level<<" fallback="<<fallback<<" source="<<a.potential<<","<<b.potential<<" native="<<native(enemy)<<","<<native(alternative)<<" action="<<action.toString()<<'\n';
  });
 }
 void completeArchery(int level)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());setArmy(attackerSideHero,CreatureID(3),30);setArmy(defenderSideHero,CreatureID(6),30);
  attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  map()->getTile(defenderSideHero->visitablePos()).terrainType=ETerrainId::SAND;
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);
  for(const auto & u:battle()->stacks){const auto certificate=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*u,u->isShooter());ASSERT_TRUE(certificate);EXPECT_EQ(certificate->archeryPercent,u->unitSide()==BattleSide::ATTACKER?percent(level):0u);}
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_GT(recorder.shots,0);EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesArchery,threeNativeLevelsMatchPinnedRangedEndpoints)
{
 setup(0,2);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(36));
 for(int level=1;level<=3;++level)
 {
  defenderSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  const auto certificate=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*enemy,true);ASSERT_TRUE(certificate);EXPECT_EQ(certificate->archeryPercent,percent(level));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy});ASSERT_TRUE(source);
  const auto expected=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*enemy,percent(level)),facts(*actor));
  EXPECT_EQ(source->at(enemy->unitId()).potential,expected.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,0u);
 }
}
TEST_F(AIIntegrationFHeroesArchery,skillNeverBoostsBlockedDepletedOrRetaliatoryDamage)
{
 setup(0,2,10,100);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(94));
 for(int level=1;level<=3;++level)
 {
  defenderSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);enemy->shots.reset();
  for(const bool ammo:{true,false})
  {
   if(!ammo)enemy->shots.use(enemy->shots.available());
   const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy});ASSERT_TRUE(source);
   const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(skilledFacts(*enemy,percent(level)),facts(*actor),ammo);
   const auto unskilled=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*enemy),facts(*actor),ammo);
   EXPECT_EQ(source->at(enemy->unitId()).potential,expected.potential);EXPECT_EQ(expected.potential,unskilled.potential);
   const auto reverse=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(reverse);
   const auto retaliatory=FHeroesThreatFixtureOracle::archerDefenderValue(facts(*actor),skilledFacts(*enemy,percent(level)),ammo);
   EXPECT_EQ(reverse->at(actor->unitId()).retaliation,retaliatory.retaliation);EXPECT_GT(retaliatory.retaliation,0u);
   EXPECT_EQ(retaliatory.retaliation,FHeroesThreatFixtureOracle::archerDefenderValue(facts(*actor),facts(*enemy),ammo).retaliation);
  }
 }
}
TEST_F(AIIntegrationFHeroesArchery,duplicateAndArtifactArcheryRejectWholeComparison)
{
 setup(0,2);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(36));
 defenderSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,1,ChangeValueMode::ABSOLUTE);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
 const auto bonuses=enemy->getAllBonuses(Selector::type()(BonusType::PERCENTAGE_DAMAGE_BOOST));ASSERT_EQ(bonuses->size(),1u);
 auto duplicate=std::make_shared<Bonus>(*bonuses->front());enemy->addNewBonus(duplicate);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));enemy->removeBonus(duplicate);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
 giveArtifact(defenderSideHero,ArtifactID(60),ArtifactPosition::MISC1); // Bow of Elven Cherrywood
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
}
TEST_F(AIIntegrationFHeroesArchery,realHeroArcherySpecialtyIsNotOrdinarySkill)
{
 setup(2);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(100));
 attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,1,ChangeValueMode::ABSOLUTE);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
 ASSERT_FALSE(attackerSideHero->getHeroType()->specialty.empty());
 for(const auto & bonus:attackerSideHero->getHeroType()->specialty)attackerSideHero->addNewBonus(bonus);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}
TEST(FHeroes2ArcherySource,endpointOrderAndTruncationMatchesPinnedBody)
{
 using namespace AIIntegration::Ports::FHeroes2;
 for(unsigned count:{1u,3u,11u})for(int attack:{0,5,25,40})for(int defense:{0,5,30})for(unsigned skill:{10u,25u,50u})
 {
  const OrdinaryThreatUnit a{count,count*10,10,3,5,attack,5,true,skill};const OrdinaryThreatUnit d{100,1000,10,2,3,5,defense,true};
  const auto expected=FHeroesThreatFixtureOracle::rangedValue(a,d);const auto actual=ordinaryThreatDamage(a,d,false,true);ASSERT_TRUE(actual);
  EXPECT_EQ(actual->potential,expected.potential);EXPECT_EQ(actual->retaliation,expected.retaliation);
 }
}
TEST_F(AIIntegrationFHeroesArchery,basicSourcePursuit){archeryPursuit(1,false);}
TEST_F(AIIntegrationFHeroesArchery,basicNativeFallbackPursuit){archeryPursuit(1,true);}
TEST_F(AIIntegrationFHeroesArchery,advancedSourcePursuit){archeryPursuit(2,false);}
TEST_F(AIIntegrationFHeroesArchery,advancedNativeFallbackPursuit){archeryPursuit(2,true);}
TEST_F(AIIntegrationFHeroesArchery,expertSourcePursuit){archeryPursuit(3,false);}
TEST_F(AIIntegrationFHeroesArchery,expertNativeFallbackPursuit){archeryPursuit(3,true);}
TEST_F(AIIntegrationFHeroesArchery,basicFactoryBattleCompletes){completeArchery(1);}
TEST_F(AIIntegrationFHeroesArchery,advancedFactoryBattleCompletes){completeArchery(2);}
TEST_F(AIIntegrationFHeroesArchery,expertFactoryBattleCompletes){completeArchery(3);}
