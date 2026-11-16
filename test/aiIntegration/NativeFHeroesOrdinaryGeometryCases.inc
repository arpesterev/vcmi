/* Source DAMAGE admission with native H3 footprint/reachability; GPL-2.0-or-later. */
#include "../../AI/Integration/Ports/FHeroes2NativeThreat.h"
#include "fheroesThreat/PinnedThreatFixtureOracle.h"

class AIIntegrationFHeroesOrdinaryGeometry : public AIIntegrationNativeBattle
{
protected:
 CStack * actor=nullptr;
 CStack * enemy=nullptr;
 std::shared_ptr<DecisionCallback> cb;
 std::shared_ptr<CPlayerBattleCallback> view;
 void setup(int type,int enemyType=0,int ownCount=1000,int theirCount=10)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  setArmy(attackerSideHero,CreatureID(type),ownCount);setArmy(defenderSideHero,CreatureID(enemyType),theirCount);
  map()->getTile(defenderSideHero->visitablePos()).terrainType=ETerrainId::SAND;
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  for(const auto & u:battle()->stacks)(u->unitSide()==BattleSide::ATTACKER?actor:enemy)=u.get();
  ASSERT_NE(actor,nullptr);ASSERT_NE(enemy,nullptr);
  cb=std::make_shared<DecisionCallback>(actor->unitOwner());cb->onBattleStarted(battle());view=cb->getBattle(battle()->getBattleID());
 }
 static AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit facts(const CStack & u)
 {
  const auto * t=u.unitType();return {uint32_t(u.getCount()),uint32_t(u.getAvailableHealth()),uint32_t(t->getBaseHitPoints()),uint32_t(t->getBaseDamageMin()),uint32_t(t->getBaseDamageMax()),u.getAttack(false),u.getDefense(false),u.ableToRetaliate()};
 }
 void chooseAndApply(const std::function<void(const BattleAction &)> & check)
 {
  // Native startBattle already dispatched setup and entered combat.
  // Geometry is prepared before the first controller activation; do not
  // replay BATTLE_SETUP or tactic completion a second time.
  ASSERT_EQ(battle()->tacticDistance,0);battle()->activeStack=actor->unitId();
  auto policy=AIFactory::createBattleAI("FHeroes2Tactics");ASSERT_NE(policy,nullptr);ASSERT_EQ(policy->dllName,"FHeroes2Tactics");policy->playerID=actor->unitOwner();
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(actor->unitOwner()),cb);
  AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy->initBattleInterface(environment,cb,prefs);
  policy->battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(cb->action);check(*cb->action);ASSERT_FALSE(HasFatalFailure());
  const auto id=battle()->getBattleID();const auto player=actor->unitOwner();
  recorder.onResultBeforeApply=[&](const BattleResult & result){policy->battleEnd(id,&result,result.questionID);};
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,player,*cb->action));
  recorder.onResultBeforeApply={};
  if(!recorder.battleResults.empty())
  {
   const auto question=recorder.battleResults.back().questionID;
   if(question!=QuestionID::NONE)ASSERT_TRUE(gameHandler->answerQuestion(question,0,player));
   EXPECT_EQ(recorder.acceptedResults,1);EXPECT_TRUE(gameState()->currentBattles.empty());
  }
 }
 void pursuit(int type,bool fallback)
 {
  setup(0,type);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(93));enemy->setPosition(BattleHex(36));
  auto * alternative=addStack(BattleSide::DEFENDER,CreatureID(type),BattleHex(138),23);ASSERT_NE(alternative,nullptr);
  auto attack=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,20,BonusSourceID());attack->subtype=BonusSubtypeID(PrimarySkill::ATTACK);enemy->addNewBonus(attack);
  const auto a=FHeroesThreatFixtureOracle::value(facts(*enemy),facts(*actor));const auto b=FHeroesThreatFixtureOracle::value(facts(*alternative),facts(*actor));ASSERT_GT(a.potential,b.potential);
  const auto native=[&](const CStack *u){const auto d=view->battleEstimateDamage(BattleAttackInfo(u,actor,8,false));return(double(d.damage.min)+d.damage.max)/2;};ASSERT_LT(native(enemy),native(alternative));
  const auto routes=view->getReachability(actor);
  const auto shortest=[&](const CStack * u){int best=999;for(const auto & hex:u->getAttackableHexes(actor))if(hex.isValid()&&view->isMeleeAttackPossible(actor,u,hex))best=std::min(best,int(routes.distances[hex.toInt()]));return best;};
  ASSERT_LT(shortest(enemy),GameConstants::BFIELD_SIZE);ASSERT_EQ(shortest(enemy),shortest(alternative));ASSERT_GT(shortest(enemy),actor->getMovementRange());
  if(fallback) alternative->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy,alternative});ASSERT_EQ(bool(source),!fallback);
  if(source){EXPECT_EQ(source->at(enemy->unitId()).potential,a.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,a.retaliation);}
  chooseAndApply([&](const BattleAction & action){
   ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);
   const auto dest=action.target.front().hexValue;
   EXPECT_LT(BattleHex::getDistance(dest,(fallback?alternative:enemy)->getPosition()),BattleHex::getDistance(dest,(fallback?enemy:alternative)->getPosition()));
   std::cout<<"FHEROES_GEOMETRY type="<<type<<" fallback="<<fallback<<" source="<<a.potential<<","<<b.potential<<" native="<<native(enemy)<<","<<native(alternative)<<" action="<<action.toString()<<'\n';
  });
 }
 void corridor(int type,bool shouldPass)
 {
  setup(type,0,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(8,0));
  // One-cell vertical passage. A horizontal two-cell unit cannot fit its
  // tail in the next row; the narrow control traverses the same passage.
  for(int y=0;y<GameConstants::BFIELD_HEIGHT;++y)for(int x=1;x<GameConstants::BFIELD_WIDTH-1;++x)
   if(x!=8 && !(x==7&&y==5))addForceField(BattleHex(x,y));
  const auto route=view->getReachability(actor);
  EXPECT_EQ(route.isReachable(BattleHex(8,2)),shouldPass);
  ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
  chooseAndApply([&](const BattleAction & action){if(shouldPass)EXPECT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);else EXPECT_EQ(action.actionType,EActionType::NO_ACTION);});
  ASSERT_FALSE(HasFatalFailure());
  if(shouldPass){ASSERT_EQ(recorder.battleResults.size(),1u);EXPECT_EQ(recorder.battleResults.back().winner,BattleSide::DEFENDER);}
 }
 void wall(int type,bool shouldFly)
 {
  setup(type,0,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(6,5));enemy->setPosition(BattleHex(10,5));
  for(int y=0;y<GameConstants::BFIELD_HEIGHT;++y)addForceField(BattleHex(8,y));
  EXPECT_EQ(view->getReachability(actor).isReachable(BattleHex(9,5)),shouldFly);
  ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));
  const auto actorID=actor->unitId();const auto enemyID=enemy->unitId();
  BattleHex landed;int64_t inflicted=0;
  recorder.onMovementBeforeApply=[&](const BattleStackMoved & move){if(move.stack==actorID)for(const auto & hex:move.tilesToMove)landed=hex;};
  recorder.onAttackBeforeApply=[&](const BattleAttack & attack){for(const auto & damage:attack.bsa)if(damage.stackAttacked==enemyID)inflicted+=damage.damageAmount;};
  chooseAndApply([&](const BattleAction & action){if(shouldFly)EXPECT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);else EXPECT_EQ(action.actionType,EActionType::NO_ACTION);});
  recorder.onMovementBeforeApply={};recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());
  if(shouldFly){EXPECT_GT(inflicted,0);ASSERT_TRUE(landed.isValid());EXPECT_GT(landed.getX(),8);ASSERT_EQ(recorder.battleResults.size(),1u);EXPECT_EQ(recorder.battleResults.back().winner,BattleSide::DEFENDER);}
 }
 void complete(int type,int count)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());setArmy(attackerSideHero,CreatureID(type),count);setArmy(defenderSideHero,CreatureID(0),90);
  map()->getTile(defenderSideHero->visitablePos()).terrainType=ETerrainId::SAND;
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);ASSERT_EQ(gameState()->currentBattles.size(),1u);
  auto callback=std::make_shared<DecisionCallback>(attackerSideHero->getOwner());callback->onBattleStarted(battle());auto battleView=callback->getBattle(battle()->getBattleID());
  const CStack * active=nullptr;std::vector<const CStack*> threats;
  for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER)active=u.get();else threats.push_back(u.get());
  ASSERT_NE(active,nullptr);ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*battleView,*active,threats));
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,wideSourcePursuit){pursuit(14,false);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,wideUnsupportedCompetitorUsesNativePursuit){pursuit(14,true);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,flyingSourcePursuit){pursuit(118,false);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,flyingUnsupportedCompetitorUsesNativePursuit){pursuit(118,true);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,wideCannotFitNarrowPassage){corridor(14,false);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,narrowControlFitsPassage){corridor(0,true);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,ordinaryFlyerCrossesNativeWall){wall(118,true);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,ordinaryGroundCannotCrossNativeWall){wall(14,false);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,wideFactoryBattleCompletes){complete(14,60);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,flyingFactoryBattleCompletes){complete(118,120);}
TEST_F(AIIntegrationFHeroesOrdinaryGeometry,dynamicFlightAndDuplicateInnateFlightRejectWholeContext)
{
 setup(118);ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
 auto extra=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::FLYING,BonusSource::OTHER,0,BonusSourceID());actor->addNewBonus(extra);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));actor->removeBonus(extra);
 enemy->addNewBonus(extra);EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy}));enemy->removeBonus(extra);
 const auto innate=actor->getAllBonuses(Selector::type()(BonusType::FLYING));ASSERT_EQ(innate->size(),1u);
 auto duplicate=std::make_shared<Bonus>(*(*innate)[0]);actor->addNewBonus(duplicate);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}
