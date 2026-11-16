/* Source ordinary melee priorities. GPL-2.0-or-later.
 * ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541:
 * ai_battle.cpp optimalAttackValue/evaluatePotentialAttackPositions.
 */
class AIIntegrationFHeroesMeleeSource : public AIIntegrationFHeroesOrdinaryGeometry
{
protected:
 CStack * alternative=nullptr;
 void contactSetup(int actorType=0,int otherCount=500,int distance=4)
 {
  setup(actorType,0,50,60);ASSERT_FALSE(HasFatalFailure());
  actor->setPosition(BattleHex(6,5));enemy->setPosition(BattleHex(7,5));
  alternative=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(6+distance,5),otherCount);ASSERT_NE(alternative,nullptr);
 }
 auto source(const std::vector<const CStack *> & targets)
 {
  const auto reach=view->getReachability(actor);const auto positions=view->battleGetAvailableHexes(reach,actor,false);
  return AIIntegration::Ports::FHeroes2::ordinaryMeleeTargetThreats(*view,*actor,reach,positions,targets);
 }
 double nativeValue(const CStack * target)
 {
  BattleAttackInfo attack(actor,target,0,false);DamageEstimation retaliation;const auto outgoing=view->battleEstimateDamage(attack,&retaliation);
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel scale(*view);
  const auto loss=[&](const CStack * unit,const DamageEstimation & damage){return scale.value(*unit)*std::min(double(unit->getAvailableHealth()),(double(damage.damage.min)+damage.damage.max)/2)/unit->getAvailableHealth();};
  return loss(target,outgoing)-loss(actor,retaliation);
 }
 void chooseTarget(const CStack * expected)
 {
  const auto id=expected->unitId();int hits=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & packet){if(packet.stackAttacking==actor->unitId())for(const auto & hit:packet.bsa)if(hit.stackAttacked==id){++hits;EXPECT_FALSE(packet.shot());EXPECT_GT(hit.damageAmount,0);}};
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);ASSERT_GE(action.target.size(),2u);const auto * target=view->battleGetUnitByPos(action.target[1].hexValue);ASSERT_NE(target,nullptr);EXPECT_EQ(target->unitId(),id);});
  recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_GT(hits,0);
 }
 void differing(bool fallback)
 {
  contactSetup();ASSERT_FALSE(HasFatalFailure());
  const auto nearDamage=FHeroesThreatFixtureOracle::value(facts(*enemy),facts(*actor));
  const auto farDamage=FHeroesThreatFixtureOracle::value(facts(*alternative),facts(*actor));
  ASSERT_GT(farDamage.potential,nearDamage.potential);ASSERT_GT(nativeValue(enemy),nativeValue(alternative));
  if(fallback)alternative->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto certified=source({enemy,alternative});ASSERT_EQ(bool(certified),!fallback);
  chooseTarget(fallback?enemy:alternative);ASSERT_FALSE(HasFatalFailure());
  if(!gameState()->currentBattles.empty())runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics");
  ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesMeleeSource,incomingThreatChangesActualMeleeTarget){differing(false);}
TEST_F(AIIntegrationFHeroesMeleeSource,unsupportedCandidateRestoresWholeNativeRanking){differing(true);}
TEST_F(AIIntegrationFHeroesMeleeSource,unreachableUnsupportedTargetDoesNotPoisonCurrentCandidates)
{
 contactSetup(0,500,9);ASSERT_FALSE(HasFatalFailure());
 alternative->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 const auto certified=source({enemy,alternative});ASSERT_TRUE(certified);EXPECT_EQ(certified->size(),1u);EXPECT_TRUE(certified->contains(enemy->unitId()));
 chooseTarget(enemy);
}
TEST_F(AIIntegrationFHeroesMeleeSource,currentDistanceNotHypotheticalContactControlsThreat)
{
 contactSetup(14,100,7);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(actor->doubleWide());
 enemy->setPosition(BattleHex(6,7));
 const auto certified=source({enemy,alternative});ASSERT_TRUE(certified);ASSERT_EQ(certified->size(),2u);
 const auto nearDamage=FHeroesThreatFixtureOracle::value(facts(*enemy),facts(*actor));const auto farDamage=FHeroesThreatFixtureOracle::value(facts(*alternative),facts(*actor));
 ASSERT_GT(farDamage.potential,nearDamage.potential);
 int distance=GameConstants::BFIELD_SIZE;for(const auto & from:alternative->getHexes())for(const auto & to:actor->getHexes())distance=std::min(distance,int(BattleHex::getDistance(from,to)));
 ASSERT_GT(distance,alternative->getMovementRange()+1);
 const double currentThreat=farDamage.potential/(1.5*distance/alternative->getMovementRange());ASSERT_LT(currentThreat,nearDamage.potential);
 chooseTarget(enemy);
}
TEST_F(AIIntegrationFHeroesMeleeSource,wideLegalContactsShareCurrentPositionSourceArithmetic)
{
 contactSetup(14,500,4);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(actor->doubleWide());
 const auto certified=source({enemy,alternative});ASSERT_TRUE(certified);
 const auto reach=view->getReachability(actor);const auto positions=view->battleGetAvailableHexes(reach,actor,false);int contacts=0;
 for(const auto * target:{enemy,alternative})
 {
  const auto oracle=FHeroesThreatFixtureOracle::value(facts(*target),facts(*actor));
  EXPECT_EQ(certified->at(target->unitId()).potential,oracle.potential);EXPECT_EQ(certified->at(target->unitId()).retaliation,oracle.retaliation);
  for(const auto & from:positions)for(const auto & aim:view->meleeAttackHexes(actor,target,from)){EXPECT_EQ(view->battleGetUnitByPos(aim),target);++contacts;}
 }
 EXPECT_GT(contacts,2);chooseTarget(alternative);
}
TEST_F(AIIntegrationFHeroesMeleeSource,firstStrikeRemainsNative)
{
 contactSetup();ASSERT_FALSE(HasFatalFailure());auto effect=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::FIRST_STRIKE,BonusSource::OTHER,1,BonusSourceID());effect->subtype=BonusSubtypeID(BonusCustomSubtype::damageTypeMelee);alternative->addNewBonus(effect);
 EXPECT_FALSE(source({enemy,alternative}));
}
TEST_F(AIIntegrationFHeroesMeleeSource,ordinaryArcherIsNotAdmittedToMeleeConsumer)
{
 contactSetup(2);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(source({enemy,alternative}));
}
