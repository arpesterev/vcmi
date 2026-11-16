#include "homm3Mirth/PinnedMirth.h"

TEST(HoMM3MirthSource, nonnegativeMoraleDurationMatchesPinnedBody)
{
	using namespace AIIntegration::Ports::HoMM3;
	for(const int total : {1, 59, 1000, 1000000})
		for(int morale = 0; morale <= 3; ++morale)
			for(int mastery = 0; mastery <= 3; ++mastery)
				for(int rounds = 1; rounds <= 7; ++rounds)
					for(const int turns : {1, 2, 6, 10})
						for(const bool done : {false, true})
						{
							PinnedMirth::army unit{total, morale, false, done};
							PinnedMirth::type_AI_spellcaster oracle;
							oracle.rounds = rounds;
							EXPECT_EQ(mirthSpellValue(total, morale, mastery >= 2 ? 2 : 1,
								spellDurationValue(turns, rounds, done)), oracle.getMirthValue(&unit, {mastery, turns}));
						}
}

class AIIntegrationNativeMirth : public AIIntegrationNativeBattle
{
protected:
	enum class Case { Enemy, Capped, Immune, Mixed, Friendly, MassCost, MassGain, Moved, Waited, Negative, MixedArmy, Clipped };

	void exercise(Case scenario, int initialMorale = 1, int mirthMastery = MasteryLevel::ADVANCED)
	{
		const bool mass = scenario == Case::MassCost || scenario == Case::MassGain;
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(0), scenario == Case::MassGain ? 50 : 200);
		setArmy(defenderSideHero, CreatureID(0), scenario == Case::MassCost ? 50 : 200);
		if(scenario == Case::MixedArmy)
		{
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(14).toCreature(), 1));
			ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(2)), CreatureID(70).toCreature(), 1));
		}
		for(auto * hero : {attackerSideHero, defenderSideHero})
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::LEADERSHIP), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->obstacles.clear();
		CStack * ally = nullptr;
		CStack * enemy = nullptr;
		for(const auto & stack : battle()->stacks)
			if(stack->creatureId() == CreatureID(0))
				(stack->unitSide() == BattleSide::ATTACKER ? ally : enemy) = stack.get();
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_EQ(ally->moraleVal(), 1);
		ASSERT_EQ(enemy->moraleVal(), scenario == Case::MixedArmy ? -1 : 1);
		ally->setPosition(BattleHex(87));
		enemy->setPosition(BattleHex(89));
		teachBattleSpell(attackerSideHero, SpellID::DISPEL);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
			mass ? MasteryLevel::EXPERT : MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		const auto battleID = battle()->getBattleID();
		const auto player = attackerSideHero->getOwner();
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		const auto view = callback->getBattle(battleID);
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		ASSERT_TRUE(view->battleCanAttackUnit(enemy, ally));
		ASSERT_FALSE(view->battleGetAvailableHexes(enemy, false).empty());
		const auto mirthSelector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::MIRTH)));
		const auto choose = [&]() { return AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *ally); };
		EXPECT_FALSE(choose()) << "No effects or wounds must conserve Dispel mana";

		CStack * target = scenario == Case::Friendly ? ally : enemy;
		if(scenario == Case::Negative && initialMorale == 1) initialMorale = -1;
		if(scenario == Case::Clipped) initialMorale = -5;
		if(scenario != Case::MixedArmy && (scenario == Case::Capped || initialMorale != 1))
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
				BonusSource::CREATURE_ABILITY, scenario == Case::Capped ? 2 : initialMorale - 1, BonusSourceID(CreatureID(0))));
		const int beforeMirth = target->moraleVal();
		const int turns = scenario == Case::Moved || scenario == Case::Waited ? 1 : 6;
		const auto applyMirth = [&](CStack * unit, CGHeroInstance * hero)
		{
			spells::BattleCast cast(battle(), hero, spells::Mode::PASSIVE, SpellID(SpellID::MIRTH).toSpell());
			cast.setSpellLevel(mirthMastery);
			cast.setEffectDuration(turns);
			cast.cast(gameHandler->spellEnv.get(), spells::Target{battle::Destination(unit)});
		};
		applyMirth(target, scenario == Case::Friendly ? attackerSideHero : defenderSideHero);
		ASSERT_TRUE(target->hasBonus(mirthSelector));
		ASSERT_EQ(target->moraleVal(), scenario == Case::Clipped ? -3 : std::min(3, beforeMirth + (mirthMastery >= MasteryLevel::ADVANCED ? 2 : 1)));
		if(mass)
		{
			applyMirth(ally, attackerSideHero);
			ASSERT_TRUE(ally->hasBonus(mirthSelector));
		}
		if(scenario == Case::Immune)
		{
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NO_MORALE,
				BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(0))));
			ASSERT_EQ(target->moraleVal(), 0);
		}
		if(scenario == Case::Mixed)
		{
			spells::BattleCast bless(battle(), defenderSideHero, spells::Mode::PASSIVE, SpellID(SpellID::BLESS).toSpell());
			bless.setEffectDuration(6);
			bless.cast(gameHandler->spellEnv.get(), spells::Target{battle::Destination(target)});
			ASSERT_TRUE(target->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
		}
		if(scenario == Case::Moved || scenario == Case::Waited)
		{
			battle()->activeStack = enemy->unitId();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, defenderSideHero->getOwner(),
				scenario == Case::Moved ? BattleAction::makeNoAction(enemy) : BattleAction::makeWait(enemy)));
			EXPECT_EQ(enemy->moved(), scenario == Case::Moved);
			EXPECT_EQ(enemy->waited(), scenario == Case::Waited);
		}
		battle()->activeStack = ally->unitId();
		const auto health = target->getAvailableHealth();
		const auto mana = attackerSideHero->mana;
		const auto position = target->getPosition();
		const auto morale = target->moraleVal();
		const auto casts = recorder.spellPackets;
		const auto selected = choose();
		const auto repeated = choose();
		EXPECT_EQ(bool(selected), bool(repeated));
		EXPECT_EQ(target->getAvailableHealth(), health);
		EXPECT_EQ(target->getPosition(), position);
		EXPECT_EQ(target->moraleVal(), morale);
		EXPECT_EQ(attackerSideHero->mana, mana);
		EXPECT_EQ(recorder.spellPackets, casts);
		EXPECT_TRUE(target->hasBonus(mirthSelector));
		for(const auto selectedBonuses = target->getAllBonuses(mirthSelector); const auto & bonus : *selectedBonuses)
			EXPECT_EQ(bonus->turnsRemain, turns);
		const bool valuable = scenario == Case::Enemy || scenario == Case::MassGain || scenario == Case::Waited || scenario == Case::Negative || scenario == Case::MixedArmy;
		if(!valuable)
		{
			EXPECT_FALSE(selected);
			return;
		}
		ASSERT_TRUE(selected);
		ASSERT_TRUE(repeated);
		EXPECT_EQ(selected->toString(), repeated->toString());
		EXPECT_EQ(selected->spell, SpellID::DISPEL);
		if(!mass)
		{
			ASSERT_FALSE(selected->target.empty());
			EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue), enemy);
		}
		auto policy = AIFactory::createBattleAI("HoMM3OriginalTactics");
		ASSERT_NE(policy, nullptr);
		policy->playerID = player;
		AutocombatPreferences prefs;
		prefs.enableSpellsUsage = true;
		policy->initBattleInterface(environment, callback, prefs);
		policy->battleStart(battleID, battle()->getSideArmy(BattleSide::ATTACKER),
			battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		policy->activeStack(battleID, ally);
		ASSERT_TRUE(callback->action);
		ASSERT_TRUE(callback->spellAction);
		ASSERT_EQ(callback->action->toString(), selected->toString());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *selected));
		EXPECT_FALSE(target->hasBonus(mirthSelector));
		EXPECT_EQ(target->moraleVal(), beforeMirth);
		EXPECT_EQ(target->getAvailableHealth(), health);
		EXPECT_LT(attackerSideHero->mana, mana);
		if(mass)
		{
			EXPECT_FALSE(ally->hasBonus(mirthSelector));
			EXPECT_EQ(ally->moraleVal(), 1);
		}
		policy.reset();
		attackerSideHero->mana = 0;
		runToCompletion(std::nullopt, false, "HoMM3OriginalTactics");
		EXPECT_FALSE(recorder.battleResults.empty());
		EXPECT_GT(recorder.damage, 0);
	}
};

TEST_F(AIIntegrationNativeMirth, dispelsEnemyMirthAndCompletesBattle) { exercise(Case::Enemy); }
TEST_F(AIIntegrationNativeMirth, cappedMoraleConservesMana) { exercise(Case::Capped); }
TEST_F(AIIntegrationNativeMirth, retainedMoraleImmunityConservesMana) { exercise(Case::Immune); }
TEST_F(AIIntegrationNativeMirth, mixedMirthAndBlessRemainUnsupported) { exercise(Case::Mixed); }
TEST_F(AIIntegrationNativeMirth, friendlyMirthIsARemovalCost) { exercise(Case::Friendly); }
TEST_F(AIIntegrationNativeMirth, massDispelRejectsGreaterFriendlyLoss) { exercise(Case::MassCost); }
TEST_F(AIIntegrationNativeMirth, massDispelAcceptsNetEnemyLoss) { exercise(Case::MassGain); }
TEST_F(AIIntegrationNativeMirth, oneRoundMovedEnemyConservesMana) { exercise(Case::Moved); }
TEST_F(AIIntegrationNativeMirth, oneRoundWaitingEnemyRemainsValued) { exercise(Case::Waited); }
TEST_F(AIIntegrationNativeMirth, dispelsMirthOverNegativeMoraleAndCompletesBattle) { exercise(Case::Negative); }

TEST_F(AIIntegrationNativeMirth, equalPostRemovalValuesConserveMassDispelMana)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		setArmy(hero, CreatureID(0), 200);
		hero->setPrimarySkill(PrimarySkill::ATTACK, 0, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::LEADERSHIP), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	battle()->obstacles.clear();
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	for(const auto & stack : battle()->stacks)
		(stack->unitSide() == BattleSide::ATTACKER ? ally : enemy) = stack.get();
	ASSERT_NE(ally, nullptr);
	ASSERT_NE(enemy, nullptr);
	ally->setPosition(BattleHex(87));
	enemy->setPosition(BattleHex(89));
	for(auto * unit : {ally, enemy})
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(0))));
		ASSERT_EQ(unit->moraleVal(), 2);
	}
	const auto baselineValue = ally->estimateCombatValue();
	ASSERT_EQ(enemy->estimateCombatValue(), baselineValue);
	teachBattleSpell(attackerSideHero, SpellID::DISPEL);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::WATER_MAGIC),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	for(auto * unit : {ally, enemy})
	{
		spells::BattleCast cast(battle(), unit == ally ? attackerSideHero : defenderSideHero,
			spells::Mode::PASSIVE, SpellID(SpellID::MIRTH).toSpell());
		cast.setSpellLevel(unit == ally ? MasteryLevel::BASIC : MasteryLevel::ADVANCED);
		cast.setEffectDuration(6);
		cast.cast(gameHandler->spellEnv.get(), spells::Target{battle::Destination(unit)});
		ASSERT_EQ(unit->moraleVal(), 3);
	}
	const auto allyBuffedValue = ally->estimateCombatValue();
	const auto enemyBuffedValue = enemy->estimateCombatValue();
	ASSERT_GT(enemyBuffedValue, allyBuffedValue)
		<< "Native combat value includes raw morale above the effective morale cap";
	const int rounds = AIIntegration::Ports::HoMM3::spellPlanningRounds(allyBuffedValue, enemyBuffedValue);
	const double duration = AIIntegration::Ports::HoMM3::spellDurationValue(6, rounds, false);
	ASSERT_GT(AIIntegration::Ports::HoMM3::mirthSpellValue(enemyBuffedValue, 2, 2, duration),
		AIIntegration::Ports::HoMM3::mirthSpellValue(allyBuffedValue, 2, 1, duration))
		<< "Pricing the still-buffed stacks would incorrectly favor mass Dispel";
	const auto mirth = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::MIRTH)));
	ASSERT_EQ(ally->valOfBonuses(mirth), 1);
	ASSERT_EQ(enemy->valOfBonuses(mirth), 2);
	for(auto * unit : {ally, enemy})
		for(const auto selectedBonuses = unit->getAllBonuses(mirth); const auto & bonus : *selectedBonuses)
			ASSERT_EQ(bonus->turnsRemain, 6);

	battle()->activeStack = ally->unitId();
	const auto battleID = battle()->getBattleID();
	auto callback = std::make_shared<DecisionCallback>(attackerSideHero->getOwner());
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battleID);
	auto environment = std::make_shared<NativePortEnvironment>(makeCallback(attackerSideHero->getOwner()), callback);
	const auto health = ally->getAvailableHealth();
	const auto mana = attackerSideHero->mana;
	const auto spellPackets = recorder.spellPackets;
	const auto * spell = SpellID(SpellID::DISPEL).toSpell();
	spells::BattleCast legality(view.get(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_TRUE(spell->battleMechanics(&legality)->isMassive());
	const spells::Target target{battle::Destination(enemy)};
	ASSERT_TRUE(spell->battleMechanics(&legality)->canBeCastAt(target));
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), view);
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
	preview.castEval(projected->getServerCallback(), target);
	const auto * afterAlly = projected->battleGetUnitByID(ally->unitId());
	const auto * afterEnemy = projected->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(afterAlly, nullptr);
	ASSERT_NE(afterEnemy, nullptr);
	for(const auto * after : {afterAlly, afterEnemy})
	{
		EXPECT_FALSE(after->hasBonus(mirth));
		EXPECT_EQ(after->moraleVal(), 2);
		EXPECT_EQ(after->estimateCombatValue(), baselineValue);
		EXPECT_EQ(after->getAvailableHealth(), health);
	}
	EXPECT_EQ(afterAlly->estimateCombatValue(), afterEnemy->estimateCombatValue());
	for(int repeat = 0; repeat < 2; ++repeat)
		EXPECT_FALSE(AIIntegration::HoMM3SpellPolicy::choose(view, environment.get(), *ally))
			<< "Equal effective Mirth losses must conserve mass Dispel mana";
	EXPECT_EQ(ally->estimateCombatValue(), allyBuffedValue);
	EXPECT_EQ(enemy->estimateCombatValue(), enemyBuffedValue);
	for(const auto * unit : {ally, enemy})
	{
		EXPECT_TRUE(unit->hasBonus(mirth));
		EXPECT_EQ(unit->moraleVal(), 3);
		EXPECT_EQ(unit->getAvailableHealth(), health);
	}
	EXPECT_EQ(ally->getPosition(), BattleHex(87));
	EXPECT_EQ(enemy->getPosition(), BattleHex(89));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(recorder.spellPackets, spellPackets);
}

TEST(HoMM3MirthSource, negativeMoraleDurationMatchesPinnedBody)
{
 using namespace AIIntegration::Ports::HoMM3;
 for(const int total:{1,59,1000,1000000})
  for(int morale=-3;morale<0;++morale)
   for(int mastery=0;mastery<=3;++mastery)
    for(int rounds=1;rounds<=7;++rounds)
     for(const int turns:{1,2,6,10})
      for(const bool done:{false,true})
      {
       PinnedMirth::army unit{total,morale,false,done};PinnedMirth::type_AI_spellcaster oracle;oracle.rounds=rounds;
       EXPECT_EQ(mirthSpellValue(total,morale,mastery>=2?2:1,spellDurationValue(turns,rounds,done)),
        oracle.getMirthValue(&unit,{mastery,turns}));
      }
}

class AIIntegrationNativeMirthCasting : public AIIntegrationNativeBattle
{
protected:
 void exercise(int mastery,int baseMorale,bool immune=false,bool existing=false,bool sorrow=false)
 {
  startRecordedGame();ASSERT_FALSE(HasFatalFailure());
  for(auto * hero:{attackerSideHero,defenderSideHero})
  {
   setArmy(hero,CreatureID(0),200);
   hero->setSecSkillLevel(SecondarySkill::LEADERSHIP,MasteryLevel::NONE,ChangeValueMode::ABSOLUTE);
  }
  const bool mass=mastery==MasteryLevel::EXPERT;
  if(mass)ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),CreatureID(0).toCreature(),100));
  teachBattleSpell(attackerSideHero,SpellID::MIRTH);
  attackerSideHero->setSecSkillLevel(SecondarySkill::WATER_MAGIC,mastery,ChangeValueMode::ABSOLUTE);
  gameHandler->battles->startBattle(attackerSideHero,defenderSideHero);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  std::vector<CStack *> allies;CStack * enemy=nullptr;
  for(const auto & unit:battle()->stacks)
   if(unit->unitSide()==BattleSide::ATTACKER)allies.push_back(unit.get());else enemy=unit.get();
  ASSERT_FALSE(allies.empty());ASSERT_NE(enemy,nullptr);
  CStack * ally=allies.front();
  for(auto * unit:allies)
  {
   auto moraleBonus=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MORALE,BonusSource::OTHER,baseMorale-unit->moraleVal(),BonusSourceID());
   moraleBonus->bonusOwner=attackerSideHero->getOwner();unit->addNewBonus(moraleBonus);
   ASSERT_EQ(unit->moraleVal(),baseMorale);
   if(immune)unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_MORALE,BonusSource::OTHER,1,BonusSourceID()));
   if(existing || sorrow)
   {
    spells::BattleCast cast(battle(),existing?attackerSideHero:defenderSideHero,spells::Mode::PASSIVE,SpellID(existing?SpellID::MIRTH:SpellID::SORROW).toSpell());
    cast.setSpellLevel(MasteryLevel::ADVANCED);cast.setEffectDuration(6);
    cast.cast(gameHandler->spellEnv.get(),spells::Target{battle::Destination(unit)});
    ASSERT_TRUE(unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
     BonusSourceID(SpellID(existing?SpellID::MIRTH:SpellID::SORROW)))));
   }
  }
  const auto battleID=battle()->getBattleID();const auto player=attackerSideHero->getOwner();
  battle()->activeStack=ally->unitId();
  auto callback=std::make_shared<DecisionCallback>(player);callback->onBattleStarted(battle());
  const auto view=callback->getBattle(battleID);
  auto environment=std::make_shared<NativePortEnvironment>(makeCallback(player),callback);
  const auto mana=attackerSideHero->mana;const auto health=ally->getAvailableHealth();
  const auto morale=ally->moraleVal();const auto packets=recorder.spellPackets;
  // Ordinary preview owns separate bonus objects but must retain their owner and value.
  auto preview=std::make_shared<HypotheticBattle>(environment.get(),view);
  const auto * copy=preview->battleGetUnitByID(ally->unitId());ASSERT_NE(copy,nullptr);
  const auto selector=Selector::type()(BonusType::MORALE).And(Selector::sourceType()(BonusSource::OTHER));
  const auto liveBonuses=ally->getAllBonuses(selector),copiedBonuses=copy->getAllBonuses(selector);
  ASSERT_EQ(liveBonuses->size(),1u);ASSERT_EQ(copiedBonuses->size(),1u);
  EXPECT_NE(liveBonuses->front().get(),copiedBonuses->front().get());
  EXPECT_EQ(copiedBonuses->front()->bonusOwner,player);
  EXPECT_TRUE(AIIntegration::Ports::HoMM3::sameMirthBaselineBonus(*liveBonuses->front(),*copiedBonuses->front()));
  const auto choose=[&](){return AIIntegration::HoMM3SpellPolicy::choose(view,environment.get(),*ally);};
  const auto selected=choose();const auto repeated=choose();
  EXPECT_EQ(bool(selected),bool(repeated));EXPECT_EQ(attackerSideHero->mana,mana);
  EXPECT_EQ(ally->getAvailableHealth(),health);EXPECT_EQ(ally->moraleVal(),morale);EXPECT_EQ(recorder.spellPackets,packets);
  if(baseMorale==3 || immune || existing || sorrow){EXPECT_FALSE(selected);return;}
  ASSERT_TRUE(selected);ASSERT_TRUE(repeated);EXPECT_EQ(selected->toString(),repeated->toString());
  EXPECT_EQ(selected->spell,SpellID::MIRTH);
  auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");ASSERT_NE(policy,nullptr);
  policy->playerID=player;AutocombatPreferences prefs;prefs.enableSpellsUsage=true;
  policy->initBattleInterface(environment,callback,prefs);
  policy->battleStart(battleID,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),
   battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy->activeStack(battleID,ally);
  ASSERT_TRUE(callback->action);ASSERT_TRUE(callback->spellAction);EXPECT_EQ(callback->action->toString(),selected->toString());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID,player,*selected));
  const int amount=mastery>=MasteryLevel::ADVANCED?2:1;
  for(auto * unit:allies)EXPECT_EQ(unit->moraleVal(),std::min(3,baseMorale+amount));
  EXPECT_LT(attackerSideHero->mana,mana);EXPECT_GT(recorder.spellPackets,packets);
  EXPECT_EQ(ally->getAvailableHealth(),health);
 }
};
TEST_F(AIIntegrationNativeMirthCasting,basicTargetedCast) {exercise(MasteryLevel::BASIC,0);}
TEST_F(AIIntegrationNativeMirthCasting,advancedTargetedCast) {exercise(MasteryLevel::ADVANCED,0);}
TEST_F(AIIntegrationNativeMirthCasting,expertMassCast) {exercise(MasteryLevel::EXPERT,0);}
TEST_F(AIIntegrationNativeMirthCasting,negativeMoraleCast) {exercise(MasteryLevel::ADVANCED,-3);}
TEST_F(AIIntegrationNativeMirthCasting,cappedMoraleConservesMana) {exercise(MasteryLevel::ADVANCED,3);}
TEST_F(AIIntegrationNativeMirthCasting,immuneConservesMana) {exercise(MasteryLevel::ADVANCED,0,true);}
TEST_F(AIIntegrationNativeMirthCasting,existingMirthConservesMana) {exercise(MasteryLevel::ADVANCED,0,false,true);}
TEST_F(AIIntegrationNativeMirthCasting,sorrowCancellationRemainsUnsupported) {exercise(MasteryLevel::ADVANCED,0,false,false,true);}

TEST(HoMM3MirthSource, baselineGuardHandlesNativeTerrainAndDetectsChanges)
{
 using namespace AIIntegration::Ports::HoMM3;
 Bonus a(BonusDuration::PERMANENT,BonusType::MORALE,BonusSource::OTHER,1,BonusSourceID());
 a.bonusOwner=PlayerColor(0);
 a.limiter=std::make_shared<TerrainLimiter>();
 Bonus b=a;b.limiter=std::make_shared<TerrainLimiter>();
 EXPECT_TRUE(sameMirthBaselineBonus(a,b));
 b.val=2;EXPECT_FALSE(sameMirthBaselineBonus(a,b));b.val=a.val;
 b.bonusOwner=PlayerColor(1);EXPECT_FALSE(sameMirthBaselineBonus(a,b));b.bonusOwner=a.bonusOwner;
 b.limiter=std::make_shared<TerrainLimiter>(ETerrainId::SAND);EXPECT_FALSE(sameMirthBaselineBonus(a,b));
 a.limiter=std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{std::make_shared<TerrainLimiter>()});
 b.limiter=std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{std::make_shared<TerrainLimiter>()});
 EXPECT_TRUE(sameMirthBaselineBonus(a,b));
 b.limiter=std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{std::make_shared<TerrainLimiter>(ETerrainId::SAND)});
 EXPECT_FALSE(sameMirthBaselineBonus(a,b));
}

TEST_F(AIIntegrationNativeMirth, negativeThreeBasicCancellation) { exercise(Case::Negative, -3, MasteryLevel::BASIC); }
TEST_F(AIIntegrationNativeMirth, negativeThreeAdvancedCancellation) { exercise(Case::Negative, -3); }
TEST_F(AIIntegrationNativeMirth, negativeTwoBasicCancellation) { exercise(Case::Negative, -2, MasteryLevel::BASIC); }
TEST_F(AIIntegrationNativeMirth, negativeTwoAdvancedCancellation) { exercise(Case::Negative, -2); }
TEST_F(AIIntegrationNativeMirth, negativeOneBasicCancellation) { exercise(Case::Negative, -1, MasteryLevel::BASIC); }
TEST_F(AIIntegrationNativeMirth, negativeFriendlyMirthRemainsRemovalCost) { exercise(Case::Friendly, -3); }
TEST_F(AIIntegrationNativeMirth, negativeMixedMirthBlessRemainsUnsupported) { exercise(Case::Mixed, -3); }
TEST_F(AIIntegrationNativeMirth, negativeOneTurnMovedConservesMana) { exercise(Case::Moved, -3); }
TEST_F(AIIntegrationNativeMirth, negativeOneTurnWaitingRemainsValued) { exercise(Case::Waited, -3); }
TEST_F(AIIntegrationNativeMirth, belowFloorMoraleCannotInventCancellationValue) { exercise(Case::Clipped); }
TEST_F(AIIntegrationNativeMirth, mixedFactionArmyMirthIsDispelledAndBattleCompletes) { exercise(Case::MixedArmy); }
