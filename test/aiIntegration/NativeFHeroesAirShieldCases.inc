/* Pinned H2 Shield valuation with native H3 Air Shield effects. GPL-2.0-or-later. */
class AIIntegrationFHeroesAirShield : public AIIntegrationFHeroesBlessCurse
{
protected:
 void applyShield(CStack * target,int level)
 {
  auto * caster=target->unitSide()==BattleSide::ATTACKER?attackerSideHero:defenderSideHero;
  teachBattleSpell(caster,SpellID::AIR_SHIELD,6);caster->setSecSkillLevel(SecondarySkill::AIR_MAGIC,level,ChangeValueMode::ABSOLUTE);
  ASSERT_TRUE(castOn(caster,SpellID::AIR_SHIELD,target));
  const auto cert=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*target,target->isShooter());ASSERT_TRUE(cert);EXPECT_TRUE(cert->shield);
 }
 void shot(int level,bool composition)
 {
  setup(2,1,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(10,5));
  CStack * extra=nullptr;if(level==3)extra=addStack(BattleSide::DEFENDER,CreatureID(1),BattleHex(14,2),100);
  if(composition){attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,3,ChangeValueMode::ABSOLUTE);applyMode(actor,1,2);ASSERT_FALSE(HasFatalFailure());}
  const auto before=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,true));
  applyShield(enemy,level);ASSERT_FALSE(HasFatalFailure());
  EXPECT_FALSE(actor->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION));
  if(extra){const auto c=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*extra);ASSERT_TRUE(c);EXPECT_TRUE(c->shield);}
  auto a=facts(*actor),d=facts(*enemy);d.shield=true;if(composition){a.archeryPercent=50;a.damageMode=1;}
  const auto oracle=FHeroesThreatFixtureOracle::rangedValue(a,d);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(source);EXPECT_EQ(source->at(actor->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(actor->unitId()).retaliation,0u);
  const auto native=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,true));EXPECT_LT(native.damage.max,before.damage.max);
  const auto actorID=actor->unitId();int hits=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & attack){if(attack.stackAttacking==actorID){EXPECT_TRUE(attack.shot());for(const auto & hit:attack.bsa){++hits;EXPECT_GE(hit.damageAmount,native.damage.min);EXPECT_LE(hit.damageAmount,native.damage.max);}}};
  chooseAndApply([&](const BattleAction & action){EXPECT_EQ(action.actionType,EActionType::SHOOT);});recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(hits,1);
 }
 void meleeUnaffected(bool ammo)
 {
  setup(2,1,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(9,5));
  if(!ammo)actor->shots.use(actor->shots.available());
  const auto before=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,false));applyShield(enemy,1);ASSERT_FALSE(HasFatalFailure());
  auto a=facts(*actor),d=facts(*enemy);d.shield=true;const auto oracle=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,ammo);d.shield=false;const auto unshielded=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,ammo);
  EXPECT_EQ(oracle.potential,unshielded.potential);EXPECT_EQ(oracle.retaliation,unshielded.retaliation);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(source);EXPECT_EQ(source->at(actor->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(actor->unitId()).retaliation,oracle.retaliation);
  const auto after=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,false));EXPECT_EQ(after.damage.min,before.damage.min);EXPECT_EQ(after.damage.max,before.damage.max);
  const auto actorID=actor->unitId();int hits=0;recorder.onAttackBeforeApply=[&](const BattleAttack & attack){if(attack.stackAttacking==actorID){EXPECT_FALSE(attack.shot());for(const auto & hit:attack.bsa){++hits;EXPECT_GE(hit.damageAmount,after.damage.min);EXPECT_LE(hit.damageAmount,after.damage.max);}}};
  chooseAndApply([&](const BattleAction & action){EXPECT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);});recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(hits,1);
 }
 void shieldedPursuit(bool fallback)
 {
  setup(0,2,1000,30);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(14,2));
  auto * near=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(8,8),12);ASSERT_NE(near,nullptr);
  applyShield(actor,1);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(view->battleHasDistancePenalty(enemy,enemy->getPosition(),actor->getPosition()));ASSERT_FALSE(view->battleHasDistancePenalty(near,near->getPosition(),actor->getPosition()));
  ASSERT_FALSE(view->battleHasWallPenalty(enemy,enemy->getPosition(),actor->getPosition()));
  auto defenderFacts=facts(*actor);defenderFacts.shield=true;
  const auto farSource=FHeroesThreatFixtureOracle::rangedValue(facts(*enemy),defenderFacts);
  const auto nearSource=FHeroesThreatFixtureOracle::rangedValue(facts(*near),defenderFacts);
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
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK);ASSERT_EQ(action.target.size(),1u);EXPECT_EQ(action.target.front().hexValue,fallback?nearStep:farStep);std::cout<<"FHEROES_SHIELDED_PURSUIT fallback="<<fallback<<" source="<<farSource.potential<<","<<nearSource.potential<<" native="<<native(enemy)<<","<<native(near)<<" paths="<<farPath<<","<<nearPath<<" action="<<action.toString()<<'\n';});
 }
};
TEST_F(AIIntegrationFHeroesAirShield,basicTargetedFactoryShotPreservesNativeReduction){shot(1,false);}
TEST_F(AIIntegrationFHeroesAirShield,advancedTargetedWithArcheryBless){shot(2,true);}
TEST_F(AIIntegrationFHeroesAirShield,expertMassWithArcheryBless){shot(3,true);}
TEST_F(AIIntegrationFHeroesAirShield,blockedFactoryMeleeUnaffected){meleeUnaffected(true);}
TEST_F(AIIntegrationFHeroesAirShield,depletedFactoryMeleeUnaffected){meleeUnaffected(false);}
TEST_F(AIIntegrationFHeroesAirShield,nativeDispelRevokesSourceShield)
{
 setup(2,1,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(2,5));enemy->setPosition(BattleHex(10,5));
 const auto original=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(original);
 applyShield(enemy,1);ASSERT_FALSE(HasFatalFailure());
 const auto shielded=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(shielded);EXPECT_LT(shielded->at(actor->unitId()).potential,original->at(actor->unitId()).potential);
 teachBattleSpell(attackerSideHero,SpellID::DISPEL,6);
 // Native basic Dispel is friendly-only; advanced Water enables enemy targets.
 EXPECT_FALSE(castOn(attackerSideHero,SpellID::DISPEL,enemy));
 attackerSideHero->setSecSkillLevel(SecondarySkill::WATER_MAGIC,2,ChangeValueMode::ABSOLUTE);
 ASSERT_TRUE(castOn(attackerSideHero,SpellID::DISPEL,enemy));
 EXPECT_FALSE(enemy->hasBonusOfType(BonusType::GENERAL_DAMAGE_REDUCTION));
 const auto restored=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(restored);EXPECT_EQ(restored->at(actor->unitId()).potential,original->at(actor->unitId()).potential);
}
TEST_F(AIIntegrationFHeroesAirShield,duplicateAndMeleeShieldStayWholeContextFallback)
{
 setup(2,1);ASSERT_FALSE(HasFatalFailure());applyShield(enemy,1);ASSERT_FALSE(HasFatalFailure());
 const auto bonuses=enemy->getAllBonuses(Selector::type()(BonusType::GENERAL_DAMAGE_REDUCTION));ASSERT_EQ(bonuses->size(),1u);auto extra=std::make_shared<Bonus>(*(*bonuses)[0]);enemy->addNewBonus(extra);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));enemy->removeBonus(extra);enemy->removeBonus((*bonuses)[0]);
 auto melee=std::make_shared<Bonus>(*(*bonuses)[0]);melee->sid=BonusSourceID(SpellID(SpellID::SHIELD));melee->subtype=BonusSubtypeID(BonusCustomSubtype::damageTypeMelee);enemy->addNewBonus(melee);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}
TEST_F(AIIntegrationFHeroesAirShield,expertShieldFullFactoryBattle)
{
 setup(3,6,10,30);ASSERT_FALSE(HasFatalFailure());applyShield(enemy,3);ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
 runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_GT(recorder.shots,0);EXPECT_TRUE(gameState()->currentBattles.empty());
}

TEST_F(AIIntegrationFHeroesAirShield,shieldedSourceFactoryChoosesFar){shieldedPursuit(false);}
TEST_F(AIIntegrationFHeroesAirShield,shieldedFallbackFactoryChoosesNear){shieldedPursuit(true);}
