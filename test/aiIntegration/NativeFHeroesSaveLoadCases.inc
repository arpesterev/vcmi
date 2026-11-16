/* Actual server save/load around a factory-driven battle; GPL-2.0-or-later.
 * Active battles are deliberately not serialized by CGameState. This gate
 * covers supported adventure-state saves before and after combat, not a
 * midbattle resume or GUI/launcher acceptance claim.
 */
#include "../../lib/serializer/CLoadFile.h"

TEST_F(AIIntegrationNativeBattle, fheroesFactoryCompletesBattleAcrossServerSaveLoad)
{
	startRecordedGame();
	ASSERT_FALSE(HasFatalFailure());
	setArmy(attackerSideHero, CreatureID(3), 30); // Marksmen: native double shots
	setArmy(defenderSideHero, CreatureID(6), 30); // Swordsmen: genuine retaliation
	ASSERT_FALSE(HasFatalFailure());
	const auto attackerID = attackerSideHero->id;
	const auto defenderID = defenderSideHero->id;
	const auto attackerPlayer = attackerSideHero->getOwner();
	const auto defenderPlayer = defenderSideHero->getOwner();
	struct SavedFiles
	{
		std::vector<boost::filesystem::path> paths;
		~SavedFiles() { for(const auto & path : paths) boost::filesystem::remove(path); }
	} savedFiles;
	const auto roundTrip = [&](const std::string & label)
	{
		ASSERT_TRUE(gameState()->currentBattles.empty());
		const auto originalState = gameState();
		const auto savedQuestionCounter = gameHandler->questionCounter;
		const auto saveName = "Saves/" + boost::filesystem::unique_path("fheroes-" + label + "-%%%%-%%%%-%%%%").string();
		StartInfo loadInfo = *gameState()->getStartInfo();
		loadInfo.mode = EStartMode::LOAD_GAME;
		loadInfo.mapname = saveName;
		gameHandler->save(saveName, PlayerColor::NEUTRAL);
		const auto file = CResourceHandler::get("local")->getResourceName(ResourcePath(saveName, EResType::SAVEGAME));
		ASSERT_TRUE(file) << "Native save did not publish its file";
		savedFiles.paths.push_back(*file);
		ASSERT_TRUE(boost::filesystem::exists(*file));
		ASSERT_GT(boost::filesystem::file_size(*file), 0u);
		ASSERT_GE(CLoadFile::readVersion(*file), ESerializationVersion::SAVE_CHUNKS);
		CLoadFile envelope(*file);
		EXPECT_FALSE(envelope.readChunk(SaveFileFormat::CHUNK_METADATA).empty());
		EXPECT_FALSE(envelope.readChunk(SaveFileFormat::CHUNK_HEADER).empty());
		EXPECT_FALSE(envelope.readChunk(SaveFileFormat::CHUNK_GAMESTATE).empty());
		// Mutate only the discarded handler after save. Matching the next
		// restored RNG draw verifies server payload, not just army snapshots.
		const auto expectedNextRandom = gameHandler->randomizer->getDefault().nextInt();
		auto restoredHandler = std::make_shared<CGameHandler>(recorder);
		ASSERT_NO_THROW(restoredHandler->load(loadInfo));
		ASSERT_NE(restoredHandler->gs.get(), originalState.get());
		EXPECT_EQ(restoredHandler->questionCounter, savedQuestionCounter);
		EXPECT_EQ(restoredHandler->randomizer->getDefault().nextInt(), expectedNextRandom);
		adoptLoadedGameState(restoredHandler->gs);
		ASSERT_FALSE(HasFatalFailure());
		recorder.gameState = gameState();
		server.gameState = gameState();
		gameHandler = std::move(restoredHandler);
		attackerSideHero = const_cast<CGHeroInstance *>(gameState()->getHero(attackerID));
		defenderSideHero = const_cast<CGHeroInstance *>(gameState()->getHero(defenderID));
		EXPECT_TRUE(gameState()->currentBattles.empty());
		std::cout << "FHEROES_SERVER_ROUNDTRIP stage=" << label
			<< " bytes=" << boost::filesystem::file_size(*file)
			<< " day=" << gameState()->day << '\n';
	};

	roundTrip("before-battle");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_NE(attackerSideHero, nullptr);
	ASSERT_NE(defenderSideHero, nullptr);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 30);
	EXPECT_EQ(defenderSideHero->getStackCount(SlotID(0)), 30);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), CreatureID(3));
	EXPECT_EQ(defenderSideHero->getCreature(SlotID(0))->getId(), CreatureID(6));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	// Backend selection here is explicit factory invocation, not persistence
	// of a GUI/launcher setting. Actual AIFactory registration, preferences,
	// full packet lifecycle, real server actions and accepted terminal result.
	runToCompletion(std::nullopt, false, "FHeroes2Tactics", "FHeroes2Tactics", true);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(recorder.battleResults.size(), 1u);
	ASSERT_EQ(recorder.acceptedResults, 1);
	ASSERT_TRUE(gameState()->currentBattles.empty());
	EXPECT_GT(recorder.shots, 0);
	EXPECT_GT(recorder.meleeAttacks, 0);
	EXPECT_FALSE(lastAcceptedActionTypesByPlayer[attackerPlayer].empty());
	EXPECT_FALSE(lastAcceptedActionTypesByPlayer[defenderPlayer].empty());
	const auto winnerID = recorder.battleResults.back().winner == BattleSide::ATTACKER ? attackerID : defenderID;
	const auto * winner = gameState()->getHero(winnerID);
	ASSERT_NE(winner, nullptr);
	const auto survivors = winner->getStackCount(SlotID(0));
	const auto survivorType = winner->getCreature(SlotID(0))->getId();
	const auto experience = winner->exp;
	const auto completedDay = gameState()->day;
	ASSERT_GT(survivors, 0);
	roundTrip("after-battle");
	ASSERT_FALSE(HasFatalFailure());
	const auto * restoredWinner = gameState()->getHero(winnerID);
	ASSERT_NE(restoredWinner, nullptr);
	EXPECT_EQ(restoredWinner->getStackCount(SlotID(0)), survivors);
	EXPECT_EQ(restoredWinner->getCreature(SlotID(0))->getId(), survivorType);
	EXPECT_EQ(restoredWinner->exp, experience);
	EXPECT_EQ(gameState()->day, completedDay);
	EXPECT_TRUE(gameState()->currentBattles.empty());
}
