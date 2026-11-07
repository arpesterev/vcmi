// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "MoveOrder.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace HoMM3Forecast
{
/// An attachment is not a round event. Only a pre-action/own-activation WAIT is a phase witness.
class RoundPhase
{
	std::optional<std::int32_t> round;
	std::optional<bool> second;
public:
	void attach(std::int32_t currentRound)
	{
		round = currentRound;
		second.reset();
	}
	void end()
	{
		round.reset();
		second.reset();
	}
	void newRound(std::int32_t currentRound)
	{
		if(round && currentRound <= *round)
			throw std::logic_error("HoMM3 lifecycle: repeated or reversed round event");
		round = currentRound;
		second = false;
	}
	void beforeUnitActionOrOwnActivation(std::int32_t currentRound, bool waitingPhase)
	{
		if(!round || currentRound != *round)
		{
			round = currentRound;
			second.reset();
		}
		if(waitingPhase)
			second = true;
	}
	std::optional<bool> secondPhase(std::int32_t currentRound) const
	{
		return round && *round == currentRound ? second : std::nullopt;
	}
};

struct InitialIdentity
{
	UnitId id;
	Side owningSide;
	std::int32_t armySlot;
	bool operator==(const InitialIdentity &) const = default;
};

/// Retained dead/removed identities occupy their original source slot. Created units are excluded.
class InitialRoster
{
	std::optional<std::vector<InitialIdentity>> roster;
	static std::vector<InitialIdentity> canonical(std::vector<InitialIdentity> input)
	{
		for(const auto & unit : input)
			if(unit.armySlot < 0 || unit.armySlot >= 7
				|| (unit.owningSide != Side::ATTACKER && unit.owningSide != Side::DEFENDER))
				throw std::invalid_argument("HoMM3 lifecycle: unsupported initial identity");
		std::sort(input.begin(), input.end(), [](const auto & lhs, const auto & rhs)
		{
			if(lhs.owningSide != rhs.owningSide) return lhs.owningSide < rhs.owningSide;
			return lhs.armySlot < rhs.armySlot;
		});
		for(std::size_t i = 0; i < input.size(); ++i)
			for(std::size_t j = 0; j < i; ++j)
				if(input[i].id == input[j].id || (input[i].owningSide == input[j].owningSide
					&& input[i].armySlot == input[j].armySlot))
					throw std::invalid_argument("HoMM3 lifecycle: duplicate initial identity");
		return input;
	}
public:
	void bind(std::vector<InitialIdentity> input) { roster = canonical(std::move(input)); }
	void validate(std::vector<InitialIdentity> input) const
	{
		if(!roster || canonical(std::move(input)) != *roster)
			throw std::invalid_argument("HoMM3 lifecycle: initial roster changed or is missing");
	}
	std::int32_t sourceIndex(UnitId id) const
	{
		if(!roster) throw std::logic_error("HoMM3 lifecycle: initial roster is missing");
		std::int32_t index = 0;
		std::optional<Side> side;
		for(const auto & unit : *roster)
		{
			if(side != unit.owningSide) { side = unit.owningSide; index = 0; }
			if(unit.id == id) return index;
			++index;
		}
		throw std::invalid_argument("HoMM3 lifecycle: unknown unit identity");
	}
};
}
