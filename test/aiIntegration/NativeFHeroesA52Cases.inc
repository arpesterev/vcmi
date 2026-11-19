/* FHeroes2 cautious advance, GPL-2.0-or-later.
 * ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * ai_battle.cpp findOptimalPositionForSubsequentAttack.
 */
class AIIntegrationFHeroesCautiousSource : public AIIntegrationFHeroesOrdinaryGeometry
{
protected:
 void enclosedThreat(bool fallback)
 {
  setup(0,0,40,1);ASSERT_FALSE(HasFatalFailure());
  actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,5));
  auto * guard=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(8,3),35);ASSERT_NE(guard,nullptr);
  auto barrier=std::make_shared<SpellCreatedObstacle>();barrier->uniqueID=1001;barrier->ID=SpellID::FORCE_FIELD;
  barrier->casterSide=BattleSide::DEFENDER;barrier->pos=guard->getPosition();barrier->turnsRemaining=2;
  barrier->customSize=guard->getPosition().getNeighbouringTiles();battle()->obstacles.push_back(barrier);
  if(fallback)guard->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  ASSERT_EQ(bool(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy,guard})),!fallback);
  const auto blocked=view->getReachability(guard);
  const auto guardNeighbours=guard->getPosition().getNeighbouringTiles();
  for(const auto & hex:guardNeighbours)ASSERT_FALSE(blocked.isReachable(hex));
  const auto origin=actor->getPosition();const auto hp=actor->getAvailableHealth();const auto guardHp=guard->getAvailableHealth();
  const int speed=actor->getMovementRange();BattleHex chosen;
  chooseAndApply([&](const BattleAction & action){
   ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);chosen=action.target.front().hexValue;
   EXPECT_EQ(actor->getPosition(),origin);EXPECT_EQ(actor->getAvailableHealth(),hp);EXPECT_EQ(guard->getAvailableHealth(),guardHp);
   if(fallback)EXPECT_LT(BattleHex::getDistance(origin,chosen),speed);
   else {EXPECT_EQ(BattleHex::getDistance(origin,chosen),speed);EXPECT_LE(BattleHex::getDistance(chosen,guard->getPosition()),guard->getMovementRange()+1);}
   std::cout<<"FHEROES_CAUTION fallback="<<fallback<<" origin="<<origin.toInt()<<" chosen="<<chosen.toInt()<<" speed="<<speed<<'\n';
  });
  ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(actor->getPosition(),chosen);
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","HoMM3OriginalTactics");ASSERT_FALSE(HasFatalFailure());
  EXPECT_TRUE(gameState()->currentBattles.empty());EXPECT_GT(recorder.damage,0);
 }
 void weightedRisk(int nearCount,bool fallback)
 {
  setup(0,2,100,1);ASSERT_FALSE(HasFatalFailure());
  actor->setPosition(BattleHex(2,0));enemy->setPosition(BattleHex(14,0));
  actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,-2,BonusSourceID()));
  auto * near=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(1,4),nearCount);
  auto * far=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(9,0),20);ASSERT_NE(near,nullptr);ASSERT_NE(far,nullptr);
  near->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,-1,BonusSourceID()));
  auto defense=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,150,BonusSourceID());
  defense->subtype=BonusSubtypeID(PrimarySkill::DEFENSE);near->addNewBonus(defense);
  const BattleHex first(3,0),second(4,0);ASSERT_EQ(actor->getMovementRange(),2);ASSERT_EQ(near->getMovementRange(),3);
  const auto contact=[&](const CStack * unit,const BattleHex & step){const auto route=view->getReachability(unit);for(const auto & hex:step.getNeighbouringTiles())if(route.distances[hex.toInt()]<ReachabilityInfo::INFINITE_DIST && route.distances[hex.toInt()]<=unit->getMovementRange())return true;return false;};
  ASSERT_TRUE(contact(near,first));ASSERT_FALSE(contact(near,second));ASSERT_FALSE(contact(far,first));ASSERT_TRUE(contact(far,second));
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel strength(*view);ASSERT_GT(strength.value(*near),strength.value(*far));
  const auto a=FHeroesThreatFixtureOracle::value(facts(*near),facts(*actor));const auto b=FHeroesThreatFixtureOracle::value(facts(*far),facts(*actor));
  const double sourceNear=a.potential;
  const double sourceFar=b.potential/(1.5*BattleHex::getDistance(far->getPosition(),actor->getPosition())/far->getMovementRange());
  ASSERT_LE(BattleHex::getDistance(near->getPosition(),actor->getPosition()),near->getMovementRange()+1);
  if(nearCount==5)ASSERT_LT(sourceNear,sourceFar);else {ASSERT_GT(sourceNear,sourceFar);ASSERT_LT(sourceNear,b.potential);}
  if(fallback)near->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  ASSERT_EQ(bool(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy,near,far})),!fallback);
  const auto origin=actor->getPosition();const auto health=actor->getAvailableHealth();
  chooseAndApply([&](const BattleAction & action){
   ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);
   EXPECT_EQ(actor->getPosition(),origin);EXPECT_EQ(actor->getAvailableHealth(),health);
   EXPECT_EQ(action.target.front().hexValue,(!fallback && nearCount==5)?first:second);
   std::cout<<"FHEROES_RISK source="<<sourceNear<<","<<sourceFar<<" raw="<<strength.value(*near)<<","<<strength.value(*far)<<" action="<<action.toString()<<'\n';
  });
  ASSERT_FALSE(HasFatalFailure());runToCompletion(std::nullopt,false,"FHeroes2Tactics","HoMM3OriginalTactics");
  ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(gameState()->currentBattles.empty());EXPECT_GT(recorder.damage,0);
 }

};
TEST_F(AIIntegrationFHeroesCautiousSource,blockedEnemyDoesNotCreateImaginaryPathDanger){enclosedThreat(false);}
TEST_F(AIIntegrationFHeroesCautiousSource,unsupportedEnemyRestoresWholeNativePathRisk){enclosedThreat(true);}

TEST_F(AIIntegrationFHeroesCautiousSource,incomingThreatNotRawStrengthSelectsEarlierStep){weightedRisk(5,false);}
TEST_F(AIIntegrationFHeroesCautiousSource,currentDistanceNotHypotheticalContactSelectsLaterStep){weightedRisk(10,false);}
TEST_F(AIIntegrationFHeroesCautiousSource,unsupportedContextRetainsRawStrengthPathRanking){weightedRisk(5,true);}
