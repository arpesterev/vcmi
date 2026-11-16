// Native Ray: singleton witness and verified multi-enemy source census.
class AIIntegrationNativeRayBattle : public AIIntegrationNativeBattle
{
protected:
	void exerciseRay(int mastery, int scenario = 0)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(2), scenario == 6 ? 1 : 80);
		setArmy(defenderSideHero, CreatureID(6), 15);
		if(scenario == 5)
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 10));
		ASSERT_FALSE(HasFatalFailure());
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		CStack * actor = nullptr, * enemy = nullptr;
		for(const auto & unit : battle()->stacks)
		{
			if(unit->unitSide() == BattleSide::ATTACKER) actor = unit.get();
			else if(unit->unitSlot() == SlotID(0)) enemy = unit.get();
		}
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(enemy, nullptr);
		for(const auto & spell : LIBRARY->spellh->objects)
			attackerSideHero->removeSpellFromSpellbook(spell->id);
		teachBattleSpell(attackerSideHero, SpellID::DISRUPTING_RAY, 6);
		setMagicMastery(attackerSideHero, mastery);
		std::shared_ptr<Bonus> barrier;
		if(scenario == 1 || scenario == 2 || scenario == 3 || scenario == 7)
		{
			const auto type = scenario == 1 ? BonusType::MAGIC_MIRROR
				: scenario == 2 ? BonusType::SPELL_IMMUNITY : BonusType::MAGIC_RESISTANCE;
			barrier = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
				BonusSource::OTHER, scenario == 7 ? 50 : 100, BonusSourceID(),
				BonusSubtypeID(SpellID(SpellID::DISRUPTING_RAY)));
			enemy->addNewBonus(barrier);
		}
		if(scenario == 4)
			actor->shots.use(actor->shots.available());

		std::shared_ptr<Bonus> inactiveWitness;
		if(scenario == 8)
		{
			battle()->obstacles.clear();
			actor->setPosition(BattleHex(94));
			enemy->setPosition(BattleHex(95));
		}
		if(scenario == 9)
		{
			inactiveWitness = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NOT_ACTIVE,
				BonusSource::OTHER, 0, BonusSourceID());
			actor->addNewBonus(inactiveWitness);
		}
		if(scenario == 11)
		{
			auto custom = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
				BonusSource::SPELL_EFFECT, -3, BonusSourceID(SpellID(SpellID::DISRUPTING_RAY)),
				BonusSubtypeID(PrimarySkill::DEFENSE));
			custom->stacking = "unsupported-ray-stacking";
			enemy->addNewBonus(custom);
		}
		battle()->activeStack = actor->unitId();
		const auto id = battle()->getBattleID();
		const auto player = attackerSideHero->getOwner();
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		const auto view = callback->getBattle(id);
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		const auto effects = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::DISRUPTING_RAY)));
		const auto choose = [&]() { return AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *actor); };
		const auto advanceRayRound = [&]()
		{
			if(scenario != 5)
			{
				endRound();
				return;
			}
			const int previousRound = view->battleGetRound();
			for(int step = 0; step < 64; ++step)
			{
				const auto * current = battle()->battleActiveUnit();
				ASSERT_NE(current, nullptr);
				if(view->battleGetRound() > previousRound && current->unitId() == actor->unitId())
					return;
				// Defend adds an unported percentage-defense modifier. Advance the
				// ordinary-census fixture through real no-action turns instead.
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,
					battle()->sideToPlayer(current->unitSide()), BattleAction::makeNoAction(current)));
			}
			FAIL() << "No next native archer activation";
		};
		const auto checkIsolation = [&]()
		{
			const auto health = enemy->getAvailableHealth();
			const auto defense = enemy->getDefense(false);
			const auto mana = attackerSideHero->mana;
			const auto packets = recorder.spellPackets;
			const auto position = enemy->getPosition();
			const auto bonuses = enemy->getAllBonuses(effects);
			const auto selected = choose();
			EXPECT_EQ(enemy->getAvailableHealth(), health);
			EXPECT_EQ(enemy->getDefense(false), defense);
			EXPECT_EQ(enemy->getPosition(), position);
			EXPECT_EQ(attackerSideHero->mana, mana);
			EXPECT_EQ(recorder.spellPackets, packets);
			const auto after = enemy->getAllBonuses(effects);
			EXPECT_EQ(after->size(), bonuses->size());
			for(size_t i = 0; i < std::min(after->size(), bonuses->size()); ++i)
				EXPECT_EQ((*after)[i].get(), (*bonuses)[i].get());
			return selected;
		};
		if(scenario == 2)
		{
			spells::BattleCast legality(view.get(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::DISRUPTING_RAY).toSpell());
			EXPECT_FALSE(SpellID(SpellID::DISRUPTING_RAY).toSpell()->battleMechanics(&legality)->canBeCastAt({battle::Destination(enemy)}));
		}
		if(scenario == 5)
		{
			const auto census = AIIntegration::Ports::HoMM3::captureNativeRangedCensus(*view, BattleSide::ATTACKER);
			ASSERT_TRUE(census);
			ASSERT_EQ(census->enemyIds.size(), 2u);
			ASSERT_EQ(census->rows.size(), 1u);
			ASSERT_TRUE(census->rows[0]);
			ASSERT_TRUE(census->rows[0]->target);
			EXPECT_EQ(census->committed(enemy->unitId()), std::optional<bool>(true));
		}
		if((scenario >= 1 && scenario <= 6 && scenario != 5) || scenario == 8 || scenario == 9 || scenario == 11)
		{
			EXPECT_FALSE(checkIsolation());
			EXPECT_EQ(enemy->valOfBonuses(effects), scenario == 11 ? -3 : 0);
		}
		else
		{
			auto selected = checkIsolation();
			ASSERT_TRUE(selected);
			EXPECT_EQ(selected->spell, SpellID(SpellID::DISRUPTING_RAY));
			ASSERT_FALSE(selected->target.empty());
			EXPECT_TRUE(enemy->getHexes().contains(selected->target.front().hexValue));
			if(barrier)
			{
				// Selection weights a partial resistance; deterministic effect checks use no resistance.
				enemy->removeBonus(barrier);
				selected = checkIsolation();
				ASSERT_TRUE(selected);
			}
			const int bonus = mastery >= MasteryLevel::EXPERT ? 5 : mastery >= MasteryLevel::ADVANCED ? 4 : 3;
			const int castCount = scenario == 10 ? 6 : 2;
			bool castAtZeroDefense = false;
			for(int castIndex = 1; castIndex <= castCount; ++castIndex)
			{
				castAtZeroDefense |= enemy->getDefense(false) == 0;
				const auto mana = attackerSideHero->mana;
				const auto packets = recorder.spellPackets;
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id, player, *selected));
				EXPECT_LT(attackerSideHero->mana, mana);
				EXPECT_GT(recorder.spellPackets, packets);
				EXPECT_EQ(enemy->valOfBonuses(effects), -castIndex * bonus);
				for(const auto selectedBonuses = enemy->getAllBonuses(effects); const auto & effect : *selectedBonuses)
					EXPECT_EQ(effect->duration, BonusDuration::PERMANENT);
				EXPECT_FALSE(choose()) << "A second hero cast in the same round is illegal";
				advanceRayRound();
				ASSERT_FALSE(HasFatalFailure());
				EXPECT_EQ(enemy->valOfBonuses(effects), -castIndex * bonus);
				battle()->activeStack = actor->unitId();
				if(castIndex < castCount)
				{
					if(scenario == 5)
					{
						const auto census = AIIntegration::Ports::HoMM3::captureNativeRangedCensus(*view, BattleSide::ATTACKER);
						ASSERT_TRUE(census);
						ASSERT_EQ(census->committed(enemy->unitId()), std::optional<bool>(true));
					}
					selected = checkIsolation();
					ASSERT_TRUE(selected);
					ASSERT_EQ(selected->spell, SpellID(SpellID::DISRUPTING_RAY));
				}
			}
			endRound();
			ASSERT_FALSE(HasFatalFailure());
			EXPECT_EQ(enemy->valOfBonuses(effects), -castCount * bonus);
			if(scenario == 10)
			{
				EXPECT_TRUE(castAtZeroDefense) << "Ray's source value is not gated on remaining native defense";
				EXPECT_EQ(enemy->getDefense(false), 0);
				injure(enemy, 1);
				const auto wounded = enemy->getAvailableHealth();
				teachBattleSpell(defenderSideHero, SpellID::CURE);
				ASSERT_TRUE(castOn(defenderSideHero, SpellID::CURE, enemy));
				EXPECT_GT(enemy->getAvailableHealth(), wounded);
				EXPECT_EQ(enemy->valOfBonuses(effects), -castCount * bonus);
				endRound();
				ASSERT_FALSE(HasFatalFailure());
				teachBattleSpell(defenderSideHero, SpellID::BLESS);
				ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLESS, enemy));
				ASSERT_TRUE(enemy->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
				endRound();
				ASSERT_FALSE(HasFatalFailure());
				teachBattleSpell(attackerSideHero, SpellID::DISPEL);
				ASSERT_TRUE(castOn(attackerSideHero, SpellID::DISPEL, enemy));
				EXPECT_FALSE(enemy->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
				EXPECT_EQ(enemy->valOfBonuses(effects), -castCount * bonus);
			}

		}
		if(inactiveWitness) actor->removeBonus(inactiveWitness);
		runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
	}
};

TEST_F(AIIntegrationNativeRayBattle, basicRayIsCumulativePermanentAndIsolated) { exerciseRay(MasteryLevel::BASIC); }
TEST_F(AIIntegrationNativeRayBattle, advancedRayIsCumulativePermanentAndIsolated) { exerciseRay(MasteryLevel::ADVANCED); }
TEST_F(AIIntegrationNativeRayBattle, expertRayIsCumulativePermanentAndIsolated) { exerciseRay(MasteryLevel::EXPERT); }
TEST_F(AIIntegrationNativeRayBattle, mirrorMakesDirectRayUnavailable) { exerciseRay(MasteryLevel::BASIC, 1); }
TEST_F(AIIntegrationNativeRayBattle, immunityRejectsRayTarget) { exerciseRay(MasteryLevel::BASIC, 2); }
TEST_F(AIIntegrationNativeRayBattle, fullResistanceMakesRayWorthZero) { exerciseRay(MasteryLevel::BASIC, 3); }
TEST_F(AIIntegrationNativeRayBattle, spentAmmoHasNoQualifiedShooterWitness) { exerciseRay(MasteryLevel::BASIC, 4); }
TEST_F(AIIntegrationNativeRayBattle, multipleEnemiesNeedFullTargetCensus) { exerciseRay(MasteryLevel::BASIC, 5); }
TEST_F(AIIntegrationNativeRayBattle, underdogCannotAssumeKillsOnlyFalse) { exerciseRay(MasteryLevel::BASIC, 6); }
TEST_F(AIIntegrationNativeRayBattle, partialResistanceKeepsPositiveRayValue) { exerciseRay(MasteryLevel::BASIC, 7); }

TEST_F(AIIntegrationNativeRayBattle, blockedShooterHasNoQualifiedWitness) { exerciseRay(MasteryLevel::BASIC, 8); }
TEST_F(AIIntegrationNativeRayBattle, incapacitatedShooterHasNoQualifiedWitness) { exerciseRay(MasteryLevel::BASIC, 9); }
TEST_F(AIIntegrationNativeRayBattle, zeroDefenseRaySurvivesNativeCureAndDispel) { exerciseRay(MasteryLevel::EXPERT, 10); }

TEST_F(AIIntegrationNativeRayBattle, customStackingRayEffectIsUnavailable) { exerciseRay(MasteryLevel::BASIC, 11); }
