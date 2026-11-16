/*
 * BonusLimiterContext.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "BonusLimiterContext.h"
#include "../CCreatureHandler.h"
#include "../CStack.h"
#include "../GameLibrary.h"
#include "../entities/faction/CTownHandler.h"
#include "../battle/BattleInfo.h"

static const CStack * retrieveStackBattle(const CBonusSystemNode * node)
{
	switch(node->getNodeType())
	{
	case BonusNodeType::STACK_BATTLE:
		return dynamic_cast<const CStack *>(node);
	default:
		return nullptr;
	}
}

static const CStackInstance * retrieveStackInstance(const CBonusSystemNode * node)
{
	switch(node->getNodeType())
	{
	case BonusNodeType::STACK_INSTANCE:
		return (dynamic_cast<const CStackInstance *>(node));
	case BonusNodeType::STACK_BATTLE:
		return (dynamic_cast<const CStack *>(node))->base;
	default:
		return nullptr;
	}
}

static const CCreature * retrieveCreature(const CBonusSystemNode *node)
{
	switch(node->getNodeType())
	{
	case BonusNodeType::CREATURE:
		return (dynamic_cast<const CCreature *>(node));
	case BonusNodeType::STACK_BATTLE:
		return (dynamic_cast<const CStack *>(node))->unitType();
	default:
		const CStackInstance * csi = retrieveStackInstance(node);
		if(csi)
			return csi->getCreature();
		return nullptr;
	}
}


namespace
{
CSelector selectorFor(const BonusLimiterQuery & query)
{
 boost::container::static_vector<CSelector, 4> segments;
 if(query.type) segments.push_back(Selector::type()(*query.type));
 if(query.subtype) segments.push_back(Selector::subtype()(*query.subtype));
 if(query.source && query.sourceID) segments.push_back(Selector::source(*query.source, *query.sourceID));
 else if(query.source) segments.push_back(Selector::sourceTypeSel(*query.source));
 else if(query.sourceID) segments.push_back([id = *query.sourceID](const Bonus * bonus) { return bonus->sid == id; });
 auto result = segments.empty() ? Selector::none : segments[0];
 for(size_t i = 1; i < segments.size(); ++i) result = result.And(segments[i]);
 return result;
}
std::optional<int> firstValue(const BonusList & list, const BonusLimiterQuery & query)
{
 if(auto bonus = list.getFirst(selectorFor(query))) return bonus->val;
 return std::nullopt;
}
template<class T> const T & required(const std::optional<T> & value, const char * name)
{
 if(!value) throw std::logic_error(std::string("Missing limiter fact: ") + name);
 return *value;
}
}

NativeBonusLimiterContext::NativeBonusLimiterContext(const BonusLimitationContext & context) : context(context) {}
const CCreature * NativeBonusLimiterContext::getCreature() const
{
 if(!creature) creature = retrieveCreature(&context.node);
 return *creature;
}

BonusNodeType NativeBonusLimiterContext::nodeType() const { return context.node.getNodeType(); }
PlayerColor NativeBonusLimiterContext::owner() const { return context.node.getOwner(); }
PlayerColor NativeBonusLimiterContext::bonusOwner() const { return context.b.bonusOwner; }
BonusSource NativeBonusLimiterContext::bonusSource() const { return context.b.source; }
BonusSourceID NativeBonusLimiterContext::bonusSourceID() const { return context.b.sid; }
bool NativeBonusLimiterContext::hasCreature() const { return getCreature() != nullptr; }
CreatureID NativeBonusLimiterContext::creatureID() const { return getCreature()->getId(); }
int32_t NativeBonusLimiterContext::creatureLevel() const { return getCreature()->getLevel(); }
EAlignment NativeBonusLimiterContext::creatureAlignment() const { return getCreature()->getAlignment(); }
bool NativeBonusLimiterContext::hasStackInstance() const { return retrieveStackInstance(&context.node) != nullptr; }
bool NativeBonusLimiterContext::isCommander() const { return retrieveStackInstance(&context.node)->getNodeType() == BonusNodeType::COMMANDER; }
int NativeBonusLimiterContext::rank() const { return retrieveStackInstance(&context.node)->getExpRank(); }
bool NativeBonusLimiterContext::hasBattleStack() const { return retrieveStackBattle(&context.node) != nullptr; }
BattleHexArray NativeBonusLimiterContext::hexes() const { return retrieveStackBattle(&context.node)->getHexes(); }
bool NativeBonusLimiterContext::validPosition() const { return retrieveStackBattle(&context.node)->getPosition().isValid(); }
bool NativeBonusLimiterContext::hasFaction() const { return dynamic_cast<const INativeTerrainProvider *>(&context.node) != nullptr; }
FactionID NativeBonusLimiterContext::faction() const { return dynamic_cast<const INativeTerrainProvider *>(&context.node)->getFactionID(); }
FactionID NativeBonusLimiterContext::sourceCreatureFaction() const { return context.b.sid.as<CreatureID>().toCreature()->getFactionID(); }
bool NativeBonusLimiterContext::creatureUpgradeOf(CreatureID ancestor) const
{
 return ancestor.toCreature()->isMyDirectOrIndirectUpgrade(getCreature());
}
bool NativeBonusLimiterContext::adjacentTo(CreatureID target) const
{
 const auto * stack = retrieveStackBattle(&context.node);
 for(const auto * unit : stack->getBattle()->battleAdjacentUnits(stack))
  if(unit->creatureId() == target) return true;
 return false;
}
ETerrainId NativeBonusLimiterContext::currentTerrain() const
{
 const auto type = context.node.getNodeType();
 if(type == BonusNodeType::STACK_BATTLE || type == BonusNodeType::STACK_INSTANCE)
 {
  if(const auto * stack = retrieveStackBattle(&context.node)) return stack->getCurrentTerrain();
  if(const auto * stack = retrieveStackInstance(&context.node)) return stack->getCurrentTerrain();
 }
 else if(const auto * army = dynamic_cast<const CArmedInstance *>(&context.node)) return army->getCurrentTerrain();
 return ETerrainId::NONE;
}
bool NativeBonusLimiterContext::nativeTerrain(ETerrainId terrain) const
{
 return dynamic_cast<const INativeTerrainProvider *>(&context.node)->getFactionID().toEntity(LIBRARY)->isNativeTerrain(terrain);
}
bool NativeBonusLimiterContext::accepted(const BonusLimiterQuery & query) const { return context.alreadyAccepted.getFirst(selectorFor(query)) != nullptr; }
bool NativeBonusLimiterContext::undecided(const BonusLimiterQuery & query) const { return context.stillUndecided.getFirst(selectorFor(query)) != nullptr; }
int NativeBonusLimiterContext::acceptedValue(const BonusLimiterQuery & query) const { return context.alreadyAccepted.valOfBonuses(selectorFor(query)); }
std::optional<int> NativeBonusLimiterContext::firstAcceptedValue(const BonusLimiterQuery & query) const { return firstValue(context.alreadyAccepted, query); }
bool SnapshotBonusLimiterContext::accepted(const BonusLimiterQuery & query) const { return acceptedBonuses.getFirst(selectorFor(query)) != nullptr; }
bool SnapshotBonusLimiterContext::undecided(const BonusLimiterQuery & query) const { return undecidedBonuses.getFirst(selectorFor(query)) != nullptr; }
int SnapshotBonusLimiterContext::acceptedValue(const BonusLimiterQuery & query) const { return acceptedBonuses.valOfBonuses(selectorFor(query)); }
std::optional<int> SnapshotBonusLimiterContext::firstAcceptedValue(const BonusLimiterQuery & query) const { return firstValue(acceptedBonuses, query); }

CapturedCreatureSourceFactionTable::CapturedCreatureSourceFactionTable(const std::map<CreatureID, FactionID> & source)
 : values(source) {}

std::shared_ptr<const CapturedCreatureSourceFactionTable> CapturedCreatureSourceFactionTable::capture(const std::map<CreatureID, FactionID> & source)
{
 return std::shared_ptr<const CapturedCreatureSourceFactionTable>(new const CapturedCreatureSourceFactionTable(source));
}

FactionID CapturedCreatureSourceFactionTable::at(CreatureID creature) const
{
 return values.at(creature);
}

SnapshotBonusLimiterContext::SnapshotBonusLimiterContext(BonusLimiterFacts facts, const Bonus & bonus,
 const BonusList & accepted, const BonusList & undecided)
 : facts(std::move(facts)), subjectOwner(bonus.bonusOwner), subjectSource(bonus.source), subjectSourceID(bonus.sid)
{
 std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> identities;
 auto copyList = [&identities](const BonusList & source, BonusList & destination)
 {
  for(const auto & original : source)
  {
   auto & copy = identities[original.get()];
   if(!copy)
   {
    copy = std::make_shared<Bonus>();
    copy->type = original->type;
    copy->subtype = original->subtype;
    copy->source = original->source;
    copy->sid = original->sid;
    copy->val = original->val;
    copy->valType = original->valType;
    copy->targetSourceType = original->targetSourceType;
    copy->targetSourceID = original->targetSourceID;
    copy->stacking = original->stacking;
    copy->effectRange = original->effectRange;
   }
   destination.push_back(copy);
  }
 };
 copyList(accepted, acceptedBonuses);
 copyList(undecided, undecidedBonuses);
}
BonusNodeType SnapshotBonusLimiterContext::nodeType() const { return facts.type; }
PlayerColor SnapshotBonusLimiterContext::owner() const { return required(facts.originalOwner, "owner"); }
PlayerColor SnapshotBonusLimiterContext::bonusOwner() const { return subjectOwner; }
BonusSource SnapshotBonusLimiterContext::bonusSource() const { return subjectSource; }
BonusSourceID SnapshotBonusLimiterContext::bonusSourceID() const { return subjectSourceID; }
bool SnapshotBonusLimiterContext::hasCreature() const { return facts.creature; }
CreatureID SnapshotBonusLimiterContext::creatureID() const { return required(facts.creatureID, "creatureID"); }
int32_t SnapshotBonusLimiterContext::creatureLevel() const { return required(facts.creatureLevel, "creatureLevel"); }
EAlignment SnapshotBonusLimiterContext::creatureAlignment() const { return required(facts.creatureAlignment, "creatureAlignment"); }
bool SnapshotBonusLimiterContext::hasStackInstance() const { return facts.stackInstance; }
bool SnapshotBonusLimiterContext::isCommander() const { return required(facts.commander, "commander"); }
int SnapshotBonusLimiterContext::rank() const { return required(facts.rank, "rank"); }
bool SnapshotBonusLimiterContext::hasBattleStack() const { return facts.battleStack; }
BattleHexArray SnapshotBonusLimiterContext::hexes() const { return required(facts.hexes, "hexes"); }
bool SnapshotBonusLimiterContext::validPosition() const { return required(facts.validPosition, "validPosition"); }
bool SnapshotBonusLimiterContext::hasFaction() const { return facts.factionProvider; }
FactionID SnapshotBonusLimiterContext::faction() const { return required(facts.faction, "faction"); }
FactionID SnapshotBonusLimiterContext::sourceCreatureFaction() const
{
 if(facts.creatureSourceFactions)
  return facts.creatureSourceFactions->at(subjectSourceID.as<CreatureID>());
 if(facts.capturedCreatureSourceFactions)
  return facts.capturedCreatureSourceFactions->at(subjectSourceID.as<CreatureID>());
 throw std::logic_error("Missing limiter fact: creature source factions");
}
bool SnapshotBonusLimiterContext::creatureUpgradeOf(CreatureID ancestor) const
{
 return required(facts.upgradeAncestors, "upgrade ancestors").contains(ancestor);
}
bool SnapshotBonusLimiterContext::adjacentTo(CreatureID target) const
{
 return vstd::contains(required(facts.adjacentCreatures, "adjacent creatures"), target);
}
ETerrainId SnapshotBonusLimiterContext::currentTerrain() const { return required(facts.currentTerrain, "terrain"); }
bool SnapshotBonusLimiterContext::nativeTerrain(ETerrainId terrain) const
{
 return required(facts.nativeTerrains, "native terrain relation").contains(terrain);
}
