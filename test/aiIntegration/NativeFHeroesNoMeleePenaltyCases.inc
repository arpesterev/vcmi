/* Canonical innate no-melee-penalty source projection. GPL-2.0-or-later. */
class AIIntegrationFHeroesNoMeleePenalty : public AIIntegrationFHeroesOrdinaryGeometry
{
protected:
 void melee(int type,bool ammo)
 {
  setup(type,1,20,1000);ASSERT_FALSE(HasFatalFailure());actor->setPosition(BattleHex(8,5));enemy->setPosition(BattleHex(9,5));
  // A speed-5 Halberdier prevents the source speed+2 escape shortcut.
  // Faster shooters correctly kite a speed-4 Pikeman instead of attacking.
  ASSERT_GE(enemy->getMovementRange()+2,actor->getMovementRange());
  if(!ammo)actor->shots.use(actor->shots.available());
  ASSERT_EQ(actor->shots.canUse(1),ammo);ASSERT_TRUE(view->battleIsUnitBlocked(actor));ASSERT_FALSE(view->battleCanShoot(actor));
  const auto certificate=AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*actor,true);ASSERT_TRUE(certificate);ASSERT_TRUE(certificate->noMeleePenalty);
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor});ASSERT_TRUE(source);
  auto a=facts(*actor);a.noMeleePenalty=true;const auto d=facts(*enemy);
  const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,ammo);
  EXPECT_EQ(source->at(actor->unitId()).potential,expected.potential);EXPECT_EQ(source->at(actor->unitId()).retaliation,expected.retaliation);
  const auto projected=AIIntegration::Ports::FHeroes2::ordinaryThreatDamage(a,d,ammo);ASSERT_TRUE(projected);EXPECT_EQ(projected->potential,expected.potential);
  if(ammo){a.noMeleePenalty=false;const auto penalized=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,true);EXPECT_GT(expected.potential,penalized.potential);}
  // Unchanged native H3 attack remains full-strength as well. Inspect real
  // server damage, not only the source estimate; lethal retaliation is legal.
  const auto native=view->battleEstimateDamage(BattleAttackInfo(actor,enemy,0,false));const auto actorID=actor->unitId(),enemyID=enemy->unitId();int hits=0;
  recorder.onAttackBeforeApply=[&](const BattleAttack & attack){if(attack.stackAttacking==actorID){EXPECT_FALSE(attack.shot());for(const auto & hit:attack.bsa)if(hit.stackAttacked==enemyID){++hits;EXPECT_GE(hit.damageAmount,native.damage.min);EXPECT_LE(hit.damageAmount,native.damage.max);}}};
  chooseAndApply([&](const BattleAction & action){EXPECT_EQ(action.actionType,EActionType::WALK_AND_ATTACK);});recorder.onAttackBeforeApply={};ASSERT_FALSE(HasFatalFailure());EXPECT_EQ(hits,1);
 }
 void complete(int type)
 {
  setup(type,6,10,30);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
  runToCompletion(std::nullopt,false,"FHeroes2Tactics","FHeroes2Tactics",true);ASSERT_FALSE(HasFatalFailure());EXPECT_GT(recorder.shots,0);EXPECT_GT(recorder.meleeAttacks,0);EXPECT_TRUE(gameState()->currentBattles.empty());
 }
};
TEST_F(AIIntegrationFHeroesNoMeleePenalty,beholderBlockedFactoryMelee){melee(74,true);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,evilEyeBlockedFactoryMelee){melee(75,true);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,zealotBlockedFactoryMelee){melee(9,true);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,zealotDepletedFactoryMelee){melee(9,false);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,beholderFactoryBattleCompletes){complete(74);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,zealotFactoryBattleCompletes){complete(9);}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,dynamicAndDuplicateAbilityRejectWholeComparison)
{
 setup(9);ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
 const auto base=actor->getAllBonuses(Selector::type()(BonusType::NO_MELEE_PENALTY));ASSERT_EQ(base->size(),1u);
 actor->addNewBonus(std::make_shared<Bonus>(*(*base)[0]));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
 enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_MELEE_PENALTY,BonusSource::OTHER,0,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*enemy));
}
TEST_F(AIIntegrationFHeroesNoMeleePenalty,unrelatedMageSpecialsRemainUnsupported)
{
 setup(34);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*enemy,{actor}));
}
