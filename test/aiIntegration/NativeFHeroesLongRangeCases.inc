/* H2 source threat has no H3 range penalty; native shots retain it. GPL-2.0-or-later. */
class AIIntegrationFHeroesLongRange : public AIIntegrationFHeroesArchery
{
protected:
 void mixedRangePursuit(bool fallback,int level)
 {
  setup(0,2,1000,30);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,2));
  auto * near=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(8,8),12);ASSERT_NE(near,nullptr);
  if(level)defenderSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  ASSERT_TRUE(view->battleHasDistancePenalty(enemy,enemy->getPosition(),actor->getPosition()));ASSERT_FALSE(view->battleHasDistancePenalty(near,near->getPosition(),actor->getPosition()));
  ASSERT_FALSE(view->battleHasWallPenalty(enemy,enemy->getPosition(),actor->getPosition()));
  const auto rangedBonus=level?percent(level):0;
  const auto farSource=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*enemy,rangedBonus),facts(*actor));
  const auto nearSource=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*near,rangedBonus),facts(*actor));
  const auto routes=view->getReachability(actor);const auto available=view->battleGetAvailableHexes(routes,actor,false);
  const auto goal=[&](const CStack * target){BattleHex chosen;int best=999;for(const auto & hex:target->getAttackableHexes(actor))if(hex.isValid()&&view->isMeleeAttackPossible(actor,target,hex)){const int distance=routes.distances[hex.toInt()];if(distance<best||(distance==best&&(!chosen.isValid()||hex.toInt()<chosen.toInt()))){chosen=hex;best=distance;}}return chosen;};
  const auto farGoal=goal(enemy),nearGoal=goal(near);ASSERT_TRUE(farGoal.isValid());ASSERT_TRUE(nearGoal.isValid());
  const auto farPath=routes.distances[farGoal.toInt()],nearPath=routes.distances[nearGoal.toInt()];ASSERT_LT(farPath,GameConstants::BFIELD_SIZE);ASSERT_LT(nearPath,GameConstants::BFIELD_SIZE);ASSERT_GT(nearPath,actor->getMovementRange());
  const auto step=[&](BattleHex hex){for(int i=0;i<GameConstants::BFIELD_SIZE&&hex.isValid();++i){if(available.contains(hex))return hex;const auto previous=routes.predecessors[hex.toInt()];if(previous==hex)break;hex=previous;}return BattleHex();};
  const auto farStep=step(farGoal),nearStep=step(nearGoal);ASSERT_TRUE(farStep.isValid());ASSERT_TRUE(nearStep.isValid());ASSERT_NE(farStep,nearStep);
  const auto native=[&](const CStack * u){const auto d=view->battleEstimateDamage(BattleAttackInfo(u,actor,0,true));return(double(d.damage.min)+d.damage.max)/2;};
  ASSERT_GT(double(farSource.potential)/farPath,double(nearSource.potential)/nearPath);
  ASSERT_LT(native(enemy)/farPath,native(near)/nearPath);
  if(fallback)near->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy,near});ASSERT_EQ(bool(source),!fallback);
  if(source){EXPECT_EQ(source->at(enemy->unitId()).potential,farSource.potential);EXPECT_EQ(source->at(near->unitId()).potential,nearSource.potential);}
  const auto stance=AIIntegration::FHeroes2TacticsContext::tactics(*view,*actor,{enemy,near});ASSERT_FALSE(stance.defensive);ASSERT_FALSE(stance.cautiousOffensive);
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);EXPECT_EQ(action.target.front().hexValue,fallback?nearStep:farStep);std::cout<<"FHEROES_LONG_RANGE fallback="<<fallback<<" archery="<<level<<" source="<<farSource.potential<<","<<nearSource.potential<<" native="<<native(enemy)<<","<<native(near)<<" paths="<<farPath<<","<<nearPath<<" action="<<action.toString()<<'\n';});
 }
 void nativeDistantShots(int type,int level)
 {
  setup(type,0,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,5));
  if(level)attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  ASSERT_TRUE(view->battleHasDistancePenalty(actor,actor->getPosition(),enemy->getPosition()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(source);
  const auto expected=FHeroesThreatFixtureOracle::rangedValue(skilledFacts(*actor,level?percent(level):0),facts(*enemy));EXPECT_EQ(source->at(actor->unitId()).potential,expected.potential);
  const auto far=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,true));
  BattleAttackInfo close(actor,enemy,0,true);close.attackerPos=BattleHex(8,5);const auto near=view->battleEstimateDamage(close);
  ASSERT_LT(far.damage.max,near.damage.min);EXPECT_GT(expected.potential,far.damage.max);
  const auto enemyID=enemy->unitId();int shots=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & attack){if(attack.shot()){++shots;for(const auto & hit:attack.bsa)if(hit.stackAttacked==enemyID){EXPECT_GE(hit.damageAmount,far.damage.min);EXPECT_LE(hit.damageAmount,far.damage.max);}}};
  chooseAndApply([&](const BattleAction & action){EXPECT_EQ(action.actionType,EActionType::SHOOT);});
  recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(shots,2);
 }
 void completeLongRange(int level)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());setArmy(attackerSideHero,CreatureID(3),30);setArmy(defenderSideHero,CreatureID(6),30);
  if(level)attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,level,ChangeValueMode::ABSOLUTE);
  map()->getTile(defenderSideHero->visitablePos()).terrainType=ETerrainId::SAND;
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);
  for(const auto & u:battle()->stacks)(u->unitSide()==BattleSide::ATTACKER?actor:enemy)=u.get();ASSERT_NE(actor,nullptr);ASSERT_NE(enemy,nullptr);
  auto callback=std::make_shared<DecisionCallback>(actor->unitOwner());callback->onBattleStarted(battle());const auto v=callback->getBattle(battle()->getBattleID());
  ASSERT_TRUE(v->battleHasDistancePenalty(actor,actor->getPosition(),enemy->getPosition()));ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*v,*enemy,{actor}));
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_GT(recorder.shots,0);EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesLongRange,mixedNearFarSourceChoosesFarThreat){mixedRangePursuit(false,0);}
TEST_F(AIIntegrationFHeroesLongRange,mixedNearFarFallbackChoosesNearThreat){mixedRangePursuit(true,0);}
TEST_F(AIIntegrationFHeroesLongRange,expertArcherySourceChoosesFarThreat){mixedRangePursuit(false,3);}
TEST_F(AIIntegrationFHeroesLongRange,expertArcheryFallbackChoosesNearThreat){mixedRangePursuit(true,3);}
TEST_F(AIIntegrationFHeroesLongRange,marksmanNativeShotStillHasDistancePenalty){nativeDistantShots(3,0);}
TEST_F(AIIntegrationFHeroesLongRange,grandElfNativeShotStillHasDistancePenalty){nativeDistantShots(19,0);}
TEST_F(AIIntegrationFHeroesLongRange,expertMarksmanNativeShotStillHasDistancePenalty){nativeDistantShots(3,3);}
TEST_F(AIIntegrationFHeroesLongRange,ordinaryLongRangeFactoryBattleCompletes){completeLongRange(0);}
TEST_F(AIIntegrationFHeroesLongRange,expertLongRangeFactoryBattleCompletes){completeLongRange(3);}
TEST_F(AIIntegrationFHeroesLongRange,rangeBypassEffectStillRejectsWholeComparison)
{
 setup(0,2);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,5));
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_DISTANCE_PENALTY,BonusSource::OTHER,0,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
}
