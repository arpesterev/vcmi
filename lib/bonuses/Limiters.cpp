/*
 * Limiters.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Limiters.h"
#include "BonusLimiterContext.h"
#include "Updaters.h"

#include "../CBonusTypeHandler.h"
#include "../GameLibrary.h"
#include "../entities/faction/CTownHandler.h"
#include "../CCreatureHandler.h"
#include "../CStack.h"
#include "../TerrainHandler.h"
#include "../constants/StringConstants.h"
#include "../battle/BattleInfo.h"
#include "../json/JsonUtils.h"

const std::map<std::string, TLimiterPtr> bonusLimiterMap =
{
	{"SHOOTER_ONLY", std::make_shared<HasAnotherBonusLimiter>(BonusType::SHOOTER)},
	{"DRAGON_NATURE", std::make_shared<HasAnotherBonusLimiter>(BonusType::DRAGON_NATURE)},
	{"IS_UNDEAD", std::make_shared<HasAnotherBonusLimiter>(BonusType::UNDEAD)},
	{"UNIT_DEFENDING", std::make_shared<HasAnotherBonusLimiter>(BonusType::UNIT_DEFENDING)},
	{"CREATURE_NATIVE_TERRAIN", std::make_shared<TerrainLimiter>()},
	{"CREATURES_ONLY", std::make_shared<CreatureLevelLimiter>()},
	{"OPPOSITE_SIDE", std::make_shared<OppositeSideLimiter>()},
};

static ILimiter::EDecision limitTyped(const ILimiter & self, const BonusLimiterContext & context)
{
	return ILimiter::EDecision::ACCEPT;
}

static ILimiter::EDecision limitTyped(const CCreatureTypeLimiter & self, const BonusLimiterContext & context)
{
	if(!context.hasCreature())
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept =  context.creatureID() == self.creatureID || (self.includeUpgrades && context.creatureUpgradeOf(self.creatureID));
	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	//drop bonus if it's not our creature and (we don`t check upgrades or its not our upgrade)
}

static ILimiter::EDecision limitTyped(const HasAnotherBonusLimiter & self, const BonusLimiterContext & context)
{
	BonusLimiterQuery query;
	if(self.type != BonusType::NONE) query.type = self.type;
	if(self.isSubtypeRelevant) query.subtype = self.subtype;
	if(self.isSourceRelevant) query.source = self.source;
	if(self.isSourceRelevant && self.isSourceIDRelevant) query.sourceID = self.sid;

	//if we have a bonus of required self.type accepted, limiter should accept also this bonus
	if(context.accepted(query))
	{
		if ( self.minValue == std::numeric_limits<int32_t>::min() &&
			 self.maxValue == std::numeric_limits<int32_t>::max())
			return ILimiter::EDecision::ACCEPT;

		// can't determine final bonus value yet
		if(context.undecided(query))
			return ILimiter::EDecision::NOT_SURE;

		int bonusValue = context.acceptedValue(query);

		if (bonusValue >= self.minValue && bonusValue <= self.maxValue)
			return ILimiter::EDecision::ACCEPT;
		else
			return ILimiter::EDecision::DISCARD;
	}

	//if there are no matching bonuses pending, we can (and must) reject right away
	if(!context.undecided(query))
		return ILimiter::EDecision::DISCARD;

	//do not accept for now but it may change if more bonuses gets included
	return ILimiter::EDecision::NOT_SURE;
}

static ILimiter::EDecision limitTyped(const UnitOnHexLimiter & self, const BonusLimiterContext & context)
{
	if(!context.hasBattleStack())
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept = false;

	for (const auto & hex : context.hexes())
		accept |= self.applicableHexes.contains(hex);

	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
}

static ILimiter::EDecision limitTyped(const UnitAdjacentLimiter & self, const BonusLimiterContext & context)
{
	if(!context.hasBattleStack())
		return ILimiter::EDecision::NOT_APPLICABLE;

	if (!context.validPosition())
		return ILimiter::EDecision::DISCARD;

	return context.adjacentTo(self.targetUnit) ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
}

static ILimiter::EDecision limitTyped(const TerrainLimiter & self, const BonusLimiterContext & context)
{
	BonusNodeType nodeType = context.nodeType();
	std::set<BonusNodeType> allowedNodes = {BonusNodeType::STACK_BATTLE, BonusNodeType::STACK_INSTANCE, BonusNodeType::HERO, BonusNodeType::TOWN, BonusNodeType::TOWN_AND_VISITOR};
	if (!allowedNodes.contains(nodeType))
		return ILimiter::EDecision::NOT_APPLICABLE;

	ETerrainId currentTerrain = context.currentTerrain();

	if (self.terrainType == ETerrainId::NATIVE_TERRAIN)
	{
		BonusLimiterQuery query;
		query.type = BonusType::TERRAIN_NATIVE;

		if(context.accepted(query))
			return ILimiter::EDecision::ACCEPT;

		if(context.undecided(query))
			return ILimiter::EDecision::NOT_SURE;

		return context.nativeTerrain(currentTerrain) ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	}

	return currentTerrain == self.terrainType ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
}

static ILimiter::EDecision limitTyped(const FactionLimiter & self, const BonusLimiterContext & context)
{
	if(context.hasFaction())
	{
		if(self.faction != FactionID::DEFAULT)
			return context.faction() == self.faction ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

		switch(context.bonusSource())
		{
			case BonusSource::CREATURE_ABILITY:
				return context.faction() == context.sourceCreatureFaction() ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

			case BonusSource::TOWN_STRUCTURE:
				return context.faction() == context.bonusSourceID().as<BuildingTypeUniqueID>().getFaction() ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

			//TODO: other sources of bonuses
		}
	}
	return ILimiter::EDecision::NOT_APPLICABLE; //Discard by default
}

static ILimiter::EDecision limitTyped(const CreatureLevelLimiter & self, const BonusLimiterContext & context)
{
	if (!context.hasCreature())
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept = context.creatureLevel() < self.maxLevel && context.creatureLevel() >= self.minLevel;
	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD; //drop bonus for non-creatures or non-native residents
}

static ILimiter::EDecision limitTyped(const CreatureAlignmentLimiter & self, const BonusLimiterContext & context)
{
	if(context.hasCreature()) {
		if(self.alignment == context.creatureAlignment())
			return ILimiter::EDecision::ACCEPT;
		return ILimiter::EDecision::DISCARD;
	}

	return ILimiter::EDecision::NOT_APPLICABLE;
}

static ILimiter::EDecision limitTyped(const RankRangeLimiter & self, const BonusLimiterContext & context)
{
	if(context.hasStackInstance())
	{
		if (context.isCommander()) //no stack exp bonuses for commander creatures
			return ILimiter::EDecision::DISCARD;
		if (context.rank() >= self.minRank && context.rank() < self.maxRank)
			return ILimiter::EDecision::ACCEPT;

		return ILimiter::EDecision::DISCARD;
	}
	return ILimiter::EDecision::NOT_APPLICABLE;
}

static ILimiter::EDecision limitTyped(const OppositeSideLimiter & self, const BonusLimiterContext & context)
{
	PlayerColor contextOwner = context.owner();
	PlayerColor bonusOwner = context.bonusOwner();
	if (contextOwner == PlayerColor::UNFLAGGABLE)
		contextOwner = PlayerColor::NEUTRAL;
	auto decision = (bonusOwner == contextOwner || bonusOwner == PlayerColor::CANNOT_DETERMINE) ? ILimiter::EDecision::DISCARD : ILimiter::EDecision::ACCEPT;
	return decision;
}

static ILimiter::EDecision limitTyped(const AllOfLimiter & self, const BonusLimiterContext & context, const std::function<ILimiter::EDecision(const ILimiter &, const BonusLimiterContext &)> & childLimit)
{
	bool wasntSure = false;

	for(const auto & limiter : self.limiters)
	{
		auto result = childLimit(*limiter, context);
		if(result == ILimiter::EDecision::DISCARD || result == ILimiter::EDecision::NOT_APPLICABLE)
			return result;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::ACCEPT;
}

static ILimiter::EDecision limitTyped(const AnyOfLimiter & self, const BonusLimiterContext & context, const std::function<ILimiter::EDecision(const ILimiter &, const BonusLimiterContext &)> & childLimit)
{
	bool wasntSure = false;

	for(const auto & limiter : self.limiters)
	{
		auto result = childLimit(*limiter, context);
		if(result == ILimiter::EDecision::ACCEPT)
			return result;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::DISCARD;
}

static ILimiter::EDecision limitTyped(const NoneOfLimiter & self, const BonusLimiterContext & context, const std::function<ILimiter::EDecision(const ILimiter &, const BonusLimiterContext &)> & childLimit)
{
	bool wasntSure = false;

	for(const auto & limiter : self.limiters)
	{
		auto result = childLimit(*limiter, context);
		if(result == ILimiter::EDecision::NOT_APPLICABLE)
			return ILimiter::EDecision::NOT_APPLICABLE;
		if(result == ILimiter::EDecision::ACCEPT)
			return ILimiter::EDecision::DISCARD;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::ACCEPT;
}

static ILimiter::EDecision limitTyped(const HasChargesLimiter & self, const BonusLimiterContext & context)
{
 BonusLimiterQuery query;
 query.type = BonusType::ARTIFACT_CHARGE;
 query.sourceID = context.bonusSourceID();
 if(context.undecided(query)) return ILimiter::EDecision::NOT_SURE;
 if(auto value = context.firstAcceptedValue(query))
  return *value >= self.chargeCost ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
 return ILimiter::EDecision::DISCARD;
}

ILimiter::EDecision ILimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string ILimiter::toString() const
{
	return typeid(*this).name();
}

JsonNode ILimiter::toJsonNode() const
{
	JsonNode root;
	root["type"].String() = toString();
	return root;
}

ILimiter::EDecision CCreatureTypeLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

CCreatureTypeLimiter::CCreatureTypeLimiter(const CCreature & creature_, bool IncludeUpgrades)
	: creatureID(creature_.getId()), includeUpgrades(IncludeUpgrades)
{
}

void CCreatureTypeLimiter::setCreature(const CreatureID & id)
{
	creatureID = id;
}

std::string CCreatureTypeLimiter::toString() const
{
	boost::format fmt("CCreatureTypeLimiter(creature=%s, includeUpgrades=%s)");
	fmt % creatureID.toEntity(LIBRARY)->getJsonKey() % (includeUpgrades ? "true" : "false");
	return fmt.str();
}

JsonNode CCreatureTypeLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_TYPE_LIMITER";
	root["parameters"].Vector().emplace_back(creatureID.toEntity(LIBRARY)->getJsonKey());
	root["parameters"].Vector().emplace_back(includeUpgrades);

	return root;
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus )
	: type(bonus), isSubtypeRelevant(false), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus, BonusSubtypeID _subtype )
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSource src)
	: type(bonus), source(src), isSubtypeRelevant(false), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSubtypeID _subtype, BonusSource src)
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), source(src), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

ILimiter::EDecision HasAnotherBonusLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string HasAnotherBonusLimiter::toString() const
{
	std::string typeName = LIBRARY->bth->bonusToString(type);
	if(isSubtypeRelevant)
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s, subtype=%s)");
		fmt % typeName % subtype.toString();
		return fmt.str();
	}
	else
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s)");
		fmt % typeName;
		return fmt.str();
	}
}

JsonNode HasAnotherBonusLimiter::toJsonNode() const
{
	JsonNode root;
	std::string typeName = LIBRARY->bth->bonusToString(type);
	auto sourceTypeName = vstd::findKey(bonusSourceMap, source);

	root["type"].String() = "HAS_ANOTHER_BONUS_LIMITER";
	root["parameters"].Vector().emplace_back(typeName);
	if(isSubtypeRelevant)
		root["parameters"].Vector().emplace_back(subtype.toString());
	if(isSourceRelevant)
		root["parameters"].Vector().emplace_back(sourceTypeName);

	return root;
}

ILimiter::EDecision UnitOnHexLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

UnitOnHexLimiter::UnitOnHexLimiter(const BattleHexArray & applicableHexes):
	applicableHexes(applicableHexes)
{
}

JsonNode UnitOnHexLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "UNIT_ON_HEXES";
	for(const auto & hex : applicableHexes)
		root["parameters"].Vector().emplace_back(hex.toInt());

	return root;
}

ILimiter::EDecision UnitAdjacentLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

JsonNode UnitAdjacentLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "UNIT_ADJACENT";
	root["creature"].String() = targetUnit.toEntity(LIBRARY)->getJsonKey();

	return root;
}

TerrainLimiter::TerrainLimiter()
	: terrainType(ETerrainId::NATIVE_TERRAIN)
{
}

TerrainLimiter::TerrainLimiter(TerrainId terrain):
	terrainType(terrain)
{
}

ILimiter::EDecision TerrainLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string TerrainLimiter::toString() const
{
	boost::format fmt("CreatureTerrainLimiter(terrainType=%s)");
	auto terrainName = LIBRARY->terrainTypeHandler->getById(terrainType)->getJsonKey();
	fmt % (terrainType == ETerrainId::NATIVE_TERRAIN ? "native" : terrainName);
	return fmt.str();
}

JsonNode TerrainLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_TERRAIN_LIMITER";
	auto terrainName = LIBRARY->terrainTypeHandler->getById(terrainType)->getJsonKey();
	root["parameters"].Vector().emplace_back(terrainName);

	return root;
}

FactionLimiter::FactionLimiter(FactionID creatureFaction)
	: faction(creatureFaction)
{
}

ILimiter::EDecision FactionLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string FactionLimiter::toString() const
{
	boost::format fmt("FactionLimiter(faction=%s)");
	fmt % LIBRARY->factions()->getById(faction)->getJsonKey();
	return fmt.str();
}

JsonNode FactionLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "FACTION_LIMITER";
	root["parameters"].Vector().emplace_back(LIBRARY->factions()->getById(faction)->getJsonKey());

	return root;
}

CreatureLevelLimiter::CreatureLevelLimiter(uint32_t minLevel, uint32_t maxLevel) :
	minLevel(minLevel),
	maxLevel(maxLevel)
{
}

ILimiter::EDecision CreatureLevelLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string CreatureLevelLimiter::toString() const
{
	boost::format fmt("CreatureLevelLimiter(minLevel=%d,maxLevel=%d)");
	fmt % minLevel % maxLevel;
	return fmt.str();
}

JsonNode CreatureLevelLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_LEVEL_LIMITER";
	root["parameters"].Vector().emplace_back(minLevel);
	root["parameters"].Vector().emplace_back(maxLevel);

	return root;
}

CreatureAlignmentLimiter::CreatureAlignmentLimiter(EAlignment Alignment)
	: alignment(Alignment)
{
}

ILimiter::EDecision CreatureAlignmentLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

std::string CreatureAlignmentLimiter::toString() const
{
	boost::format fmt("CreatureAlignmentLimiter(alignment=%s)");
	fmt % GameConstants::ALIGNMENT_NAMES[vstd::to_underlying(alignment)];
	return fmt.str();
}

JsonNode CreatureAlignmentLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_ALIGNMENT_LIMITER";
	root["parameters"].Vector().emplace_back(GameConstants::ALIGNMENT_NAMES[vstd::to_underlying(alignment)]);

	return root;
}

RankRangeLimiter::RankRangeLimiter(ui8 Min, ui8 Max)
	:minRank(Min), maxRank(Max)
{
}

RankRangeLimiter::RankRangeLimiter()
{
	minRank = maxRank = -1;
}

ILimiter::EDecision RankRangeLimiter::limit(const BonusLimitationContext &context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

JsonNode RankRangeLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "EXPERIENCE_RANK_LIMITER";
	root["minRank"].Integer() = minRank;
	if(maxRank != std::numeric_limits<ui8>::max())
		root["maxRank"].Integer() = maxRank;

	return root;
}

OppositeSideLimiter::OppositeSideLimiter()
{
}

ILimiter::EDecision OppositeSideLimiter::limit(const BonusLimitationContext & context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

// Aggregate/Boolean Limiters

AggregateLimiter::AggregateLimiter(std::vector<TLimiterPtr> limiters):
	limiters(std::move(limiters))
{
}

void AggregateLimiter::add(const TLimiterPtr & limiter)
{
	if(limiter)
		limiters.push_back(limiter);
}

JsonNode AggregateLimiter::toJsonNode() const
{
	JsonNode result;
	result.Vector().emplace_back(getAggregator());
	for(const auto & l : limiters)
		result.Vector().push_back(l->toJsonNode());
	return result;
}

const std::string AllOfLimiter::aggregator = "allOf";
const std::string & AllOfLimiter::getAggregator() const
{
	return aggregator;
}

AllOfLimiter::AllOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision AllOfLimiter::limit(const BonusLimitationContext & context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context), [&context](const ILimiter & child, const BonusLimiterContext &) { return child.limit(context); });
}

const std::string AnyOfLimiter::aggregator = "anyOf";
const std::string & AnyOfLimiter::getAggregator() const
{
	return aggregator;
}

AnyOfLimiter::AnyOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision AnyOfLimiter::limit(const BonusLimitationContext & context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context), [&context](const ILimiter & child, const BonusLimiterContext &) { return child.limit(context); });
}

const std::string NoneOfLimiter::aggregator = "noneOf";
const std::string & NoneOfLimiter::getAggregator() const
{
	return aggregator;
}

NoneOfLimiter::NoneOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision NoneOfLimiter::limit(const BonusLimitationContext & context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context), [&context](const ILimiter & child, const BonusLimiterContext &) { return child.limit(context); });
}

HasChargesLimiter::HasChargesLimiter(const uint16_t cost)
	: chargeCost(cost)
{
}

ILimiter::EDecision HasChargesLimiter::limit(const BonusLimitationContext & context) const
{
	return limitTyped(*this, NativeBonusLimiterContext(context));
}

ILimiter::EDecision limitBonus(const ILimiter & limiter, const BonusLimiterContext & context)
{
	if(typeid(limiter) == typeid(ILimiter)) return limitTyped(static_cast<const ILimiter &>(limiter), context);
	if(typeid(limiter) == typeid(CCreatureTypeLimiter)) return limitTyped(static_cast<const CCreatureTypeLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(HasAnotherBonusLimiter)) return limitTyped(static_cast<const HasAnotherBonusLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(UnitOnHexLimiter)) return limitTyped(static_cast<const UnitOnHexLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(UnitAdjacentLimiter)) return limitTyped(static_cast<const UnitAdjacentLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(TerrainLimiter)) return limitTyped(static_cast<const TerrainLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(FactionLimiter)) return limitTyped(static_cast<const FactionLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(CreatureLevelLimiter)) return limitTyped(static_cast<const CreatureLevelLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(CreatureAlignmentLimiter)) return limitTyped(static_cast<const CreatureAlignmentLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(RankRangeLimiter)) return limitTyped(static_cast<const RankRangeLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(OppositeSideLimiter)) return limitTyped(static_cast<const OppositeSideLimiter &>(limiter), context);
	if(typeid(limiter) == typeid(AllOfLimiter)) return limitTyped(static_cast<const AllOfLimiter &>(limiter), context, limitBonus);
	if(typeid(limiter) == typeid(AnyOfLimiter)) return limitTyped(static_cast<const AnyOfLimiter &>(limiter), context, limitBonus);
	if(typeid(limiter) == typeid(NoneOfLimiter)) return limitTyped(static_cast<const NoneOfLimiter &>(limiter), context, limitBonus);
	if(typeid(limiter) == typeid(HasChargesLimiter)) return limitTyped(static_cast<const HasChargesLimiter &>(limiter), context);
	throw std::logic_error("Unsupported limiter dynamic type in owned evaluation");
}
