// GPL-2.0-or-later. Included after AIIntegrationNativeBattle's definition.
class FHeroes2RangedPressureBattle : public AIIntegrationNativeBattle
{
protected:
	std::shared_ptr<DecisionCallback> pressureCallback;
	std::shared_ptr<CPlayerBattleCallback> pressureView;
	const CStack * guard = nullptr;
	std::vector<const CStack *> pressureEnemies;

	void preparePressure(bool ownArchers = true)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(0), 100);
		setArmy(defenderSideHero, CreatureID(0), 100);
		if(ownArchers)
		{
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 10));
		}
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 20));
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == CreatureID(0))
				guard = unit.get();
		ASSERT_NE(guard, nullptr);
		pressureCallback = std::make_shared<DecisionCallback>(battle()->sideToPlayer(BattleSide::ATTACKER));
		pressureCallback->onBattleStarted(battle());
		pressureView = pressureCallback->getBattle(battle()->getBattleID());
		pressureEnemies = pressureView->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		attackerSideHero->mana = 0;
		defenderSideHero->mana = 0;
	}

	AIIntegration::Ports::FHeroes2::Tactics stance(const AIIntegration::FHeroes2ObservedSpells & observed = {})
	{
		return AIIntegration::FHeroes2TacticsContext::tactics(*pressureView, *guard, pressureEnemies, observed);
	}
};

TEST_F(FHeroes2RangedPressureBattle, ownDamageChangesStanceOnlyWithShootersAndAffordableSpell)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FALSE(stance().defensive);
	teachBattleSpell(attackerSideHero, SpellID::IMPLOSION, 99);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	const int cost = pressureView->battleGetSpellCost(spell, attackerSideHero);
	ASSERT_GT(cost, 0);
	attackerSideHero->mana = cost - 1;
	EXPECT_FALSE(stance().defensive);
	attackerSideHero->mana = cost;
	const auto mana = attackerSideHero->mana;
	const auto health = guard->getAvailableHealth();
	EXPECT_TRUE(stance().defensive);
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(guard->getAvailableHealth(), health);
	battle()->getSide(BattleSide::ATTACKER).castSpellsCount = 1;
	EXPECT_NE(pressureView->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	EXPECT_TRUE(stance().defensive) << "Strategic damage survives the current turn's cast restriction";
}

TEST_F(FHeroes2RangedPressureBattle, ownMageWithoutShootersDoesNotBecomeDefensive)
{
	preparePressure(false);
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::IMPLOSION, 99);
	EXPECT_FALSE(stance().defensive);
}

TEST_F(FHeroes2RangedPressureBattle, ownNonDamageAndAdventureSpellsDoNotCreatePressure)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::HASTE, 99);
	teachSpell(attackerSideHero, spellByName("core:townPortal"));
	EXPECT_FALSE(stance().defensive);
}

TEST_F(FHeroes2RangedPressureBattle, ownArtifactGrantedSpellDoesNotRequireBookEntry)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::HASTE, 99);
	const SpellID spell(SpellID::IMPLOSION);
	attackerSideHero->removeSpellFromSpellbook(spell);
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL,
		BonusSource::ARTIFACT, 1, BonusSourceID());
	bonus->subtype = BonusSubtypeID(spell);
	attackerSideHero->addNewBonus(bonus);
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	EXPECT_TRUE(stance().defensive);
}

TEST_F(FHeroes2RangedPressureBattle, enemyPressureUsesOnlyObservedSpellsAndPublicUntrainedDamage)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(defenderSideHero, SpellID::IMPLOSION, 99);
	using Magic = AIIntegration::FHeroes2ObservedMagic;
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, {}), 0);
	const AIIntegration::FHeroes2ObservedSpells observed{{SpellID(SpellID::IMPLOSION), false}};
	const double raw = Magic::maximumObservedDamage(*pressureView, observed);
	ASSERT_GT(raw, 0);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	EXPECT_EQ(raw, spell->calculateRawEffectValue(MasteryLevel::NONE, 99, 1));
	setMagicMastery(defenderSideHero, MasteryLevel::EXPERT);
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, observed), raw);
	battle()->getSide(BattleSide::DEFENDER).castSpellsCount = 1;
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, observed), raw);
	defenderSideHero->mana = spell->getCost(MasteryLevel::NONE) - 1;
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, observed), 0);
	defenderSideHero->mana = spell->getCost(MasteryLevel::NONE);
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, observed), raw);
}

TEST_F(FHeroes2RangedPressureBattle, enemyObservedMaximumDoesNotMultiplyAreaDamageOrReadOtherSpells)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(defenderSideHero, SpellID::IMPLOSION, 99);
	using Magic = AIIntegration::FHeroes2ObservedMagic;
	const SpellID area(SpellID::ARMAGEDDON);
	const AIIntegration::FHeroes2ObservedSpells one{{area, false}}, many{{area, true}};
	const double raw = area.toSpell()->calculateRawEffectValue(MasteryLevel::NONE, 99, 1);
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, one), raw);
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, many), raw);
	const AIIntegration::FHeroes2ObservedSpells both{{area, true}, {SpellID(SpellID::IMPLOSION), false},
		{SpellID(SpellID::HASTE), true}, {spellByName("core:townPortal"), false}};
	EXPECT_EQ(Magic::maximumObservedDamage(*pressureView, both), std::max(raw,
		static_cast<double>(SpellID(SpellID::IMPLOSION).toSpell()->calculateRawEffectValue(MasteryLevel::NONE, 99, 1))));
}

TEST_F(FHeroes2RangedPressureBattle, observedEnemyMageOverridesOwnDefensivePressure)
{
	preparePressure();
	ASSERT_FALSE(HasFatalFailure());
	teachBattleSpell(attackerSideHero, SpellID::IMPLOSION, 99);
	teachBattleSpell(defenderSideHero, SpellID::IMPLOSION, 99);
	EXPECT_TRUE(stance().defensive) << "An unseen hostile spellbook must not leak into stance";
	EXPECT_FALSE(stance({{SpellID(SpellID::IMPLOSION), false}}).defensive);
}
