/* Source-derived ordinary ground publication, homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21 (CC0-1.0).
 * VCMI adaptation: GPL-2.0-or-later. See test/aiIntegration/homm3Ground for the independent oracle. */
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <queue>

namespace AIIntegration::Ports::HoMM3
{
struct SourceGroundIdentity
{
	int side = 0, index = 0;
	bool operator==(const SourceGroundIdentity &) const = default;
};
struct SourceGroundCell
{
	bool blocked = false;
	std::optional<SourceGroundIdentity> occupant;
};
using SourceGroundMap = std::array<SourceGroundCell, 187>;
struct SourceGroundSearchInput
{
	SourceGroundMap map{};
	SourceGroundIdentity actor;
	int head = -1, speed = 0;
	bool movementRestricted = false;
	// Producer certifies single-hex walking, no siege/moats/stoppers/placement,
	// unchanged ownership and complete static map. Missing facts are not empty space.
	bool domainKnown = true;
};
struct SourceGroundSearchResult
{
	std::array<bool, 187> visited{}, validMove{}, enemyMarked{};
	std::array<uint16_t, 187> cost;
	SourceGroundSearchResult() { cost.fill(UINT16_MAX); }
};
namespace GroundPathDetail
{
inline bool available(int hex) { return hex >= 0 && hex < 187 && hex % 17 != 0 && hex % 17 != 16; }
inline bool identity(SourceGroundIdentity value) { return (value.side == 0 || value.side == 1) && value.index >= 0 && value.index <= 30; }
inline std::array<int, 6> adjacent(int hex)
{
	const bool odd = (hex / 17) % 2 != 0;
	std::array<int, 6> result{hex - (odd ? 17 : 16), hex + 1, hex + (odd ? 17 : 18),
		hex + (odd ? 16 : 17), hex - 1, hex - (odd ? 18 : 17)};
	for(auto & cell : result) if(!available(cell)) cell = -1;
	return result;
}
}
inline std::optional<SourceGroundSearchResult> searchSourceGround(const SourceGroundSearchInput & input)
{
	if(!input.domainKnown || input.speed < 0 || !GroundPathDetail::available(input.head)
		|| !GroundPathDetail::identity(input.actor) || input.map[input.head].blocked
		|| input.map[input.head].occupant != input.actor) return std::nullopt;
	std::array<std::array<bool, 31>, 2> seen{};
	for(int hex = 0; hex < 187; ++hex)
	{
		const auto & cell = input.map[hex];
		if(!cell.occupant) continue;
		const auto id = *cell.occupant;
		if(!GroundPathDetail::available(hex) || !GroundPathDetail::identity(id) || cell.blocked
			|| seen[id.side][id.index]) return std::nullopt;
		seen[id.side][id.index] = true;
	}
	SourceGroundSearchResult result;
	const int limit = input.speed == 0 || input.movementRestricted ? 0 : 127;
	std::queue<int> queue;
	queue.push(input.head);
	result.visited[input.head] = true;
	result.cost[input.head] = 0;
	// Unit-weight edges: first insertion is minimal. At most 165 insertions,
	// so the source's 500-element priority-queue cap cannot bind in this domain.
	while(!queue.empty())
	{
		const int head = queue.front(); queue.pop();
		const int cost = result.cost[head];
		for(const int next : GroundPathDetail::adjacent(head))
		{
			if(next < 0) continue;
			const auto & cell = input.map[next];
			if(cell.blocked || (cell.occupant && *cell.occupant != input.actor))
			{
				if(cell.occupant && cell.occupant->side != input.actor.side)
				{
					if(!result.validMove[next] || result.cost[next] > cost)
						result.cost[next] = static_cast<uint16_t>(cost);
					result.validMove[next] = result.enemyMarked[next] = true;
				}
				continue;
			}
			if(cost == limit || result.visited[next]) continue;
			result.visited[next] = true;
			result.cost[next] = static_cast<uint16_t>(cost + 1);
			queue.push(next);
		}
	}
	for(int hex = 0; hex < 187; ++hex)
		if(result.visited[hex] && result.cost[hex] <= input.speed) result.validMove[hex] = true;
	return result;
}
}
