/* Pinned blocked-archer direct action ranking. GPL-2.0-or-later. */
class AIIntegrationFHeroesBlockedArcher : public AIIntegrationFHeroesBlessCurse
{
protected:
 CStack * alternative = nullptr;
 void contactSetup(bool equal = false)
 {
  setup(2,0,50,60);ASSERT_FALSE(HasFatalFailure());
  actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(9,5));
  alternative=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(7,5),equal?60:61);
  ASSERT_NE(alternative,nullptr);ASSERT_TRUE(view->battleIsUnitBlocked(actor));
  ASSERT_GE(enemy->getMovementRange()+2,actor->getMovementRange());
 }
 static int64_t oracleDifference(const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit & a,
  const AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit & d)
 {
  const auto value=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,true);
  return int64_t(value.potential)-value.retaliation;
 }
 double nativeDifference(const CStack * target)
 {
  BattleAttackInfo attack(actor,target,0,false);attack.attackerPos=actor->getPosition();attack.defenderPos=target->getPosition();
  DamageEstimation retaliation;const auto damage=view->battleEstimateDamage(attack,&retaliation);
  return (double(damage.damage.min)+damage.damage.max-double(retaliation.damage.min)-retaliation.damage.max)/2;
 }
 void differingChoice(bool fallback,bool distantUnsupported)
 {
  contactSetup();ASSERT_FALSE(HasFatalFailure());
  auto attack=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,20,BonusSourceID());
  attack->subtype=BonusSubtypeID(PrimarySkill::ATTACK);enemy->addNewBonus(attack);
  const auto a=facts(*actor);const auto first=oracleDifference(a,facts(*enemy)),second=oracleDifference(a,facts(*alternative));
  ASSERT_GT(first,second);ASSERT_LT(nativeDifference(enemy),nativeDifference(alternative));
  CStack * distant=nullptr;
  if(distantUnsupported){distant=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(15,2),1);ASSERT_NE(distant,nullptr);}
  if(fallback||distantUnsupported)(fallback?alternative:distant)->addNewBonus(
   std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  std::vector<const CStack *> targets{enemy,alternative};if(distant)targets.push_back(distant);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryBlockedArcherDamage(*view,*actor,targets);
  ASSERT_EQ(bool(source),!fallback);
  if(source){ASSERT_EQ(source->size(),2u);EXPECT_EQ(int64_t(source->at(enemy->unitId()).potential)-source->at(enemy->unitId()).retaliation,first);}
  const auto expected=(fallback?alternative:enemy)->unitId();
  const auto estimate=view->battleEstimateDamage(BattleAttackInfo(actor,fallback?alternative:enemy,0,false));
  int hits=0;recorder.onAttackBeforeApply=[&](const BattleAttack & packet){if(packet.stackAttacking==actor->unitId())for(const auto & hit:packet.bsa)if(hit.stackAttacked==expected){++hits;EXPECT_GE(hit.damageAmount,estimate.damage.min);EXPECT_LE(hit.damageAmount,estimate.damage.max);}};
  chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);ASSERT_GE(action.target.size(),2u);const auto * target=view->battleGetUnitByPos(action.target[1].hexValue);ASSERT_NE(target,nullptr);EXPECT_EQ(target->unitId(),expected);});
  recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_GT(hits,0);
  // The attacker already issued the verified decision above and may die before acting again.
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics");ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesBlockedArcher,sourceScoreChangesActualTarget){differingChoice(false,false);}
TEST_F(AIIntegrationFHeroesBlockedArcher,unsupportedAdjacentTargetRestoresWholeNativeRanking){differingChoice(true,false);}
TEST_F(AIIntegrationFHeroesBlockedArcher,unsupportedDistantTargetDoesNotChangeContactRanking){differingChoice(false,true);}
TEST_F(AIIntegrationFHeroesBlockedArcher,sourceTiePreservesEnemyIterationOrder)
{
 contactSetup(true);ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(oracleDifference(facts(*actor),facts(*enemy)),oracleDifference(facts(*actor),facts(*alternative)));
 const auto id=enemy->unitId();
 chooseAndApply([&](const BattleAction & action){ASSERT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);ASSERT_GE(action.target.size(),2u);const auto * target=view->battleGetUnitByPos(action.target[1].hexValue);ASSERT_NE(target,nullptr);EXPECT_EQ(target->unitId(),id);});
}
TEST_F(AIIntegrationFHeroesBlockedArcher,spentRetaliationAndPartialHealthMatchPinnedOracle)
{
 contactSetup();ASSERT_FALSE(HasFatalFailure());int64_t wound=7;enemy->damage(wound);enemy->counterAttacks.use();
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryBlockedArcherDamage(*view,*actor,{enemy,alternative});ASSERT_TRUE(source);
 for(const auto * target:{enemy,alternative}){const auto oracle=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*actor),facts(*target),true);EXPECT_EQ(source->at(target->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(target->unitId()).retaliation,oracle.retaliation);}
 EXPECT_EQ(source->at(enemy->unitId()).retaliation,0u);
}
TEST_F(AIIntegrationFHeroesBlockedArcher,blessAndCurseApplyToDirectMeleeRanking)
{
 contactSetup();ASSERT_FALSE(HasFatalFailure());
 // Permit both fixture-preparation casts without adding defend-turn bonuses.
 attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN,BonusSource::OTHER,1,BonusSourceID()));
 applyMode(actor,1,2);ASSERT_FALSE(HasFatalFailure());
 applyMode(enemy,-1,1);ASSERT_FALSE(HasFatalFailure());
 auto a=facts(*actor);a.damageMode=1;auto d=facts(*enemy);d.damageMode=-1;
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryBlockedArcherDamage(*view,*actor,{enemy,alternative});ASSERT_TRUE(source);
 const auto oracle=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,true);
 EXPECT_EQ(source->at(enemy->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,oracle.retaliation);
}
TEST_F(AIIntegrationFHeroesBlockedArcher,nativeFirstStrikeRejectsSourceComparison)
{
 contactSetup();ASSERT_FALSE(HasFatalFailure());
 auto effect=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::FIRST_STRIKE,BonusSource::OTHER,1,BonusSourceID());effect->subtype=BonusSubtypeID(BonusCustomSubtype::damageTypeMelee);enemy->addNewBonus(effect);
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryBlockedArcherDamage(*view,*actor,{enemy,alternative}));
}
TEST_F(AIIntegrationFHeroesBlockedArcher,noMeleePenaltyAndLethalProjectionMatchPinnedOracle)
{
 setup(74,0,50,1);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(9,5));
 const auto a=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*actor,true);ASSERT_TRUE(a);ASSERT_TRUE(a->noMeleePenalty);
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryBlockedArcherDamage(*view,*actor,{enemy});ASSERT_TRUE(source);
 auto independent=facts(*actor);independent.noMeleePenalty=true;const auto oracle=FHeroesThreatFixtureOracle::archerMeleeValue(independent,facts(*enemy),true);
 EXPECT_EQ(source->at(enemy->unitId()).potential,oracle.potential);EXPECT_EQ(source->at(enemy->unitId()).retaliation,0u);EXPECT_GT(oracle.potential,enemy->getAvailableHealth());
}
