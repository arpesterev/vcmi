/* Ordinary archerDecision threat ranking. GPL-2.0-or-later.
 * ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * ai_battle.cpp:1413 and battle_troop.cpp:evaluateThreatForUnit.
 */
class AIIntegrationFHeroesRanged : public AIIntegrationFHeroesBlessCurse
{
protected:
 CStack * distant = nullptr;
 void rangedSetup()
 {
  setup(2,0,50,60);ASSERT_FALSE(HasFatalFailure());
  actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(8,5));
  distant=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(15,5),500);
  ASSERT_NE(distant,nullptr);ASSERT_TRUE(view->battleCanShoot(actor,enemy->getPosition()));
  ASSERT_TRUE(view->battleCanShoot(actor,distant->getPosition()));
  ASSERT_FALSE(view->battleHasDistancePenalty(actor,actor->getPosition(),enemy->getPosition()));
  ASSERT_TRUE(view->battleHasDistancePenalty(actor,actor->getPosition(),distant->getPosition()));
 }
 double incoming(const CStack & unit)
 {
  const auto damage=FHeroesThreatFixtureOracle::value(facts(unit),facts(*actor));
  int distance=GameConstants::BFIELD_SIZE;
  for(const auto & from:unit.getHexes())for(const auto & to:actor->getHexes())distance=std::min(distance,int(BattleHex::getDistance(from,to)));
  double result=damage.potential;
  if(distance>unit.getMovementRange()+1)result/=1.5*distance/unit.getMovementRange();
  if(unit.moved())result/=1.25;
  return result;
 }
 double nativeOutgoing(const CStack & unit)
 {
  const auto damage=view->battleEstimateDamage(BattleAttackInfo(actor,&unit,0,true));
  return AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).value(unit)
   *std::min(double(unit.getAvailableHealth()),(double(damage.damage.min)+damage.damage.max)/2)/unit.getAvailableHealth();
 }
 void differingChoice(bool fallback,bool unavailable)
 {
  rangedSetup();ASSERT_FALSE(HasFatalFailure());
  ASSERT_GT(incoming(*distant),incoming(*enemy));ASSERT_GT(nativeOutgoing(*enemy),nativeOutgoing(*distant));
  if(fallback)distant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  CStack * ignored=nullptr;
  if(unavailable)
  {
   ignored=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(15,8),1);ASSERT_NE(ignored,nullptr);
   ignored->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::INVINCIBLE,BonusSource::OTHER,1,BonusSourceID()));
  }
  std::vector<const CStack *> targets{enemy,distant};if(ignored)targets.push_back(ignored);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryRangedTargetThreats(*view,*actor,targets);
  ASSERT_EQ(bool(source),!fallback);if(source){EXPECT_EQ(source->size(),2u);}
  const auto * selected=fallback?enemy:distant;const auto id=selected->unitId();
  const auto expected=view->battleEstimateDamage(BattleAttackInfo(actor,selected,0,true));int hits=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & packet){if(packet.stackAttacking==actor->unitId())for(const auto & hit:packet.bsa)if(hit.stackAttacked==id){++hits;EXPECT_TRUE(packet.shot());EXPECT_GE(hit.damageAmount,expected.damage.min);EXPECT_LE(hit.damageAmount,expected.damage.max);}};
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::SHOOT);ASSERT_FALSE(action.target.empty());EXPECT_EQ(action.target.front().unitValue,id);});
  recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_GT(hits,0);
  if(ignored)ignored->removeBonuses(Selector::type()(BonusType::INVINCIBLE));
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics");ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesRanged,incomingThreatChangesActualShotTarget){differingChoice(false,false);}
TEST_F(AIIntegrationFHeroesRanged,unsupportedLegalTargetRestoresWholeNativeRanking){differingChoice(true,false);}
TEST_F(AIIntegrationFHeroesRanged,untargetableUnsupportedUnitDoesNotPoisonComparison){differingChoice(false,true);}
TEST_F(AIIntegrationFHeroesRanged,partialHealthKeepsUncappedIncomingThreat)
{
 rangedSetup();ASSERT_FALSE(HasFatalFailure());int64_t wound=7;distant->damage(wound);
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryRangedTargetThreats(*view,*actor,{enemy,distant});ASSERT_TRUE(source);
 const auto oracle=FHeroesThreatFixtureOracle::value(facts(*distant),facts(*actor));
 EXPECT_EQ(source->at(distant->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(distant->unitId()).retaliation,oracle.retaliation);
 EXPECT_GT(oracle.potential,actor->getAvailableHealth());
}
TEST_F(AIIntegrationFHeroesRanged,blessedTargetUsesIncomingSourceDamage)
{
 rangedSetup();ASSERT_FALSE(HasFatalFailure());applyMode(distant,1,1);ASSERT_FALSE(HasFatalFailure());
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryRangedTargetThreats(*view,*actor,{enemy,distant});ASSERT_TRUE(source);
 auto d=facts(*distant);d.damageMode=1;const auto oracle=FHeroesThreatFixtureOracle::value(d,facts(*actor));
 EXPECT_EQ(source->at(distant->unitId()).potential,oracle.potential);
}
TEST_F(AIIntegrationFHeroesRanged,depletedActiveArcherRejectsRangedComparison)
{
 rangedSetup();ASSERT_FALSE(HasFatalFailure());actor->shots.use(actor->shots.available());
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryRangedTargetThreats(*view,*actor,{enemy,distant}));
}
TEST_F(AIIntegrationFHeroesRanged,rangedReturnFireRejectsWholeSourceComparison)
{
 rangedSetup();ASSERT_FALSE(HasFatalFailure());
 distant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::RANGED_RETALIATION,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryRangedTargetThreats(*view,*actor,{enemy,distant}));
}
