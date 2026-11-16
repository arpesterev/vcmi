// GPL-2.0-or-later. Native reflection eligibility is not simulated by castEval.
class FHeroes2NativeMirrorBattle : public AIIntegrationNativeBattle
{
protected:
 enum class Case { GUARANTEED, FAREWELL, PARTIAL, ZERO, AREA, MASS, POSITIVE };
 void exerciseMirror(Case scenario)
 {
  const CreatureID enemyType(2);
  prepareBattle(CreatureID(2), 50, enemyType, 50);
  ASSERT_FALSE(HasFatalFailure());
  const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
  ASSERT_NE(active, nullptr);
  ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
  CStack * enemy = nullptr;
  for(const auto & unit : battle()->stacks)
   if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == enemyType) enemy = unit.get();
  ASSERT_NE(enemy, nullptr);
  for(const auto & spell : LIBRARY->spellh->objects)
   attackerSideHero->removeSpellFromSpellbook(spell->id);
  const SpellID spellID(scenario == Case::AREA ? SpellID::FIREBALL
   : scenario == Case::MASS ? SpellID::CURSE
   : scenario == Case::POSITIVE ? SpellID::BLESS : SpellID::MAGIC_ARROW);
  teachBattleSpell(attackerSideHero, spellID, 12);
  setMagicMastery(attackerSideHero, scenario == Case::MASS ? MasteryLevel::EXPERT : MasteryLevel::BASIC);
  auto * mirrored = scenario == Case::POSITIVE ? const_cast<CStack *>(active) : enemy;
  const int chance = scenario == Case::PARTIAL ? 1 : scenario == Case::ZERO ? 0 : 100;
  auto mirror = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_MIRROR,
   BonusSource::OTHER, chance, BonusSourceID());
  mirrored->addNewBonus(mirror);
  const auto battleID = battle()->getBattleID();
  const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
  auto callback = std::make_shared<DecisionCallback>(player);
  callback->onBattleStarted(battle());
  const auto view = callback->getBattle(battleID);
  auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
  const auto * spell = spellID.toSpell();
  spells::BattleCast legality(view.get(), attackerSideHero, spells::Mode::HERO, spell);
  const auto mechanics = spell->battleMechanics(&legality);
  const spells::Target target{battle::Destination(mirrored)};
  ASSERT_TRUE(mechanics->canBeCastAt(target));
  if(scenario == Case::AREA)
   ASSERT_NE(spell->getLevelInfo(mechanics->getRangeLevel()).range, std::vector<int>{0});
  else if(scenario == Case::MASS)
   ASSERT_TRUE(mechanics->isMassive());
  else
  {
   ASSERT_FALSE(mechanics->isMassive());
   ASSERT_EQ(spell->getLevelInfo(mechanics->getRangeLevel()).range, std::vector<int>{0});
  }
  EXPECT_EQ(mechanics->isNegativeSpell(), scenario != Case::POSITIVE);
  const auto ownHealth = active->getAvailableHealth();
  const auto enemyHealth = enemy->getAvailableHealth();
  const auto mana = attackerSideHero->mana;
  const auto spellPackets = recorder.spellPackets;
  const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(view, environment.get(), active,
   scenario == Case::FAREWELL);
  EXPECT_EQ(active->getAvailableHealth(), ownHealth);
  EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
  EXPECT_EQ(attackerSideHero->mana, mana);
  EXPECT_EQ(recorder.spellPackets, spellPackets);
  EXPECT_EQ(mirrored->valOfBonuses(BonusType::MAGIC_MIRROR), chance);
  const bool unavailable = scenario == Case::GUARANTEED || scenario == Case::FAREWELL || scenario == Case::PARTIAL;
  if(unavailable)
  {
   EXPECT_FALSE(selected) << "Unmodeled reflected outcomes must not be valued as enemy damage";
   if(scenario == Case::GUARANTEED)
   {
    auto preview = std::make_shared<HypotheticBattle>(environment.get(), view);
    spells::BattleCast evaluated(preview.get(), attackerSideHero, spells::Mode::HERO, spell);
    evaluated.castEval(preview->getServerCallback(), target);
    EXPECT_EQ(preview->battleGetUnitByID(active->unitId())->getAvailableHealth(), ownHealth);
    EXPECT_LT(preview->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), enemyHealth);
    const auto reference = BattleAction::makeHeroSpellcast(BattleSide::ATTACKER, target, spellID);
    ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, reference));
    EXPECT_LT(active->getAvailableHealth(), ownHealth);
    EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
    EXPECT_LT(attackerSideHero->mana, mana);
    EXPECT_GT(recorder.spellPackets, spellPackets);
   }
  }
  else
  {
   ASSERT_TRUE(selected) << "Nonreflectable or zero-chance spell must remain selectable";
   EXPECT_EQ(selected->spell, spellID);
   ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *selected));
   EXPECT_LT(attackerSideHero->mana, mana);
   EXPECT_GT(recorder.spellPackets, spellPackets);
   EXPECT_EQ(active->getAvailableHealth(), ownHealth);
   if(scenario == Case::POSITIVE)
    EXPECT_TRUE(active->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
   else if(scenario == Case::MASS)
    EXPECT_TRUE(enemy->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE));
   else
    EXPECT_LT(enemy->getAvailableHealth(), enemyHealth);
  }
  runToCompletion(std::nullopt, false, "FHeroes2Tactics");
 }
};

TEST_F(FHeroes2NativeMirrorBattle, guaranteedMirrorRejectsWrongRecipientPreview) { exerciseMirror(Case::GUARANTEED); }
TEST_F(FHeroes2NativeMirrorBattle, farewellDoesNotValueReflectedKills) { exerciseMirror(Case::FAREWELL); }
TEST_F(FHeroes2NativeMirrorBattle, partialMirrorIsConservativelyUnavailable) { exerciseMirror(Case::PARTIAL); }
TEST_F(FHeroes2NativeMirrorBattle, zeroMirrorChanceKeepsDirectDamage) { exerciseMirror(Case::ZERO); }
TEST_F(FHeroes2NativeMirrorBattle, areaDamageStillHitsMirroredEnemy) { exerciseMirror(Case::AREA); }
TEST_F(FHeroes2NativeMirrorBattle, massNegativeSpellStillAffectsMirroredEnemy) { exerciseMirror(Case::MASS); }
TEST_F(FHeroes2NativeMirrorBattle, positiveSpellStillBenefitsMirroredAlly) { exerciseMirror(Case::POSITIVE); }
