/* Public native Bless/Curse records mapped to pinned H2 damage modes. GPL-2.0-or-later. */
class AIIntegrationFHeroesBlessCurse : public AIIntegrationFHeroesOrdinaryGeometry
{
protected:
 void applyMode(CStack * unit,int mode,int level)
 {
  const auto spell=SpellID(mode>0?SpellID::BLESS:SpellID::CURSE);
  auto * caster=(mode>0)==(unit->unitSide()==BattleSide::ATTACKER)?attackerSideHero:defenderSideHero;
  teachBattleSpell(caster,spell,6);
  caster->setSecSkillLevel(mode>0?SecondarySkill::WATER_MAGIC:SecondarySkill::FIRE_MAGIC,level,ChangeValueMode::ABSOLUTE);
  ASSERT_TRUE(castOn(caster,spell,unit));
  const auto cert=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*unit,unit->isShooter());ASSERT_TRUE(cert);EXPECT_EQ(cert->damageMode,mode);
 }
 void attackWithMode(int mode,int level,bool blocked)
 {
  setup(2,1,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(blocked?3:14,5));
  applyMode(actor,mode,level);ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(view->battleIsUnitBlocked(actor),blocked);
  auto a=facts(*actor);a.damageMode=mode;const auto d=facts(*enemy);
  const auto expected=blocked?FHeroesThreatFixtureOracle::archerMeleeValue(a,d,true):FHeroesThreatFixtureOracle::rangedValue(a,d);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(source);
  EXPECT_EQ(source->at(actor->unitId()).potential,expected.potential);EXPECT_EQ(source->at(actor->unitId()).retaliation,expected.retaliation);
  const auto native=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,!blocked));EXPECT_EQ(native.damage.min,native.damage.max);
  const auto actorID=actor->unitId(),enemyID=enemy->unitId();int hits=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & attack){if(attack.stackAttacking==actorID)for(const auto & hit:attack.bsa)if(hit.stackAttacked==enemyID){++hits;EXPECT_EQ(hit.damageAmount,native.damage.min);EXPECT_EQ(attack.shot(),!blocked);}};
  chooseAndApply([&](const BattleAction & action){EXPECT_EQ(action.actionType,blocked?EActionType::WALK_AND_ATTACK:EActionType::SHOOT);});recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(hits,1);
 }
 void retaliationAndCounter(int firstMode)
 {
  setup(0,0,100,100);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(9,5));
  for(int mode:{firstMode,-firstMode})
  {
   applyMode(actor,mode,1);ASSERT_FALSE(HasFatalFailure());
   auto a=facts(*enemy),d=facts(*actor);d.damageMode=mode;
   const auto oracle=FHeroesThreatFixtureOracle::value(a,d);
   const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*actor,{enemy});ASSERT_TRUE(source);
   EXPECT_EQ(source->at(enemy->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,oracle.retaliation);
   EXPECT_FALSE(actor->hasBonusOfType(mode>0?BonusType::ALWAYS_MINIMUM_DAMAGE:BonusType::ALWAYS_MAXIMUM_DAMAGE));
  }
 }
 void blessedPursuit(bool fallback)
 {
  setup(0,2,1000,30);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,2));
  auto * near=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(8,8),12);ASSERT_NE(near,nullptr);
  applyMode(enemy,1,1);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(view->battleHasDistancePenalty(enemy,enemy->getPosition(),actor->getPosition()));ASSERT_FALSE(view->battleHasDistancePenalty(near,near->getPosition(),actor->getPosition()));
  ASSERT_FALSE(view->battleHasWallPenalty(enemy,enemy->getPosition(),actor->getPosition()));
  auto farFacts=facts(*enemy);farFacts.damageMode=1;
  const auto farSource=FHeroesThreatFixtureOracle::rangedValue(farFacts,facts(*actor));
  const auto nearSource=FHeroesThreatFixtureOracle::rangedValue(facts(*near),facts(*actor));
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
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);EXPECT_EQ(action.target.front().hexValue,fallback?nearStep:farStep);std::cout<<"FHEROES_BLESSED_PURSUIT fallback="<<fallback<<" source="<<farSource.potential<<","<<nearSource.potential<<" native="<<native(enemy)<<","<<native(near)<<" paths="<<farPath<<","<<nearPath<<" action="<<action.toString()<<'\n';});
 }
 void complete(int mode)
 {
  setup(3,6,10,30);ASSERT_FALSE(HasFatalFailure());applyMode(actor,mode,3);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_GT(recorder.shots,0);EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesBlessCurse,basicBlessRangedFactoryDamage){attackWithMode(1,1,false);}
TEST_F(AIIntegrationFHeroesBlessCurse,advancedBlessRangedFactoryDamage){attackWithMode(1,2,false);}
TEST_F(AIIntegrationFHeroesBlessCurse,expertBlessBlockedFactoryDamage){attackWithMode(1,3,true);}
TEST_F(AIIntegrationFHeroesBlessCurse,basicCurseRangedFactoryDamage){attackWithMode(-1,1,false);}
TEST_F(AIIntegrationFHeroesBlessCurse,advancedCurseRangedFactoryDamage){attackWithMode(-1,2,false);}
TEST_F(AIIntegrationFHeroesBlessCurse,expertCurseBlockedFactoryDamage){attackWithMode(-1,3,true);}
TEST_F(AIIntegrationFHeroesBlessCurse,blessThenCurseReplacesRetaliationMode){retaliationAndCounter(1);}
TEST_F(AIIntegrationFHeroesBlessCurse,curseThenBlessReplacesRetaliationMode){retaliationAndCounter(-1);}
TEST_F(AIIntegrationFHeroesBlessCurse,expertBlessFullFactoryBattle){complete(1);}
TEST_F(AIIntegrationFHeroesBlessCurse,expertCurseFullFactoryBattle){complete(-1);}
TEST_F(AIIntegrationFHeroesBlessCurse,duplicateAndExpiredRecordsRejectWholeComparison)
{
 setup(0);ASSERT_FALSE(HasFatalFailure());applyMode(actor,1,1);ASSERT_FALSE(HasFatalFailure());
 const auto bs=actor->getAllBonuses(Selector::type()(BonusType::ALWAYS_MAXIMUM_DAMAGE));ASSERT_EQ(bs->size(),1u);
 auto extra=std::make_shared<Bonus>(*(*bs)[0]);actor->addNewBonus(extra);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));actor->removeBonus(extra);
 (*bs)[0]->turnsRemain=0;actor->nodeHasChanged();
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}

TEST_F(AIIntegrationFHeroesBlessCurse,blessedThreatSourceFactoryChoosesFar){blessedPursuit(false);}
TEST_F(AIIntegrationFHeroesBlessCurse,blessedThreatFallbackFactoryChoosesNear){blessedPursuit(true);}

TEST_F(AIIntegrationFHeroesBlessCurse,canonicalMasteryAloneKeepsOrdinaryDamageAndRejectsDuplicate)
{
 setup(0);ASSERT_FALSE(HasFatalFailure());
 const auto before=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(before);
 for(const auto school:{SecondarySkill::FIRE_MAGIC,SecondarySkill::AIR_MAGIC,SecondarySkill::WATER_MAGIC,SecondarySkill::EARTH_MAGIC})
  for(int level=1;level<=3;++level)
  {
   attackerSideHero->setSecSkillLevel(school,level,ChangeValueMode::ABSOLUTE);
   const auto after=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(after);
   EXPECT_EQ(after->at(actor->unitId()).potential,before->at(actor->unitId()).potential);
   EXPECT_EQ(after->at(actor->unitId()).retaliation,before->at(actor->unitId()).retaliation);
  }
 const auto skills=actor->getAllBonuses(Selector::type()(BonusType::MAGIC_SCHOOL_SKILL));ASSERT_EQ(skills->size(),4u);
 actor->addNewBonus(std::make_shared<Bonus>(*(*skills)[0]));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}
