/*
 * HeadlessMatchTrace.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../lib/CConfigHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/army/CStackInstance.h"
#include "../lib/mapping/CMap.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/PacksForClientBattle.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace HeadlessMatchTrace
{
/// Records applied client state without changing AI callbacks or map visibility.
inline void record(const CGameState & state, const CPackForClient & pack)
{
	const char * path = std::getenv("VCMI_AI_MATCH_TRACE");
	if(!path || !*path || !settings["session"]["headless"].Bool())
		return;

	const char * event = nullptr;
#define VCMI_MATCH_EVENT(Type) if(dynamic_cast<const Type *>(&pack)) event = #Type
	VCMI_MATCH_EVENT(NewTurn);
	VCMI_MATCH_EVENT(PlayerStartsTurn);
	VCMI_MATCH_EVENT(PlayerEndsTurn);
	VCMI_MATCH_EVENT(PlayerEndsGame);
	VCMI_MATCH_EVENT(TryMoveHero);
	VCMI_MATCH_EVENT(RemoveObject);
	VCMI_MATCH_EVENT(NewStructures);
	VCMI_MATCH_EVENT(SetAvailableCreatures);
	VCMI_MATCH_EVENT(SetResources);
	VCMI_MATCH_EVENT(SetStackType);
	VCMI_MATCH_EVENT(ChangeStackCount);
	VCMI_MATCH_EVENT(InsertNewStack);
	VCMI_MATCH_EVENT(BattleStart);
	VCMI_MATCH_EVENT(BattleAttack);
	VCMI_MATCH_EVENT(BattleResultAccepted);
	VCMI_MATCH_EVENT(PackageApplied);
#undef VCMI_MATCH_EVENT
	if(!event)
		return;

	static std::ofstream output(path, std::ios::out | std::ios::trunc);
	static uint64_t sequence = 0;
	if(!output)
		throw std::runtime_error("Cannot write VCMI_AI_MATCH_TRACE");

	output << "{\"seq\":" << sequence++ << ",\"event\":\"" << event
		<< "\",\"day\":" << state.day;
	if(const auto * value = dynamic_cast<const PlayerStartsTurn *>(&pack))
		output << ",\"player\":" << static_cast<int>(value->player.getNum());
	if(const auto * value = dynamic_cast<const PlayerEndsTurn *>(&pack))
		output << ",\"player\":" << static_cast<int>(value->player.getNum());
	if(const auto * value = dynamic_cast<const PlayerEndsGame *>(&pack))
		output << ",\"player\":" << static_cast<int>(value->player.getNum())
			<< ",\"victory\":" << (value->victoryLossCheckResult.victory() ? "true" : "false");
	if(const auto * value = dynamic_cast<const RemoveObject *>(&pack))
		output << ",\"object\":" << value->objectID.getNum();
	if(const auto * value = dynamic_cast<const NewStructures *>(&pack))
		output << ",\"town\":" << value->tid.getNum();
	if(const auto * value = dynamic_cast<const SetAvailableCreatures *>(&pack))
		output << ",\"town\":" << value->tid.getNum();
	if(const auto * value = dynamic_cast<const BattleStart *>(&pack))
	{
		const auto * battle = state.getBattle(value->battleID);
		output << ",\"battle\":" << value->battleID.getNum();
		if(battle)
			output << ",\"defender\":" << battle->getSideArmy(BattleSide::DEFENDER)->id.getNum();
	}
	if(const auto * value = dynamic_cast<const BattleAttack *>(&pack))
	{
		int64_t damage = 0;
		for(const auto & attack : value->bsa)
			damage += attack.damageAmount;
		output << ",\"battle\":" << value->battleID.getNum() << ",\"damage\":" << damage;
	}
	if(const auto * value = dynamic_cast<const BattleResultAccepted *>(&pack))
		output << ",\"battle\":" << value->battleID.getNum();
	if(const auto * value = dynamic_cast<const PackageApplied *>(&pack))
		output << ",\"request\":" << value->requestID
			<< ",\"accepted\":" << (value->result ? "true" : "false");

	output << ",\"objects\":[";
	bool first = true;
	for(const auto & object : state.getMap().objects)
	{
		if(!object)
			continue;
		const char * kind = "other";
		if(object->ID == Obj::TOWN) kind = "town";
		if(object->ID == Obj::HERO) kind = "hero";
		if(object->ID == Obj::RESOURCE) kind = "resource";
		if(object->ID == Obj::ARTIFACT) kind = "artifact";
		if(object->ID == Obj::MONSTER) kind = "monster";
		if(!first)
			output << ',';
		first = false;
		const auto pos = object->visitablePos();
		output << "{\"id\":" << object->id.getNum() << ",\"kind\":\"" << kind
			<< "\",\"owner\":" << static_cast<int>(object->getOwner().getNum())
			<< ",\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << pos.z;
		if(const auto * army = dynamic_cast<const CArmedInstance *>(object.get()))
		{
			int64_t count = 0;
			for(const auto & slot : army->Slots())
				count += slot.second->getCount();
			output << ",\"troops\":" << count;
		}
		if(const auto * town = dynamic_cast<const CGTownInstance *>(object.get()))
		{
			uint64_t stock = 0;
			for(const auto & dwelling : town->creatures)
				stock += dwelling.first;
			output << ",\"buildings\":" << town->builtBuildings.size() << ",\"stock\":" << stock;
		}
		output << '}';
	}
	output << "]}\n";
	output.flush();
	if(!output)
		throw std::runtime_error("Failed to flush VCMI_AI_MATCH_TRACE");
}
}
