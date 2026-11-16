// GPL-2.0-or-later. Native setup and authoritative siege packets.
class FHeroes2SiegePressureBattle : public AIIntegrationNativeBattle
{
protected:
	std::shared_ptr<DecisionCallback> siegeCallback;
	std::shared_ptr<CPlayerBattleCallback> siegeView;
	const CStack * guard = nullptr;
	std::vector<CStack *> attackers;

	void prepareTowerPressure(BattleSide viewSide = BattleSide::DEFENDER)
	{
		startRecordedGame();
		ASSERT_FALSE(HasFatalFailure());
		setArmy(attackerSideHero, CreatureID(2), 10);
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id, SlotID(1)), CreatureID(2).toCreature(), 10));
		const auto * town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		for(const auto building : {BuildingID::FORT, BuildingID::CITADEL, BuildingID::CASTLE})
			if(!town->hasBuilt(building))
				ASSERT_TRUE(gameHandler->buildStructure(town->id, building, true));
		ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(town->id, SlotID(0)), CreatureID(0).toCreature(), 100));
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, town);
		gameHandler->battles->startBattle(attackerSideHero, town, town->visitablePos(), attackerSideHero, nullptr, layout, town);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		for(const auto & unit : battle()->stacks)
		{
			if(unit->unitSide()==BattleSide::DEFENDER && !unit->isTurret()) guard=unit.get();
			if(unit->unitSide()==BattleSide::ATTACKER && unit->creatureId()==CreatureID(2)) attackers.push_back(unit.get());
		}
		ASSERT_NE(guard,nullptr);
		ASSERT_EQ(attackers.size(),2u);
		attackerSideHero->mana=0;
		attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,0,ChangeValueMode::ABSOLUTE);
		siegeCallback=std::make_shared<DecisionCallback>(battle()->sideToPlayer(viewSide));
		siegeCallback->onBattleStarted(battle());
		siegeView=siegeCallback->getBattle(battle()->getBattleID());
		const auto facts=AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,viewSide);
		ASSERT_TRUE(facts.active);
		ASSERT_GT(facts.towers,0) << "Native tower setup must produce nonzero damage/value, not an inert zero-valued fixture";
	}

	void destroyTower(const CStack * tower)
	{
		CatapultAttack packet;
		packet.battleID=battle()->getBattleID();
		packet.attackedPart=tower->getPosition().toInt()==BattleHex::CASTLE_CENTRAL_TOWER ? EWallPart::KEEP
			: tower->getPosition().toInt()==BattleHex::CASTLE_UPPER_TOWER ? EWallPart::UPPER_TOWER : EWallPart::BOTTOM_TOWER;
		packet.damageDealt=100;
		packet.killedTowerShooter=tower->unitId();
		gameHandler->sendAndApply(packet);
	}
};

TEST_F(FHeroes2SiegePressureBattle, liveTowersAddOnlyShooterPressureAndNeverTroopStrength)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	using namespace AIIntegration::Ports::FHeroes2;
	const auto facts=AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER);
	double expected=0;
	for(const auto * unit : siegeView->battleGetAllStacks(true))
		if(unit->isTurret() && unit->alive()) expected+=unit->estimateCombatValue();
	EXPECT_DOUBLE_EQ(facts.towers,expected);
	for(const auto * unit : siegeView->battleGetStacks(CBattleInfoEssentials::MINE_AND_ENEMY))
		EXPECT_FALSE(unit->isTurret());
	ASSERT_EQ(facts.attackerIgnoresCover,false);
	TacticsInput input;
	input.armyStrength=500; input.enemyArmyStrength=600;
	input.shootersStrength=120; input.enemyShootersStrength=90;
	applySiegePressure(input,facts);
	EXPECT_DOUBLE_EQ(input.shootersStrength,120+expected);
	EXPECT_DOUBLE_EQ(input.enemyShootersStrength,60);
	EXPECT_DOUBLE_EQ(input.armyStrength,500);
	EXPECT_DOUBLE_EQ(input.enemyArmyStrength,600);
}

TEST_F(FHeroes2SiegePressureBattle, lastTowerPacketDisablesSiegeDespiteIntactGateAndChangesGeometry)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	BattleHex outsideWalls;
	for(int index=0;index<GameConstants::BFIELD_SIZE;++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && candidate.getX()>=GameConstants::BFIELD_WIDTH/2
			&& !siegeView->battleIsInsideWalls(candidate)) { outsideWalls=candidate; break; }
	}
	ASSERT_TRUE(outsideWalls.isAvailable());
	EXPECT_FALSE(AIIntegration::FHeroes2TacticsContext::isPositionInDefendedArea(*siegeView,*guard,outsideWalls));
	const auto towers=siegeView->battleGetAllStacks(true);
	for(const auto * tower : towers)
		if(tower->isTurret() && tower->alive()) destroyTower(tower);
	EXPECT_EQ(siegeView->battleGetWallState(EWallPart::GATE),EWallState::INTACT);
	const auto facts=AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER);
	EXPECT_FALSE(facts.active);
	EXPECT_EQ(facts.towers,0);
	EXPECT_TRUE(AIIntegration::FHeroes2TacticsContext::isPositionInDefendedArea(*siegeView,*guard,outsideWalls));
	AIIntegration::Ports::FHeroes2::TacticsInput input;
	input.shootersStrength=120; input.enemyShootersStrength=90;
	AIIntegration::Ports::FHeroes2::applySiegePressure(input,facts);
	EXPECT_FALSE(input.defendingCastle);
	EXPECT_DOUBLE_EQ(input.shootersStrength,120);
	EXPECT_DOUBLE_EQ(input.enemyShootersStrength,90);
	EXPECT_GT(recorder.catapultEventPackets,0);
}

TEST_F(FHeroes2SiegePressureBattle, brokenGateWithLiveTowerRetainsCastleGeometry)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	CatapultAttack packet;
	packet.battleID=battle()->getBattleID(); packet.attackedPart=EWallPart::GATE; packet.damageDealt=100;
	gameHandler->sendAndApply(packet);
	EXPECT_EQ(siegeView->battleGetWallState(EWallPart::GATE),EWallState::DESTROYED);
	EXPECT_TRUE(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).active);
	EXPECT_TRUE(AIIntegration::FHeroes2TacticsContext::isPositionInDefendedArea(*siegeView,*guard,guard->getPosition()));
}

TEST_F(FHeroes2SiegePressureBattle, publicNativeArcheryMetadataBypassesSourceCoverWithoutHiddenHero)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).attackerIgnoresCover,false);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,MasteryLevel::BASIC,ChangeValueMode::ABSOLUTE);
	for(const auto * archer : attackers)
	{
		const auto bonuses=archer->getAllBonuses(Selector::source(BonusSource::SECONDARY_SKILL,BonusSourceID(SecondarySkill(SecondarySkill::ARCHERY))));
		ASSERT_FALSE(bonuses->empty());
		EXPECT_EQ(bonuses->front()->type,BonusType::PERCENTAGE_DAMAGE_BOOST);
		EXPECT_EQ(bonuses->front()->subtype,BonusCustomSubtype::damageTypeRanged);
		EXPECT_EQ(bonuses->front()->valType,BonusValueType::BASE_NUMBER);
	}
	EXPECT_EQ(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).attackerIgnoresCover,true);
	attackerSideHero->setSecSkillLevel(SecondarySkill::ARCHERY,0,ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).attackerIgnoresCover,false);
}

TEST_F(FHeroes2SiegePressureBattle, goldenBowArtifactBypassesButInnateNoWallDoesNot)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	for(auto * archer : attackers)
		archer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_WALL_PENALTY,
			BonusSource::CREATURE_ABILITY,0,BonusSourceID()));
	EXPECT_EQ(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).attackerIgnoresCover,false);
	giveArtifact(attackerSideHero,ArtifactID(91),ArtifactPosition::MISC1);
	ASSERT_FALSE(HasFatalFailure());
	for(const auto * archer : attackers)
		ASSERT_FALSE(archer->getAllBonuses(Selector::type()(BonusType::NO_WALL_PENALTY)
			.And(Selector::sourceTypeSel(BonusSource::ARTIFACT)))->empty());
	EXPECT_EQ(AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER).attackerIgnoresCover,true);
}

TEST_F(FHeroes2SiegePressureBattle, conditionalStackOnlyArtifactDoesNotInventGlobalCoverKnowledge)
{
	prepareTowerPressure();
	ASSERT_FALSE(HasFatalFailure());
	attackers.front()->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::NO_WALL_PENALTY,
		BonusSource::ARTIFACT,0,BonusSourceID()));
	const auto facts=AIIntegration::FHeroes2TacticsContext::siegePressure(*siegeView,BattleSide::DEFENDER);
	EXPECT_FALSE(facts.attackerIgnoresCover.has_value());
	AIIntegration::Ports::FHeroes2::TacticsInput input;
	input.enemyShootersStrength=90;
	AIIntegration::Ports::FHeroes2::applySiegePressure(input,facts);
	EXPECT_DOUBLE_EQ(input.enemyShootersStrength,90);
}

TEST_F(AIIntegrationNativeBattle, fheroes2FortWithoutTowersDoesNotGrantCastleStanceOrGeometry)
{
	prepareSiege(false, CreatureID(0), 100, CreatureID(0), 100);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_NE(battle()->battleGetDefendedTown(), nullptr);
	ASSERT_GT(static_cast<int>(battle()->battleGetWallState(EWallPart::GATE)), static_cast<int>(EWallState::DESTROYED));
	for(const auto * unit : battle()->battleGetAllStacks(true))
		ASSERT_FALSE(unit->isTurret() && unit->alive());
	const CStack * guard = nullptr;
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER && !unit->isTurret()) guard = unit.get();
	ASSERT_NE(guard, nullptr);
	auto callback = std::make_shared<DecisionCallback>(battle()->sideToPlayer(BattleSide::DEFENDER));
	callback->onBattleStarted(battle());
	const auto view = callback->getBattle(battle()->getBattleID());
	const auto facts = AIIntegration::FHeroes2TacticsContext::siegePressure(*view, BattleSide::DEFENDER);
	EXPECT_FALSE(facts.active);
	EXPECT_EQ(facts.towers, 0);
	const auto enemies = view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
	EXPECT_FALSE(AIIntegration::FHeroes2TacticsContext::tactics(*view, *guard, enemies).defensive);
	BattleHex outsideWalls;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && candidate.getX() >= GameConstants::BFIELD_WIDTH / 2
			&& !view->battleIsInsideWalls(candidate)) { outsideWalls = candidate; break; }
	}
	ASSERT_TRUE(outsideWalls.isAvailable());
	EXPECT_TRUE(AIIntegration::FHeroes2TacticsContext::isPositionInDefendedArea(*view, *guard, outsideWalls));
}
