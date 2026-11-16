/*
 * OwnedBattleBonusModel.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "OwnedBattleBonusModel.h"
#include "BattleInfo.h"
#include "AmmoCartQuery.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../CStack.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusGraphMutationState.h"

OwnedBattleBonusModel OwnedBattleBonusModel::capture(const BattleInfo & battle, OwnedSpellPersistenceFacts persistence, const std::vector<TConstBonusListPtr> & extraLists)
{
 return captureImpl(battle, std::move(persistence), extraLists, nullptr);
}

std::pair<OwnedBattleBonusModel, OrdinaryPreviewDefinitionGuard> OwnedBattleBonusModel::captureForOrdinarySeed(
 const BattleInfo & battle, OwnedSpellPersistenceFacts persistence, const std::vector<TConstBonusListPtr> & extraLists)
{
 std::optional<OrdinaryPreviewDefinitionGuard> guard;
 auto model = captureImpl(battle, std::move(persistence), extraLists, &guard);
 return {std::move(model), std::move(guard.value())};
}

OwnedBattleBonusModel OwnedBattleBonusModel::captureImpl(const BattleInfo & battle, OwnedSpellPersistenceFacts persistence,
 const std::vector<TConstBonusListPtr> & extraLists, std::optional<OrdinaryPreviewDefinitionGuard> * qualification)
{
 OwnedBattleBonusModel result;
 result.battleTerrain = battle.getTerrainType();
 for(size_t i = 0; i < 2; ++i)
 {
  const auto side = i == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
  result.sidePlayers[i] = battle.getSidePlayer(side);
  const auto * hero = battle.getSideHero(side);
  if(hero)
  {
   const auto cart = hero->artifactsWorn.find(ArtifactPosition::MACH2);
   result.artifactCarts[i] = cart != hero->artifactsWorn.end() && cart->second.getArt()->getTypeId() == ArtifactID::AMMO_CART;
  }
 }
 const auto units = battle.getUnitsIf([](const battle::Unit *) { return true; });
 std::vector<const CBonusSystemNode *> roots;
 for(const auto * unit : units)
 {
  const auto * stack = dynamic_cast<const CStack *>(unit);
  if(!stack)
   throw std::invalid_argument("Owned battle bonus capture requires native stacks");
  roots.push_back(stack);
 }
 std::vector<CreatureID> definitions;
 LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
 {
  const auto * node = dynamic_cast<const CBonusSystemNode *>(creature);
  if(!node) throw std::invalid_argument("Creature definition has no native bonus graph");
  result.creatureGeometryAndCart.emplace(creature->getId(), std::pair{creature->isDoubleWide(), creature->getWarMachine() == ArtifactID::AMMO_CART});
  definitions.push_back(creature->getId());
  roots.push_back(node);
 });
 std::array<std::optional<size_t>, 2> armyRoots, heroRoots;
 for(size_t i = 0; i < 2; ++i)
 {
  const auto side = i == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
  if(const auto * army = battle.getSideArmy(side)) { armyRoots[i] = roots.size(); roots.push_back(army); }
  if(const auto * hero = battle.getSideHero(side)) { heroRoots[i] = roots.size(); roots.push_back(hero); }
 }
 const auto battleRoot=roots.size(); roots.push_back(&battle);
 // All non-definition roots can initiate recursive ordinary mutation.
 auto evaluation = [&]()
 {
  if(!qualification) return BonusEvaluationSnapshot::captureWithLists(roots, extraLists);
  std::vector<const CBonusSystemNode *> mutationRoots;
  for(size_t i = units.size() + definitions.size(); i < roots.size(); ++i)
   mutationRoots.push_back(roots.at(i));
  auto captured = OrdinaryPreviewDefinitionGuard::captureEvaluationForOrdinarySeed(roots, mutationRoots, extraLists);
  qualification->emplace(std::move(captured.second));
  return std::move(captured.first);
 }();
 result.bonusSession = std::make_unique<OwnedBonusGraphSession>(OwnedBonusGraphSession::capture(std::move(evaluation), std::move(persistence)));
 result.battleNode=result.evaluation().graph().roots().at(battleRoot);
 for(size_t i = 0; i < 2; ++i)
 {
  if(armyRoots[i]) result.sideArmyNodes[i] = result.evaluation().graph().roots().at(*armyRoots[i]);
  if(heroRoots[i]) result.sideHeroNodes[i] = result.evaluation().graph().roots().at(*heroRoots[i]);
 }
 for(size_t i = 0; i < definitions.size(); ++i)
 {
  const auto inserted = result.creatureNodes.emplace(definitions[i], result.evaluation().graph().roots().at(units.size() + i)).second;
  if(qualification && !inserted)
   throw std::invalid_argument("Ordinary preview seed has duplicate registered Creature IDs");
 }
 if(qualification)
 {
  const auto & bindings = qualification->value().definitions();
  for(const auto & binding : bindings)
  {
   const auto registered = result.creatureNodes.find(binding.native->getId());
   if(registered == result.creatureNodes.end() || registered->second != binding.node)
    throw std::invalid_argument("Ordinary preview Creature binding does not match its registered capture root");
  }
  if(bindings.size() != definitions.size())
   throw std::invalid_argument("Ordinary preview Creature bindings differ from registered definitions");
 }
 for(size_t i = 0; i < units.size(); ++i)
 {
  const auto * unit = units[i];
  const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
  if(!state) throw std::invalid_argument("Native unit state missing");
  result.unitFacts.push_back({unit->unitId(), result.evaluation().graph().roots()[i], unit->creatureId(), unit->unitSide(), unit->unitOwner(), unit->doubleWide(), unit->isAmmoCart(),
   state->health.getCount(), state->cloneID, unit->alive(), state->ghost, state->ghostPending,
   state->position.isValid(), unit->getHexes(), state->position.isValid() ? unit->getSurroundingHexes() : BattleHexArray{}});
 }
 return result;
}

const std::vector<OwnedBattleBonusModel::UnitFacts> & OwnedBattleBonusModel::units() const { return unitFacts; }
const BonusEvaluationSnapshot & OwnedBattleBonusModel::evaluation() const { return bonusSession->evaluationRef(); }
OwnedBattleBonusModel::UnitFacts * OwnedBattleBonusModel::find(uint32_t id)
{
 for(auto & unit : unitFacts)
  if(unit.id == id)
   return &unit;
 return nullptr;
}
TConstBonusListPtr OwnedBattleBonusModel::getAllBonuses(uint32_t id, const CSelector & selector) const
{
 for(const auto & unit : unitFacts)
  if(unit.id == id) return evaluation().getAllBonuses(unit.node,selector);
 throw std::out_of_range("Missing owned battle unit");
}
TConstBonusListPtr OwnedBattleBonusModel::queryBonuses(uint32_t id, const CSelector & selector, const std::string & key) const
{
 for(const auto & unit : unitFacts)
  if(unit.id == id)
   return bonusSession->query(unit.node, selector, key);
 throw std::out_of_range("Missing owned battle unit");
}

int32_t OwnedBattleBonusModel::nodeVersion(uint32_t id) const
{
 for(const auto & unit : unitFacts) if(unit.id == id)
 {
  const auto version = bonusSession->nodeVersion(unit.node);
  if(version > INT32_MAX || version < INT32_MIN) throw std::overflow_error("Owned node epoch exhausted");
  return static_cast<int32_t>(version);
 }
 throw std::out_of_range("Missing owned battle unit");
}
void OwnedBattleBonusModel::invalidateUnit(uint32_t id)
{
 const auto * unit = find(id);
 if(!unit) return;
 bonusSession->invalidateNodeAndDescendants(unit->node);
}

void OwnedBattleBonusModel::invalidateBattle()
{
 bonusSession->invalidateNodeAndDescendants(battleNode);
}

void OwnedBattleBonusModel::refreshUnitContexts()
{
 auto bonuses = evaluation();
 for(const auto & unit : unitFacts)
 {
  auto & context = bonuses.updaterContexts.at(unit.node);
  if(context.nodeType() != BonusNodeType::STACK_BATTLE)
   throw std::logic_error("Owned unit node is not a battle stack");
  context.countOfStack = unit.count;
  auto & facts = bonuses.limiterFacts.at(unit.node);
  facts.hexes = unit.hexes;
  facts.validPosition = unit.validPosition;
  facts.adjacentCreatures.emplace();
  for(const auto & other : unitFacts)
  {
   if(!unit.validPosition)
    break;
   if(!other.alive)
    continue;
   for(auto hex : other.hexes)
    if(unit.surrounding.contains(hex))
    {
     facts.adjacentCreatures->push_back(other.creature);
     break;
    }
  }
 }
 bonusSession->replaceEvaluation(std::move(bonuses));
}

bool OwnedBattleBonusModel::removeUnit(uint32_t id)
{
 auto transaction = transactionCopy();
 std::set<uint32_t> pending{id};
 bool complete = true;
 while(!pending.empty())
 {
  const auto current = *pending.begin();
  auto * unit = transaction.find(current);
  if(!unit)
  {
   complete = false;
   break;
  }
  if(!unit->ghost)
  {
   unit->count = 0;
   unit->alive = false;
   unit->ghostPending = false;
   unit->ghost = true;
   transaction.refreshUnitContexts();
   auto graph = transaction.evaluation().mutations();
   while(!graph.nodes().at(unit->node).propagationParents.empty())
    graph.detach(unit->node, graph.nodes().at(unit->node).propagationParents.front());
   while(!graph.nodes().at(unit->node).inheritanceParents.empty())
    graph.detachSource(unit->node, graph.nodes().at(unit->node).inheritanceParents.front());
   transaction.bonusSession->replaceEvaluation(transaction.evaluation().withGraph(graph.snapshot()));
   if(unit->cloneID >= 0)
   {
    pending.insert(unit->cloneID);
    unit->cloneID = -1;
   }
   for(auto & other : transaction.unitFacts)
    if(other.cloneID == current)
     other.cloneID = -1;
  }
  pending.erase(current);
 }
 if(transaction.unitFacts != unitFacts)
  ++transaction.stateRevision;
 *this = std::move(transaction);
 return complete;
}

uint64_t OwnedBattleBonusModel::revision() const { return stateRevision; }

bool OwnedBattleBonusModel::synchronizeUnits(const std::vector<UnitFacts> & observations)
{
 if(observations.size() != unitFacts.size())
  throw std::invalid_argument("Owned unit observation set changed");
 for(size_t i = 0; i < observations.size(); ++i)
 {
  const auto & next = observations[i];
  const auto & current = unitFacts[i];
  if(next.id != current.id || next.node != current.node || next.creature != current.creature || next.side != current.side || next.originalOwner != current.originalOwner || next.doubleWide != current.doubleWide || next.ammoCart != current.ammoCart)
   throw std::invalid_argument("Owned unit observation identity changed");
  if(next.count < 0 || next.alive != (next.count > 0))
   throw std::invalid_argument("Owned unit observation count and alive disagree");
 }
 if(observations == unitFacts)
  return false;
 auto transaction = transactionCopy();
 transaction.unitFacts = observations;
 transaction.refreshUnitContexts();
 ++transaction.stateRevision;
 *this = std::move(transaction);
 return true;
}

OwnedBattleBonusModel::UnitFacts OwnedBattleBonusModel::observeUnit(uint32_t id, const battle::CUnitState & state) const
{
 for(const auto & original : unitFacts)
  if(original.id == id)
  {
   auto result = original;
   result.count = state.health.getCount();
   result.alive = result.count > 0;
   result.cloneID = state.cloneID;
   result.ghost = state.ghost;
   result.ghostPending = state.ghostPending;
   result.validPosition = state.position.isValid();
   result.hexes = battle::Unit::getHexes(state.position, original.doubleWide, original.side);
   result.surrounding = result.validPosition ? battle::Unit::getSurroundingHexes(state.position, original.doubleWide, original.side) : BattleHexArray{};
   return result;
  }
 throw std::out_of_range("Missing owned battle observation unit");
}


PlayerColor OwnedBattleBonusModel::ownerForUnit(BattleSide side, bool hypnotized) const
{
 if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
  throw std::invalid_argument("Invalid owned battle side");
 const auto initial = sidePlayers.at(side == BattleSide::ATTACKER ? 0 : 1);
 return hypnotized ? (initial == sidePlayers[0] ? sidePlayers[1] : sidePlayers[0]) : initial;
}

PlayerColor OwnedBattleBonusModel::unitEffectiveOwner(uint32_t id) const
{
 for(const auto & unit : unitFacts)
  if(unit.id == id)
   return ownerForUnit(unit.side, !queryBonuses(id, Selector::type()(BonusType::HYPNOTIZED))->empty());
 throw std::out_of_range("Missing owned ammunition unit");
}

bool OwnedBattleBonusModel::unitHasAmmoCart(uint32_t id) const
{
 for(const auto & unit : unitFacts)
  if(unit.id == id)
   return ammoForUnit(id, unit.side, !queryBonuses(id, Selector::type()(BonusType::HYPNOTIZED))->empty());
 throw std::out_of_range("Missing owned ammunition unit");
}

bool OwnedBattleBonusModel::ammoForUnit(uint32_t id, BattleSide side, bool hypnotized) const
{
 const auto originalSideOwner = ownerForUnit(side, false);
 return AmmoCartQuery::evaluate(unitFacts,
  [&](const UnitFacts & cart) { return cart.id == id || unitEffectiveOwner(cart.id) == originalSideOwner; },
  [](const UnitFacts & cart) { return cart.ammoCart; },
  [](const UnitFacts & cart) { return cart.alive; },
  [&]()
  {
   const auto owner = ownerForUnit(side, hypnotized);
   if(owner == sidePlayers[0]) return artifactCarts[0];
   if(owner == sidePlayers[1]) return artifactCarts[1];
   return false;
  });
}

BonusGraphSnapshot::NodeId OwnedBattleBonusModel::creatureNode(CreatureID creature) const
{
 return creatureNodes.at(creature);
}

namespace
{
class NewBattleStackUpdaterFacts final : public BonusUpdaterContext
{
 const PlayerColor player; const int count; const int level;
public:
 NewBattleStackUpdaterFacts(PlayerColor p, int c, int l) : player(p), count(c), level(l) {}
 BonusNodeType nodeType() const override { return BonusNodeType::STACK_BATTLE; }
 PlayerColor owner() const override { return player; }
 int stackCount() const override { return count; }
 int stackLevel() const override { return level; }
 int heroLevel() const override { throw std::logic_error("New stack is not a hero"); }
 std::vector<BonusUpdaterArmyStack> armyStacks() const override { throw std::logic_error("New stack is not an army"); }
 int armySize(CreatureID, FactionID, int) const override { throw std::logic_error("New stack is not an army"); }
};
}
BonusGraphSnapshot::NodeId OwnedBattleBonusModel::sideArmyNode(BattleSide side) const
{
 if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) throw std::invalid_argument("Invalid battle side");
 const auto & army = sideArmyNodes.at(side == BattleSide::ATTACKER ? 0 : 1);
 if(!army) throw std::invalid_argument("Battle side has no captured army");
 return *army;
}
void OwnedBattleBonusModel::addUnit(const battle::UnitInfo & info)
{
 if(find(info.id)) throw std::invalid_argument("Duplicate owned battle unit ID");
 const auto army = sideArmyNode(info.side);
 const auto creature = creatureNode(info.type);
 const auto owner = ownerForUnit(info.side, false);
 auto facts = evaluation().limiterFacts.at(creature);
 facts.type = BonusNodeType::STACK_BATTLE; facts.originalOwner = owner;
 facts.stackInstance = false; facts.commander.reset(); facts.rank.reset();
 facts.battleStack = true; facts.currentTerrain = battleTerrain;
 const auto [wide, cart] = creatureGeometryAndCart.at(info.type);
 const auto hexes = battle::Unit::getHexes(info.position, wide, info.side);
 const auto surrounding = info.position.isValid() ? battle::Unit::getSurroundingHexes(info.position, wide, info.side) : BattleHexArray{};
 facts.hexes = hexes; facts.validPosition = info.position.isValid(); facts.adjacentCreatures.emplace();
 const int count = std::max(0, info.count);
 SnapshotBonusUpdaterContext context(NewBattleStackUpdaterFacts(owner, count, facts.creatureLevel.value()));
 auto transaction = transactionCopy();
 auto [evaluation, node] = transaction.evaluation().appendBattleLeaf(std::move(context), std::move(facts), army, creature);
 transaction.bonusSession->replaceEvaluation(std::move(evaluation));
 transaction.unitFacts.push_back({info.id,node,info.type,info.side,owner,wide,cart,count,-1,count>0,false,false,info.position.isValid(),hexes,surrounding});
 transaction.refreshUnitContexts(); ++transaction.stateRevision;
 *this = std::move(transaction);
}

OwnedBattleBonusModel::OwnedBattleBonusModel() = default;
OwnedBattleBonusModel::~OwnedBattleBonusModel() = default;
OwnedBattleBonusModel::OwnedBattleBonusModel(OwnedBattleBonusModel &&) noexcept = default;
OwnedBattleBonusModel & OwnedBattleBonusModel::operator=(OwnedBattleBonusModel &&) noexcept = default;
OwnedBattleBonusModel::OwnedBattleBonusModel(const OwnedBattleBonusModel & other) : OwnedBattleBonusModel(other, CopyKind::Fork) {}
OwnedBattleBonusModel & OwnedBattleBonusModel::operator=(const OwnedBattleBonusModel & other)
{
 if(this != &other) { OwnedBattleBonusModel copy(other); *this = std::move(copy); }
 return *this;
}
OwnedBattleBonusModel::OwnedBattleBonusModel(const OwnedBattleBonusModel & other, CopyKind kind)
 : bonusSession(other.bonusSession ? std::make_unique<OwnedBonusGraphSession>(kind == CopyKind::Fork ? other.bonusSession->fork() : other.bonusSession->transactionCopy()) : nullptr),
 unitFacts(other.unitFacts), stateRevision(other.stateRevision), sidePlayers(other.sidePlayers), artifactCarts(other.artifactCarts),
 creatureNodes(other.creatureNodes), creatureGeometryAndCart(other.creatureGeometryAndCart), sideArmyNodes(other.sideArmyNodes), sideHeroNodes(other.sideHeroNodes), battleTerrain(other.battleTerrain), battleNode(other.battleNode)
{}
OwnedBattleBonusModel OwnedBattleBonusModel::transactionCopy() const { return OwnedBattleBonusModel(*this, CopyKind::Transaction); }
OwnedLogicalBonusEvent OwnedBattleBonusModel::makeBonusEvent(const std::vector<Bonus> & values) const
{
 return bonusSession->makeEvent(values);
}
OwnedLogicalBonusEvent OwnedBattleBonusModel::eventFromUnitExports(uint32_t unit, const CSelector & selector) const
{
 for(const auto & facts : unitFacts) if(facts.id == unit)
 {
  std::vector<BonusGraphSnapshot::BonusId> ids;
  for(auto id : evaluation().graph().nodes().at(facts.node).exported)
   if(selector(evaluation().graph().bonuses().at(id).get())) ids.push_back(id);
  return bonusSession->eventFromBonuses(ids);
 }
 throw std::out_of_range("Missing owned battle unit");
}
size_t OwnedBattleBonusModel::bonusEventSize(const OwnedLogicalBonusEvent & event) const
{ return bonusSession->eventSize(event); }
void OwnedBattleBonusModel::applyBonusEventItem(uint32_t unit, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd)
{
 const auto * facts = find(unit);
 if(!facts) return;
 if(stateRevision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned battle revision exhausted");
 bonusSession->applyEventItem(facts->node, event, index, forceAdd);
 ++stateRevision;
}
bool OwnedBattleBonusModel::applyBonusEvent(uint32_t unit, const OwnedLogicalBonusEvent & event, bool forceAdd)
{
 if(!find(unit)) return false;
 const auto count = bonusEventSize(event);
 for(size_t i = 0; i < count; ++i) applyBonusEventItem(unit, event, i, forceAdd);
 return true;
}

bool OwnedBattleBonusModel::removeBonusEventItem(uint32_t unit, const OwnedLogicalBonusEvent & event, size_t index)
{
 const auto * facts = find(unit);
 if(!facts) return false;
 if(stateRevision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned battle revision exhausted");
 if(!bonusSession->removeEventItem(facts->node, event, index)) return false;
 ++stateRevision;
 return true;
}


bool OwnedBattleBonusModel::expireBonuses(uint32_t unit, OwnedBonusExpiry event)
{
 const auto * facts = find(unit);
 if(!facts) return false;
 if(stateRevision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned battle revision exhausted");
 if(!bonusSession->expireBonuses(facts->node, event)) return false;
 ++stateRevision;
 return true;
}

std::vector<OwnedLogicalBonusEvent> OwnedBattleBonusModel::importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const
{
 return bonusSession->importComponentEvents(groups);
}


bool OwnedBattleBonusModel::removeNonpersistentSpells(uint32_t unit)
{
 const auto * facts = find(unit);
 if(!facts) return false;
 if(stateRevision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned battle revision exhausted");
 if(!bonusSession->removeNonpersistentSpells(facts->node)) return false;
 ++stateRevision;
 return true;
}

bool OwnedBattleBonusModel::removeBonuses(uint32_t unit, const CSelector & selector)
{
 const auto * facts = find(unit);
 if(!facts) return false;
 if(stateRevision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned battle revision exhausted");
 if(!bonusSession->removeBonuses(facts->node, selector)) return false;
 ++stateRevision;
 return true;
}
