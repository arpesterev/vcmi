/*
 * SpellTargetsEvaluator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include <vcmi/spells/Spell.h>
#include <array>

using namespace spells;

std::vector<Target> SpellTargetEvaluator::getViableTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::vector<AimType> targetTypes = spellMechanics->getTargetTypes();
	if(targetTypes.empty())
		return result;
	if(targetTypes.size() == 2 && targetTypes[0] == AimType::CREATURE
		&& targetTypes[1] == AimType::LOCATION && spellMechanics->getSpellId() == SpellID::TELEPORT)
		return shortlistTeleportTargets(spellMechanics, allTargetCombinations(spellMechanics, targetTypes));
	if(targetTypes.size() > 1 || targetTypes.front() == AimType::OBSTACLE)
		return allTargetCombinations(spellMechanics, targetTypes);

	auto targetType = targetTypes.front();

	switch(targetType)
	{
		case AimType::CREATURE:
			return allTargetableCreatures(spellMechanics);
		case AimType::LOCATION:
		{
			// Real mechanics expose obstacle spells as LOCATION targets. Preserve
			// the declared type so isolated obstacles are not missed by the
			// near-unit heuristic, without expanding ordinary location spells.
			const auto * spell = dynamic_cast<const CSpell *>(spellMechanics->getSpell());
			if(spell && spell->getTargetType() == AimType::OBSTACLE)
				return allTargetCombinations(spellMechanics, targetTypes);
			if(spellMechanics->isNeutralSpell())
				return defaultLocationSpellHeuristics(
					spellMechanics
				); // theoretically anything can be a useful destination, so we balance performance and validity
			else
				return theBestLocationCasts(spellMechanics);
		}
		case AimType::NO_TARGET:
			return std::vector<Target>(1); //default-constructed target means cast without destination
		default:
			return result;
	}
}

std::vector<Target> SpellTargetEvaluator::shortlistTeleportTargets(const Mechanics * mechanics, std::vector<Target> legalTargets)
{
	// Engineering work budget, not a claim of optimality or a wall-clock bound.
	// Keep the complete cheap legality scan, but cap expensive post-cast searches.
	constexpr size_t MAX_TARGETS = 8;
	constexpr size_t CATEGORY_QUOTA = 2;
	const auto * battle = mechanics->battle();
	const auto units = battle->battleAliveUnits();
	const auto * active = battle->battleActiveUnit();
	const auto casterColor = mechanics->getCasterColor();

	struct Candidate
	{
		Target target;
		uint32_t unitId;
		BattleHex destination;
		bool friendly = false;
		bool activeAttack = false;
		bool otherAttack = false;
		bool shooterEscape = false;
		bool attacksShooter = false;
		uint32_t defenderId = 0;
		int64_t defenderHealth = 0;
		int64_t sourceHealth = 0;
		int64_t margin = 0;
		int64_t marginGain = 0;
	};
	std::vector<Candidate> candidates;
	std::set<std::pair<uint32_t, int>> unique;
	for(auto & target : legalTargets)
	{
		if(target.size() != 2)
			continue;
		const auto * unit = battle->battleGetUnitByPos(target.front().hexValue);
		const auto destination = target.back().hexValue;
		if(!unit || !unit->alive() || !destination.isValid() || destination == unit->getPosition())
			continue; // No-op teleport is represented by the unchanged no-spell baseline.
		if(!unique.emplace(unit->unitId(), destination.toInt()).second)
			continue;
		Candidate candidate;
		candidate.target = std::move(target);
		candidate.unitId = unit->unitId();
		candidate.destination = destination;
		candidate.friendly = battle->battleGetOwner(unit) == casterColor;
		candidate.sourceHealth = unit->getAvailableHealth();
		int64_t currentMargin = GameConstants::BFIELD_SIZE;
		candidate.margin = GameConstants::BFIELD_SIZE;
		bool blockedAfter = false;
		const bool berserk = unit->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE);
		const auto & surrounding = unit->getSurroundingHexes(destination);
		for(const auto * other : units)
		{
			if(other->unitId() == unit->unitId() || !other->getPosition().isValid())
				continue;
			// Match the engine's blocking convention, including both occupied hexes.
			if(berserk || other->unitOwner() != unit->unitOwner())
				for(const auto & occupied : other->getHexes())
					blockedAfter = blockedAfter || surrounding.contains(occupied);

			if(battle->battleGetOwner(other) != battle->battleGetOwner(unit))
			{
				int currentDistance = GameConstants::BFIELD_SIZE;
				int newDistance = GameConstants::BFIELD_SIZE;
				for(const auto & enemyHex : other->getHexes())
				{
					for(const auto & ownHex : unit->getHexes())
						currentDistance = std::min(currentDistance, static_cast<int>(BattleHex::getDistance(ownHex, enemyHex)));
					for(const auto & ownHex : unit->getHexes(destination))
						newDistance = std::min(newDistance, static_cast<int>(BattleHex::getDistance(ownHex, enemyHex)));
				}
				// Geometric priority only: this is not a reachability/safety verdict.
				const auto speed = static_cast<int64_t>(other->getMovementRange());
				currentMargin = std::min(currentMargin, currentDistance - speed - 1);
				candidate.margin = std::min(candidate.margin, newDistance - speed - 1);
			}
			if(candidate.friendly && unit->willMove() && battle->battleCanAttackUnit(unit, other)
				&& CStack::isMeleeAttackPossible(unit, other, destination))
			{
				const bool shooter = other->canShoot();
				const auto health = other->getAvailableHealth();
				const bool hadAttack = candidate.activeAttack || candidate.otherAttack;
				if(!hadAttack || shooter > candidate.attacksShooter
					|| (shooter == candidate.attacksShooter && (health > candidate.defenderHealth
						|| (health == candidate.defenderHealth && other->unitId() < candidate.defenderId))))
				{
					candidate.attacksShooter = shooter;
					candidate.defenderHealth = health;
					candidate.defenderId = other->unitId();
				}
				candidate.activeAttack = active && active->unitId() == unit->unitId();
				candidate.otherAttack = !candidate.activeAttack;
			}
		}
		candidate.marginGain = candidate.margin - currentMargin;
		candidate.shooterEscape = candidate.friendly && unit->canShoot() && !unit->canShootBlocked()
			&& battle->battleIsUnitBlocked(unit) && !blockedAfter;
		candidates.push_back(std::move(candidate));
	}

	std::array<std::vector<size_t>, 4> categories;
	std::vector<size_t> remaining;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const auto & candidate = candidates[index];
		if(candidate.activeAttack) categories[0].push_back(index);
		if(candidate.otherAttack) categories[1].push_back(index);
		if(candidate.shooterEscape) categories[2].push_back(index);
		if(candidate.friendly && candidate.marginGain > 0) categories[3].push_back(index);
		remaining.push_back(index);
	}
	auto better = [&](size_t left, size_t right, bool attack)
	{
		const auto & a = candidates[left];
		const auto & b = candidates[right];
		if(a.friendly != b.friendly) return a.friendly;
		if(attack && a.attacksShooter != b.attacksShooter) return a.attacksShooter;
		if(attack && a.defenderHealth != b.defenderHealth) return a.defenderHealth > b.defenderHealth;
		if(!attack && a.marginGain != b.marginGain) return a.marginGain > b.marginGain;
		if(a.sourceHealth != b.sourceHealth) return a.sourceHealth > b.sourceHealth;
		if(a.margin != b.margin) return a.margin > b.margin;
		if(a.unitId != b.unitId) return a.unitId < b.unitId;
		return a.destination.toInt() < b.destination.toInt();
	};
	for(size_t category = 0; category < categories.size(); ++category)
		std::sort(categories[category].begin(), categories[category].end(),
			[&](size_t a, size_t b) { return better(a, b, category < 2); });
	std::sort(remaining.begin(), remaining.end(), [&](size_t a, size_t b) { return better(a, b, false); });

	std::vector<Target> result;
	std::set<size_t> selected;
	std::array<std::set<uint32_t>, 4> selectedSources;
	std::array<std::set<uint32_t>, 2> selectedDefenders;
	auto selectCategory = [&](size_t category)
	{
		// Prefer another source/defender before spending both slots on one stack.
		for(int relaxation = 0; relaxation < 3; ++relaxation)
			for(const auto index : categories[category])
			{
				const auto & candidate = candidates[index];
				if(selected.count(index)) continue;
				if(relaxation == 0 && selectedSources[category].count(candidate.unitId)) continue;
				if(relaxation < 2 && category < 2 && selectedDefenders[category].count(candidate.defenderId)) continue;
				selected.insert(index);
				selectedSources[category].insert(candidate.unitId);
				if(category < 2) selectedDefenders[category].insert(candidate.defenderId);
				result.push_back(candidate.target);
				return true;
			}
		return false;
	};
	for(size_t category = 0; category < categories.size(); ++category)
		for(size_t count = 0; count < CATEGORY_QUOTA && result.size() < MAX_TARGETS; ++count)
			if(!selectCategory(category)) break;
	while(result.size() < MAX_TARGETS)
	{
		bool added = false;
		for(size_t category = 0; category < categories.size() && result.size() < MAX_TARGETS; ++category)
			added = selectCategory(category) || added;
		if(!added) break;
	}
	// If no category covers a legal repositioning, still permit the full scorer
	// to compare it with not casting. Prefer source diversity before duplicates.
	std::set<uint32_t> represented;
	for(const auto index : selected) represented.insert(candidates[index].unitId);
	for(int relaxation = 0; relaxation < 2 && result.size() < MAX_TARGETS; ++relaxation)
		for(const auto index : remaining)
		{
			if(result.size() == MAX_TARGETS) break;
			if(selected.count(index) || (relaxation == 0 && represented.count(candidates[index].unitId))) continue;
			selected.insert(index);
			represented.insert(candidates[index].unitId);
			result.push_back(candidates[index].target);
		}
	return result;
}

std::vector<Target> SpellTargetEvaluator::allTargetCombinations(const Mechanics * mechanics, const std::vector<AimType> & types)
{
	std::vector<std::vector<Destination>> destinations;
	for(const auto type : types)
	{
		std::vector<Destination> choices;
		if(type == AimType::CREATURE)
		{
			for(const auto * unit : mechanics->battle()->battleGetAllUnits(false))
				choices.emplace_back(unit->getPosition());
		}
		else if(type == AimType::LOCATION || type == AimType::OBSTACLE)
		{
			// Let the spell mechanics decide which tiles/obstacles are legal. In
			// particular, don't filter a teleport destination without its unit.
			for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
				choices.emplace_back(BattleHex(hex));
		}
		else if(type == AimType::NO_TARGET)
			choices.emplace_back(BattleHex::INVALID);
		else
			return {};
		destinations.push_back(std::move(choices));
	}

	std::vector<Target> result;
	Target candidate;
	std::function<void(size_t)> visit = [&](size_t index)
	{
		if(index == destinations.size())
		{
			detail::ProblemImpl ignored;
			if(mechanics->canBeCastAt(candidate, ignored))
				result.push_back(candidate);
			return;
		}
		for(const auto & destination : destinations[index])
		{
			candidate.push_back(destination);
			visit(index + 1);
			candidate.pop_back();
		}
	};
	visit(0);
	return result;
}

std::vector<Target> SpellTargetEvaluator::defaultLocationSpellHeuristics(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result = allTargetableCreatures(spellMechanics);
	auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units) //insert a random surrounding hex
	{
		auto surroundingHexes = unit->getSurroundingHexes();
		if(!surroundingHexes.empty())
		{
			auto randomSurroundingHex = *RandomGeneratorUtil::nextItem(surroundingHexes, CRandomGenerator::getDefault()); // don't think this method bias matter with such small numbers
			addIfCanBeCast(spellMechanics, randomSurroundingHex, result);
		}
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::allTargetableCreatures(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units)
		addIfCanBeCast(spellMechanics, unit->getPosition(), result);
	return result;
}

std::vector<Target> SpellTargetEvaluator::theBestLocationCasts(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::map<BattleHex, std::set<const CStack *>> allCasts;
	std::map<BattleHex, std::set<const CStack *>> bestCasts;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; i++)
	{
		BattleHex dest(i);
		if(canBeCastAt(spellMechanics, dest))
		{
			Target target;
			target.emplace_back(dest);
			auto temp = spellMechanics->getAffectedStacks(target);
			std::set<const CStack *> affectedStacks(temp.begin(), temp.end());
			allCasts[dest] = affectedStacks;
		}
	}

	for(const auto & cast : allCasts)
	{
		std::set<BattleHex> worseCasts;
		if(isCastHarmful(spellMechanics, cast.second))
			continue;

		bool isBestCast = true;
		for(const auto & bestCast : bestCasts)
		{
			Compare compare = compareAffectedStacks(spellMechanics, cast.second, bestCast.second);

			if(compare == Compare::WORSE || compare == Compare::EQUAL)
			{
				isBestCast = false;
				break;
			}

			if(compare == Compare::BETTER)
			{
				worseCasts.insert(bestCast.first);
			}
		}

		if(isBestCast)
		{
			bestCasts.insert(cast);
			for(BattleHex worseCast : worseCasts)
				bestCasts.erase(worseCast);
		}
	}

	for(const auto & cast : bestCasts)
	{
		Destination des(cast.first);
		result.push_back({des});
	}
	return result;
}

bool SpellTargetEvaluator::isCastHarmful(const spells::Mechanics * spellMechanics, const std::set<const CStack *> & affectedStacks)
{

	bool isAffectedAlly = false;
	bool isAffectedEnemy = false;

	for(const CStack * affectedUnit : affectedStacks)
	{
		if(affectedUnit->unitSide() == spellMechanics->casterSide)
			isAffectedAlly = true;
		else
			isAffectedEnemy = true;
	}

	return (spellMechanics->isPositiveSpell() && !isAffectedAlly) || (spellMechanics->isNegativeSpell() && !isAffectedEnemy);
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacks(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newCast, const std::set<const CStack *> & oldCast)
{
	if(newCast.size() == oldCast.size())
		return newCast == oldCast ? Compare::EQUAL : Compare::DIFFERENT;

	auto getAlliedUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> alliedUnits;
		for(auto stack : allUnits)
		{
			if(stack->unitSide() == spellMechanics->casterSide)
				alliedUnits.insert(stack);
		}
		return alliedUnits;
	};

	auto getEnemyUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> enemyUnits;
		for(auto stack : allUnits)
		{
			if(stack->unitSide() != spellMechanics->casterSide)
				enemyUnits.insert(stack);
		}
		return enemyUnits;
	};

	Compare alliedSubsetComparison = compareAffectedStacksSubset(spellMechanics, getAlliedUnits(newCast), getAlliedUnits(oldCast));
	Compare enemySubsetComparison = compareAffectedStacksSubset(spellMechanics, getEnemyUnits(newCast), getEnemyUnits(oldCast));

	if(spellMechanics->isPositiveSpell())
		enemySubsetComparison = reverse(enemySubsetComparison);
	else if(spellMechanics->isNegativeSpell())
		alliedSubsetComparison = reverse(alliedSubsetComparison);

	std::set<Compare> comparisonResults = {alliedSubsetComparison, enemySubsetComparison};
	std::set<std::set<Compare>> possibleBetterResults = {
		{Compare::BETTER, Compare::BETTER},
        {Compare::BETTER, Compare::EQUAL }
	};
	std::set<std::set<Compare>> possibleWorstResults = {
		{Compare::WORSE, Compare::WORSE},
        {Compare::WORSE, Compare::EQUAL}
	};

	if(possibleBetterResults.find(comparisonResults) != possibleBetterResults.end())
		return Compare::BETTER;
	if(possibleWorstResults.find(comparisonResults) != possibleWorstResults.end())
		return Compare::WORSE;

	return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacksSubset(
    const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newSubset, const std::set<const CStack *> & oldSubset)
{
	if(newSubset.size() == oldSubset.size())
		return newSubset == oldSubset ? Compare::EQUAL : Compare::DIFFERENT;

	if(oldSubset.size() > newSubset.size())
		return reverse(compareAffectedStacksSubset(spellMechanics, oldSubset, newSubset));

	const std::set<const CStack *> & biggerSet = newSubset;
	const std::set<const CStack *> & smallerSet = oldSubset;

	if(std::includes(biggerSet.begin(), biggerSet.end(), smallerSet.begin(), smallerSet.end()))
		return Compare::BETTER;
	else
		return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::reverse(SpellTargetEvaluator::Compare compare)
{
	switch(compare)
	{
		case Compare::BETTER:
			return Compare::WORSE;
		case Compare::WORSE:
			return Compare::BETTER;
		default:
			return compare;
	}
}

bool SpellTargetEvaluator::canBeCastAt(const spells::Mechanics * spellMechanics, BattleHex hex)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	return spellMechanics->canBeCastAt({des}, ignored);
}

void SpellTargetEvaluator::addIfCanBeCast(const spells::Mechanics * spellMechanics, BattleHex hex, std::vector<Target> & targets)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	if(spellMechanics->canBeCastAt({des}, ignored))
		targets.push_back({des});
}
