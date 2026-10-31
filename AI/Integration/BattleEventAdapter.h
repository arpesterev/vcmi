/*
 * BattleEventAdapter.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../../lib/callback/CBattleGameInterface.h"
#include "EventChannel.h"

namespace AIIntegration
{
/// Preserves native event payloads and ordering; does not emulate foreign rules.
class BattleEventAdapter final : public CBattleGameInterface
{
public:
	BattleEventAdapter(std::string name, std::shared_ptr<CBattleGameInterface> implementation,
		EventChannel::Observer observer = {})
		: implementation(std::move(implementation)), events(name, std::move(observer))
	{
		if(!this->implementation)
			throw std::invalid_argument("AI adapter requires an actual backend implementation");
		dllName = std::move(name);
	}

	void initBattleInterface(std::shared_ptr<Environment> env, std::shared_ptr<CBattleCallback> cb) override
	{
		syncIdentity();
		events.deliver(-1, "initialize", [&] { implementation->initBattleInterface(std::move(env), std::move(cb)); });
	}

	void initBattleInterface(std::shared_ptr<Environment> env, std::shared_ptr<CBattleCallback> cb, AutocombatPreferences prefs) override
	{
		syncIdentity();
		events.deliver(-1, "initializeAutocombat", [&] { implementation->initBattleInterface(std::move(env), std::move(cb), prefs); });
	}

	void activeStack(const BattleID & battleID, const CStack * stack) override
	{
		events.deliver(battleID.getNum(), "activeStack", [&] { implementation->activeStack(battleID, stack); });
	}

	void yourTacticPhase(const BattleID & battleID, int distance) override
	{
		events.deliver(battleID.getNum(), "yourTacticPhase", [&] { implementation->yourTacticPhase(battleID, distance); });
	}

	void actionFinished(const BattleID & battleID, const BattleAction & action) override
	{
		events.deliver(battleID.getNum(), "actionFinished", [&] { implementation->actionFinished(battleID, action); });
	}

	void actionStarted(const BattleID & battleID, const BattleAction & action) override
	{
		events.deliver(battleID.getNum(), "actionStarted", [&] { implementation->actionStarted(battleID, action); });
	}

	void battleAttack(const BattleID & battleID, const BattleAttack * attack) override
	{
		events.deliver(battleID.getNum(), "battleAttack", [&] { implementation->battleAttack(battleID, attack); });
	}

	void battleStacksAttacked(const BattleID & battleID, const std::vector<BattleStackAttacked> & stacks, bool ranged) override
	{
		events.deliver(battleID.getNum(), "battleStacksAttacked", [&] { implementation->battleStacksAttacked(battleID, stacks, ranged); });
	}

	void battleEnd(const BattleID & battleID, const BattleResult * result, QuestionID question) override
	{
		events.deliver(battleID.getNum(), "battleEnd", [&] { implementation->battleEnd(battleID, result, question); });
	}

	void battleNewRoundFirst(const BattleID & battleID) override
	{
		events.deliver(battleID.getNum(), "battleNewRoundFirst", [&] { implementation->battleNewRoundFirst(battleID); });
	}

	void battleTacticsEnded(const BattleID & battleID) override
	{
		events.deliver(battleID.getNum(), "battleTacticsEnded", [&] { implementation->battleTacticsEnded(battleID); });
	}

	void battleNewRound(const BattleID & battleID) override
	{
		events.deliver(battleID.getNum(), "battleNewRound", [&] { implementation->battleNewRound(battleID); });
	}

	void battleLogMessage(const BattleID & battleID, const std::vector<MetaString> & lines) override
	{
		events.deliver(battleID.getNum(), "battleLogMessage", [&] { implementation->battleLogMessage(battleID, lines); });
	}

	void battleStackMoved(const BattleID & battleID, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport) override
	{
		events.deliver(battleID.getNum(), "battleStackMoved", [&] { implementation->battleStackMoved(battleID, stack, dest, distance, teleport); });
	}

	void battleSpellCast(const BattleID & battleID, const BattleSpellCast * cast) override
	{
		events.deliver(battleID.getNum(), "battleSpellCast", [&] { implementation->battleSpellCast(battleID, cast); });
	}

	void battleStacksEffectsSet(const BattleID & battleID, const SetStackEffect & effect) override
	{
		events.deliver(battleID.getNum(), "battleStacksEffectsSet", [&] { implementation->battleStacksEffectsSet(battleID, effect); });
	}

	void battleTriggerEffect(const BattleID & battleID, const BattleTriggerEffect & effect) override
	{
		events.deliver(battleID.getNum(), "battleTriggerEffect", [&] { implementation->battleTriggerEffect(battleID, effect); });
	}

	void battleAnimationPlayed(const BattleID & battleID, const BattleAnimationPlayed & animation) override
	{
		events.deliver(battleID.getNum(), "battleAnimationPlayed", [&] { implementation->battleAnimationPlayed(battleID, animation); });
	}

	void battleStartBefore(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2) override
	{
		events.deliver(battleID.getNum(), "battleStartBefore", [&] { implementation->battleStartBefore(battleID, army1, army2, tile, hero1, hero2); });
	}

	void battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed) override
	{
		events.deliver(battleID.getNum(), "battleStart", [&] { implementation->battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed); });
	}

	void battleUnitsChanged(const BattleID & battleID, const std::vector<UnitChanges> & units) override
	{
		events.deliver(battleID.getNum(), "battleUnitsChanged", [&] { implementation->battleUnitsChanged(battleID, units); });
	}

	void battleObstaclesChanged(const BattleID & battleID, const ObstacleChanges & obstacles) override
	{
		events.deliver(battleID.getNum(), "battleObstaclesChanged", [&] { implementation->battleObstaclesChanged(battleID, obstacles); });
	}

	void battleCatapultAttacked(const BattleID & battleID, const CatapultAttack & attack) override
	{
		events.deliver(battleID.getNum(), "battleCatapultAttacked", [&] { implementation->battleCatapultAttacked(battleID, attack); });
	}

	void battleGateStateChanged(const BattleID & battleID, const EGateState state) override
	{
		events.deliver(battleID.getNum(), "battleGateStateChanged", [&] { implementation->battleGateStateChanged(battleID, state); });
	}

private:
	void syncIdentity()
	{
		implementation->human = human;
		implementation->playerID = playerID;
		implementation->dllName = dllName;
	}
	std::shared_ptr<CBattleGameInterface> implementation;
	EventChannel events;
};
}
