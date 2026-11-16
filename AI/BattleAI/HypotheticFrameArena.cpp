/* Owned hypothetical frame prototype. GPL-2.0-or-later. */
#include "StdInc.h"
#include "HypotheticFrameArena.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include <vcmi/spells/Service.h>
#include <vcmi/spells/Spell.h>
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/networkPacks/BattleChanges.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
class HypotheticFrameArena::FrameUnit final : public StackWithBonuses
{
 HypotheticFrameArena * const arena;
 const PreviewCreatureBinding descriptor;
 const int32_t level;
public:
 FrameUnit(HypotheticFrameArena * a, const battle::CUnitState & source, PreviewCreatureBinding d, const battle::CUnitState::BonusCaches * captured = nullptr)
 : StackWithBonuses(d.get(), source.unitBaseAmount(), source.unitId(), source.unitSide(), source.unitOwner(), source.unitSlot()),
 arena(a), descriptor(std::move(d)), level(source.unitLevel())
 { bindBonusCacheAccess(arena->accessMutex); bindEnvironment(arena); battle::CUnitState::operator=(source); restoreBonusCaches(captured ? *captured : source.captureBonusCaches()); }
 FrameUnit(HypotheticFrameArena * a, const battle::UnitInfo & info, PlayerColor owner,
  PreviewCreatureBinding d)
 : StackWithBonuses(d.get(), info.count, info.id, info.side, owner, SlotID::SUMMONED_SLOT_PLACEHOLDER),
 arena(a), descriptor(std::move(d)), level(std::max(1, descriptor->getLevel()))
 { bindBonusCacheAccess(arena->accessMutex); }
 void bindSession() noexcept
 {
  for(const auto & facts : arena->model.units()) if(facts.id == unitId())
  { bindOwnedBonusSession(arena->model.bonusSession, facts.node); return; }
  std::terminate();
 }
 void initializeAddedUnit(const battle::UnitInfo & info)
 {
  // Native CStack localInit queries health before publishing initialPosition.
  // This unit already belongs to storage, so each query sees its actual partial health.
  bindSession(); localInit(arena); position = info.position; summoned = info.summoned;
 }
 bool mutateLocalBonuses(LocalBonusMutation operation, const std::vector<Bonus> & values) override
 {
  if(!arena->ordinaryStoreMutators) return false;
  arena->mutateLocalBonuses(unitId(), values, operation == LocalBonusMutation::Remove,
   operation == LocalBonusMutation::Add);
  return true;
 }
 bool removeLocalBonuses(const CSelector & selector) override
 {
  if(!arena->ordinaryStoreMutators) return false;
  arena->removeBonuses(unitId(), selector);
  return true;
 }
 bool hasOwnedFrame() const override { return true; }
 int32_t unitLevel() const override { return level; }
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key) const override
 { return arena->queryUnit(unitId(), selector, key); }
 int32_t getTreeVersion() const override
 { return arena->unitVersion(unitId()); }
 std::shared_ptr<battle::Unit> acquire() const override { return acquireState(); }
 std::shared_ptr<battle::CUnitState> acquireState() const override
 {
  std::lock_guard<std::recursive_mutex> lock(arena->accessMutex);
  arena->synchronize(); auto owner = arena->shared_from_this();
  arena->model.queryBonuses(unitId(), Selector::all);
  return battle::OwnedDetachedUnit::capture(*this, arena->model.transactionCopy(), unitId(), descriptor,
   OwnedUnitEnvironmentHandle(std::shared_ptr<const battle::IUnitEnvironment>(owner, arena)));
 }
 void spendMana(ServerCallback *, int cost) const override { const_cast<FrameUnit *>(this)->casts.use(cost); }
};
HypotheticFrameArena::HypotheticFrameArena(OwnedBattleBonusModel m, DefinitionMode definitions) : model(std::move(m))
{
 if(definitions == DefinitionMode::OwnedFixture)
  definitionContext.emplace(model.evaluation(), model.revision(), model.bonusSession);
 else if(!model.revision() || !model.evaluation().graph().sameOrigin(model.evaluation().graph()))
  throw std::invalid_argument("Owned creature context requires a captured evaluation and nonzero revision");
}
HypotheticFrameArena::~HypotheticFrameArena() = default;
std::shared_ptr<HypotheticFrameArena> HypotheticFrameArena::capture(const BattleInfo & battle)
{
 return captureImpl(battle, nullptr);
}
std::pair<std::shared_ptr<HypotheticFrameArena>, OrdinaryPreviewDefinitionGuard> HypotheticFrameArena::captureForOrdinarySeed(const BattleInfo & battle)
{
 std::optional<OrdinaryPreviewDefinitionGuard> guard;
 auto arena = captureImpl(battle, &guard);
 return {std::move(arena), std::move(guard.value())};
}
std::shared_ptr<HypotheticFrameArena> HypotheticFrameArena::captureOrdinary(const BattleInfo & battle)
{
 std::optional<OrdinaryPreviewDefinitionGuard> guard;
 auto result = captureImpl(battle, &guard, DefinitionMode::BorrowedNative);
 result->ordinaryStoreMutators = true;
 result->nativeDefinitions = true;
 return result;
}
std::shared_ptr<HypotheticFrameArena> HypotheticFrameArena::captureImpl(const BattleInfo & battle, std::optional<OrdinaryPreviewDefinitionGuard> * qualification, DefinitionMode definitionMode)
{
 OwnedSpellPersistenceFacts persistence;
 LIBRARY->spells()->forEach([&](const spells::Spell * spell, bool &)
 { persistence.emplace(spell->getId(), spell->isPersistent()); });
 std::vector<battle::CUnitState::BonusCaches> capturedCaches;
 std::vector<TConstBonusListPtr> extraLists;
 const auto nativeUnits = battle.getUnitsIf([](const battle::Unit *) { return true; });
 std::set<uint32_t> unitIds;
 for(const auto * unit : nativeUnits)
 {
  const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
  if(!state) throw std::invalid_argument("Native unit state required for owned frame capture");
  if(!unitIds.insert(unit->unitId()).second) throw std::invalid_argument("Duplicate preview unit ID");
  capturedCaches.push_back(state->captureBonusCaches());
  const auto & caches = capturedCaches.back();
  extraLists.push_back(caches.immobilized.list ? caches.immobilized.list : std::make_shared<BonusList>());
  extraLists.push_back(caches.speed.list ? caches.speed.list : std::make_shared<BonusList>());
 }
 auto result = std::shared_ptr<HypotheticFrameArena>(new HypotheticFrameArena(OwnedBattleBonusModel::captureImpl(battle, std::move(persistence), extraLists, qualification), definitionMode));
 for(size_t i = 0; i < capturedCaches.size(); ++i)
 {
  auto & caches = capturedCaches[i];
  const auto & memberships = result->model.evaluation().graph().capturedExtraLists();
  caches.immobilized.membership = std::make_shared<const std::vector<OwnedBonusValueId>>(result->model.bonusSession->capturedMembership(memberships.at(2*i)));
  caches.speed.membership = std::make_shared<const std::vector<OwnedBonusValueId>>(result->model.bonusSession->capturedMembership(memberships.at(2*i+1)));
  caches.immobilized.list.reset(); caches.speed.list.reset();
 }
 result->round = battle.getRound();
 result->tacticDistance = battle.getTacticDist();
 result->tacticsSide = battle.getTacticsSide();
 result->hasObstacles = !battle.obstacles.empty();
 auto geometry = std::make_shared<BattleHexArray>();
 for(const auto & obstacle : battle.obstacles) geometry->insert(obstacle->getBlockedTiles());
 result->blockedHexes = std::move(geometry);
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  const auto & source = battle.getSide(side);
  result->sides.at(side) = {source.castSpellsCount, source.enchanterCounter, source.usedSpellsHistory};
 }
 auto sharedDefinitions = result->definitionContext ? result->definitionContext->view() : nullptr;
 LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
 {
  if(definitionMode == DefinitionMode::BorrowedNative)
  {
   result->definitions.emplace(creature->getId(), PreviewCreatureBinding::borrow(*creature));
   return;
  }
  const auto * faction = creature->getFactionID().toFaction(); std::set<TerrainId> terrains;
  if(faction) terrains.insert(faction->nativeTerrains.begin(), faction->nativeTerrains.end());
  auto descriptor = std::make_shared<const OwnedCreature>(OwnedCreature::captureValues(*creature, std::move(terrains)),
   sharedDefinitions, result->model.creatureNode(creature->getId()));
  result->definitions.emplace(creature->getId(), std::move(descriptor));
 });
 size_t cacheIndex = 0;
 for(const auto * unit : nativeUnits)
 {
  const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
  if(!state) throw std::invalid_argument("Native unit state required for owned frame capture");
  result->storage.push_back(std::make_unique<FrameUnit>(result.get(), *state, result->definitions.at(unit->creatureId()), &capturedCaches.at(cacheIndex++)));
  result->storage.back()->bindSession();
 }
 result->complete = true; result->synchronize(); return result;
}
std::shared_ptr<HypotheticFrameArena> HypotheticFrameArena::fork() const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize(); auto result = std::shared_ptr<HypotheticFrameArena>(new HypotheticFrameArena(model, definitionContext ? DefinitionMode::OwnedFixture : DefinitionMode::BorrowedNative));
 result->ordinaryStoreMutators = ordinaryStoreMutators;
 result->nativeDefinitions = nativeDefinitions;
 result->round = round; result->sides = sides; result->hasObstacles = hasObstacles; result->blockedHexes = blockedHexes;
 result->tacticDistance = tacticDistance; result->tacticsSide = tacticsSide;
 for(const auto & [id, definition] : definitions)
  result->definitions.emplace(id, definition.rebind(result->definitionContext ? result->definitionContext->view() : nullptr));
 for(const auto & unit : storage)
 {
  result->storage.push_back(std::make_unique<FrameUnit>(result.get(), *unit, result->definitions.at(unit->creatureId())));
  result->storage.back()->bindSession();
 }
 result->complete = true; result->synchronize(); return result;
}
void HypotheticFrameArena::synchronize() const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 // Only unpublished construction may omit units. Published refresh uses no bonus queries.
 if(!complete) return;
 std::vector<OwnedBattleBonusModel::UnitFacts> observed; observed.reserve(storage.size());
 for(const auto & unit : storage) observed.push_back(model.observeUnit(unit->unitId(), *unit));
 if(observed == model.units()) return;
 auto candidate = model.transactionCopy();
 candidate.synchronizeUnits(observed);
 commitModel(std::move(candidate));
}
HypotheticFrameArena::FrameUnit * HypotheticFrameArena::find(uint32_t id) const
{ for(const auto & unit : storage) if(unit->unitId() == id) return unit.get(); return nullptr; }
std::shared_ptr<StackWithBonuses> HypotheticFrameArena::unit(uint32_t id)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); auto * found = find(id); if(!found) return {}; return std::shared_ptr<StackWithBonuses>(shared_from_this(), found); }
battle::Units HypotheticFrameArena::units(const battle::UnitFilter & predicate) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); battle::Units result; for(const auto & unit : storage) if(predicate(unit.get())) result.push_back(unit.get()); return result; }
void HypotheticFrameArena::moveUnit(uint32_t id, const BattleHex & destination)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 auto * changed = find(id);
 if(!changed || !changed->alive()) return;
 synchronize();
 auto candidate = model.transactionCopy();
 auto observations = model.units();
 for(auto & facts : observations) if(facts.id == id)
 {
  facts.validPosition = destination.isValid();
  facts.hexes = battle::Unit::getHexes(destination, facts.doubleWide, facts.side);
  facts.surrounding = facts.validPosition ? battle::Unit::getSurroundingHexes(destination, facts.doubleWide, facts.side) : BattleHexArray{};
 }
 candidate.synchronizeUnits(observations);
 candidate.invalidateBattle();
 auto publication = prepareDefinitionPublication(candidate);
 changed->position = destination;
 model = std::move(candidate);
 for(const auto & unit : storage) unit->bindSession();
 commitDefinitionPublication(std::move(publication));
}

bool HypotheticFrameArena::removeUnit(uint32_t id)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize(); const auto previous = model.units();
 auto candidate = model.transactionCopy();
 const bool found = candidate.removeUnit(id);
 commitModel(std::move(candidate));
 for(size_t i = 0; i < storage.size(); ++i)
 {
  const auto & facts = model.units()[i];
  if(facts.ghost && !previous[i].ghost) storage[i]->onRemoved();
  storage[i]->cloneID = facts.cloneID;
 }
 return found;
}
bool HypotheticFrameArena::unitHasAmmoCart(const battle::Unit * unit) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); if(!unit) throw std::invalid_argument("Missing ammunition query unit"); synchronize(); return model.ammoForUnit(unit->unitId(), unit->unitSide(), unit->isHypnotized()); }
PlayerColor HypotheticFrameArena::unitEffectiveOwner(const battle::Unit * unit) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); if(!unit) throw std::invalid_argument("Missing owner query unit"); synchronize(); return model.ownerForUnit(unit->unitSide(), unit->isHypnotized()); }

TConstBonusListPtr HypotheticFrameArena::getAllBonuses(const CSelector & selector, const std::string & key) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 return nativeDefinitions
  ? model.bonusSession->queryWithStableCopies(model.battleNode, selector, key, ordinaryBonusCopies)
  : model.bonusSession->query(model.battleNode, selector, key);
}
TConstBonusListPtr HypotheticFrameArena::queryUnit(uint32_t id, const CSelector & selector, const std::string & key) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!nativeDefinitions) return model.queryBonuses(id, selector, key);
 for(const auto & unit : model.units())
  if(unit.id == id) return model.bonusSession->queryWithStableCopies(unit.node, selector, key, ordinaryBonusCopies);
 throw std::out_of_range("Missing ordinary preview unit");
}

int32_t HypotheticFrameArena::unitVersion(uint32_t id) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 return model.nodeVersion(id);
}

int32_t HypotheticFrameArena::getTreeVersion() const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 const auto version = model.bonusSession->nodeVersion(model.battleNode);
 if(version > INT32_MAX || version < INT32_MIN) throw std::overflow_error("Owned battle node epoch exhausted");
 return static_cast<int32_t>(version);
}

int32_t HypotheticFrameArena::revision() const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); synchronize(); return static_cast<int32_t>((model.revision() - 1) % INT32_MAX) + 1; }
PlayerColor HypotheticFrameArena::sidePlayer(BattleSide side) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); return model.ownerForUnit(side, false); }

void HypotheticFrameArena::addUnit(const battle::UnitInfo & info)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 auto added = std::make_unique<FrameUnit>(this, info, model.ownerForUnit(info.side, false), definitions.at(info.type));
 // Model validation commits before publication; capacity is reserved so publication
 // cannot fail after the model gains a node due solely to vector allocation.
 storage.reserve(storage.size() + 1);
 auto candidate = model.transactionCopy();
 candidate.addUnit(info);
 commitModel(std::move(candidate));
 storage.push_back(std::move(added));
 storage.back()->initializeAddedUnit(info);
 synchronize();
 model.invalidateUnit(info.id);
}

void HypotheticFrameArena::addUnit(uint32_t id, const JsonNode & data)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 const auto fail = [] { throw std::invalid_argument("Unsupported owned canonical unit addition"); };
 if(!data.isStruct()) fail();
 for(const auto & [field, value] : data.Struct())
 {
  if(field == "type") { if(value.getType() != JsonNode::JsonType::DATA_STRING) fail(); }
  else if(field == "summoned") { if(value.getType() != JsonNode::JsonType::DATA_BOOL) fail(); }
  else
  {
   if(field != "count" && field != "side" && field != "position") fail();
   if(value.getType() != JsonNode::JsonType::DATA_INTEGER) fail();
   const auto minimum = field == "position" ? -4 : 0;
   const auto maximum = field == "count" ? INT32_MAX : field == "side" ? 1 : GameConstants::BFIELD_SIZE - 1;
   if(value.Integer() < minimum || value.Integer() > maximum) fail();
  }
 }
 if(!data.Struct().contains("type")) fail();
 // Native UnitInfo::save writes canonical creature keys. Resolve those only
 // against captured descriptors, never the live identifier registry.
 const std::string key = data["type"].String();
 auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto & entry)
 { return entry.second->getJsonKey() == key; });
 if(found == definitions.end()) throw std::invalid_argument("Owned unit packet requires a captured canonical creature key");
 JsonNode scalarData = data;
 scalarData["type"] = JsonNode();
 battle::UnitInfo info;
 // Empty identifier takes the native parser's default branch, without registry lookup.
 info.load(id, scalarData);
 JsonNode canonical;
 info.save(canonical); // NONE has no identifier-encoder call.
 canonical["type"] = JsonNode(key);
 if(canonical != data) fail();
 info.type = found->first;
 addUnit(info);
}

OwnedLogicalBonusEvent HypotheticFrameArena::makeBonusEvent(const std::vector<Bonus> & values) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!nativeDefinitions) return model.makeBonusEvent(values);
 auto rebound = values;
 for(auto & value : rebound)
  if(value.parameters)
   if(auto source = ordinaryBonusCopies.sourceParameters(value.parameters)) value.parameters = std::move(source);
 return model.bonusSession->makeObservedEvent(rebound, ordinaryBonusCopies);
}
OwnedLogicalBonusEvent HypotheticFrameArena::eventFromUnitExports(uint32_t id, const CSelector & selector) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex); synchronize(); return model.eventFromUnitExports(id, selector); }
void HypotheticFrameArena::applyBonusEvent(uint32_t id, const OwnedLogicalBonusEvent & event, bool forceAdd)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!find(id)) return; // Native missing-target packet is a no-op, even for foreign data.
 const auto count = model.bonusEventSize(event);
 for(size_t index = 0; index < count; ++index)
 {
  auto candidate = model.transactionCopy();
  candidate.applyBonusEventItem(id, event, index, forceAdd);
  commitModel(std::move(candidate));
 }
}

HypotheticFrameArena::DefinitionPublication HypotheticFrameArena::prepareDefinitionPublication(const OwnedBattleBonusModel & candidate) const
{
 if(definitionContext)
  return {definitionContext->prepare(candidate.evaluation(), candidate.revision(), candidate.bonusSession), nativeDefinitionRevision, this, nativeDefinitionRevision};
 const auto & original = model.evaluation().graph();
 const auto & replacement = candidate.evaluation().graph();
 if(!original.sameOrigin(replacement) || candidate.revision() < model.revision())
  throw std::invalid_argument("Owned creature publication requires the same capture and monotonic revision");
 if(replacement.nodes().size() < original.nodes().size())
  throw std::invalid_argument("Owned creature publication removed captured nodes");
 for(size_t node = 0; node < original.nodes().size(); ++node)
  if(original.nodes().at(node).type != replacement.nodes().at(node).type
   || original.nodes().at(node).hypothetical != replacement.nodes().at(node).hypothetical)
   throw std::invalid_argument("Owned creature publication changed captured node kind");
 if(candidate.revision() == model.revision() && candidate.bonusSession == model.bonusSession)
  return {{}, nativeDefinitionRevision, this, nativeDefinitionRevision};
 if(nativeDefinitionRevision == std::numeric_limits<int32_t>::max())
  throw std::overflow_error("Owned creature cache revision exhausted");
 return {{}, nativeDefinitionRevision + 1, this, nativeDefinitionRevision};
}

void HypotheticFrameArena::commitDefinitionPublication(DefinitionPublication && publication) const noexcept
{
 if(publication.owner != this || publication.expectedNativeRevision != nativeDefinitionRevision) std::terminate();
 if(definitionContext)
 {
  if(!publication.owned) std::terminate();
  definitionContext->commit(std::move(*publication.owned));
 }
 else
 {
  if(publication.owned) std::terminate();
  nativeDefinitionRevision = publication.nativeRevision;
 }
 publication.owner = nullptr;
}

void HypotheticFrameArena::commitModel(OwnedBattleBonusModel candidate) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 // One serialized owner; no concurrent or reentrant observer can run between
 // these two publication steps. All allocation and validation finishes first.
 auto publication = prepareDefinitionPublication(candidate);
 static_assert(std::is_nothrow_move_assignable_v<OwnedBattleBonusModel>);
 static_assert(noexcept(commitDefinitionPublication(std::move(publication))));
 model = std::move(candidate);
 for(const auto & unit : storage) unit->bindSession();
 commitDefinitionPublication(std::move(publication));
}

void HypotheticFrameArena::removeBonusEvent(uint32_t id, const OwnedLogicalBonusEvent & event)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!find(id)) return;
 const auto count = model.bonusEventSize(event);
 for(size_t index = 0; index < count; ++index)
 {
  auto candidate = model.transactionCopy();
  if(candidate.removeBonusEventItem(id, event, index)) commitModel(std::move(candidate));
 }
}


void HypotheticFrameArena::expireBonuses(uint32_t id, OwnedBonusExpiry event)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!find(id)) return;
 auto candidate = model.transactionCopy();
 if(candidate.expireBonuses(id, event)) commitModel(std::move(candidate));
}

std::vector<OwnedLogicalBonusEvent> HypotheticFrameArena::importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize(); return model.importComponentEvents(groups);
}

namespace
{
void validateOwnedStateJson(const JsonNode & data)
{
 const auto fail = [] { throw std::invalid_argument("Unsupported owned canonical unit state"); };
 if(!data.isStruct() || data.Struct().size() != 1 || !data.Struct().contains("state") || !data["state"].isStruct()) fail();
 const std::set<std::string> booleans{"cloned", "drainedMana", "fear", "hadMorale", "castSpellThisTurn", "ghost", "ghostPending", "summoned", "waitedThisRound"};
 const std::map<std::string, std::set<std::string>> groups{
  {"casts", {"used"}}, {"shots", {"used"}}, {"counterAttacks", {"used", "totalCache"}},
  {"health", {"firstHPleft", "fullUnits", "resurrected"}}};
 const auto integer = [&](const JsonNode & node, int64_t minimum, int64_t maximum)
 {
  if(node.getType() != JsonNode::JsonType::DATA_INTEGER || node.Integer() < minimum || node.Integer() > maximum) fail();
 };
 for(const auto & [key, value] : data["state"].Struct())
 {
  if(booleans.contains(key)) { if(value.getType() != JsonNode::JsonType::DATA_BOOL) fail(); }
  else if(key == "turnStatus")
  {
   if(value.getType() != JsonNode::JsonType::DATA_STRING) fail();
   if(value.String() != "ready" && value.String() != "waiting" && value.String() != "acted" && value.String() != "defended") fail();
  }
  else if(key == "cloneID") integer(value, -1, INT32_MAX);
  else if(key == "position") integer(value, -4, GameConstants::BFIELD_SIZE - 1);
  else if(auto found = groups.find(key); found != groups.end())
  {
   if(!value.isStruct()) fail();
   for(const auto & [field, scalar] : value.Struct())
   {
    if(!found->second.contains(field)) fail();
    integer(scalar, 0, field == "fullUnits" ? INT32_MAX - 1 : INT32_MAX);
   }
  }
  else fail();
 }
}

void validateOwnedRoundState(const battle::CUnitState & state)
{
 const auto count = state.health.getCount();
 const auto firstHP = state.health.getFirstHPleft();
 const auto resurrected = state.health.getResurrected();
 if(count < 0 || firstHP < 0 || (count > 0 && firstHP == 0)
  || resurrected < 0 || resurrected > count || state.cloneID < -1)
  throw std::invalid_argument("Unsupported owned round unit state");
}
}

bool HypotheticFrameArena::resurrectionBlocked(const FrameUnit & oldState) const
{
 // Native getHexes and coversPos differ at sentinel positions and board edges.
 for(const auto & hex : oldState.getHexes())
 {
  if(blockedHexes->contains(hex)) return true;
  for(const auto & unit : storage)
   if(!unit->isGhost() && unit->coversPos(hex) && unit->alive()) return true;
 }
 return false;
}

void HypotheticFrameArena::applyStateUpdates(const CPackForClient & packet)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 std::vector<UnitChanges> changes;
 std::vector<uint32_t> indirectRecipients;
 std::optional<std::pair<uint32_t, bool>> attack;
 const auto appendVictims = [&](const std::vector<BattleStackAttacked> & victims)
 {
  for(const auto & victim : victims)
  {
   if(victim.flags & ~(BattleStackAttacked::KILLED | BattleStackAttacked::CLONE_KILLED | BattleStackAttacked::SECONDARY | BattleStackAttacked::SPELL_EFFECT))
    throw std::invalid_argument("Unsupported owned victim flags");
   if(victim.stackAttacked != victim.newState.id)
    throw std::invalid_argument("Owned victim identity mismatch");
   changes.push_back(victim.newState);
  }
 };
 if(typeid(packet) == typeid(BattleUnitsChanged))
  changes = static_cast<const BattleUnitsChanged &>(packet).changedStacks;
 else if(typeid(packet) == typeid(BattleAttack))
 {
  const auto & value = static_cast<const BattleAttack &>(packet);
  if(value.flags & ~(BattleAttack::SHOT | BattleAttack::COUNTER | BattleAttack::LUCKY | BattleAttack::UNLUCKY | BattleAttack::BALLISTA_DOUBLE_DMG | BattleAttack::DEATH_BLOW | BattleAttack::SPELL_LIKE | BattleAttack::CUSTOM_ANIMATION))
   throw std::invalid_argument("Unknown attack packet flags");
  changes = value.attackerChanges.changedStacks;
  appendVictims(value.bsa);
  attack = std::make_pair(value.stackAttacking, value.counter());
 }
 else if(typeid(packet) == typeid(StacksInjured))
 {
  const auto & value = static_cast<const StacksInjured &>(packet);
  appendVictims(value.stacks);
  for(const auto & victim : value.stacks) indirectRecipients.push_back(victim.stackAttacked);
 }
 else throw std::invalid_argument("Unsupported owned update packet");
 synchronize();
 auto candidate = std::shared_ptr<HypotheticFrameArena>(new HypotheticFrameArena(model.transactionCopy(), definitionContext ? DefinitionMode::OwnedFixture : DefinitionMode::BorrowedNative));
 candidate->round = round; candidate->sides = sides;
 candidate->nativeDefinitions = nativeDefinitions;
 candidate->ordinaryStoreMutators = ordinaryStoreMutators;
 candidate->tacticDistance = tacticDistance; candidate->tacticsSide = tacticsSide;
 candidate->hasObstacles = hasObstacles; candidate->blockedHexes = blockedHexes;
 for(const auto & [id, definition] : definitions)
  candidate->definitions.emplace(id, definition.rebind(candidate->definitionContext ? candidate->definitionContext->view() : nullptr));
 for(const auto & unit : storage)
 {
  candidate->storage.push_back(std::make_unique<FrameUnit>(candidate.get(), *unit, candidate->definitions.at(unit->creatureId())));
  candidate->storage.back()->bindSession();
 }
 candidate->complete = true;
 const auto getState = [&](uint32_t id) -> FrameUnit &
 {
  auto * state = candidate->find(id);
  if(!state) throw std::invalid_argument("Missing owned update unit");
  return *state;
 };
 if(attack && !getState(attack->first).alive())
  throw std::invalid_argument("Owned attack requires a living initial attacker");
 // Native indirect expiry visits every recipient before the first state load.
 for(auto id : indirectRecipients)
 {
  if(!getState(id).alive()) throw std::invalid_argument("Owned indirect packet requires living initial recipients");
  candidate->expireBonuses(id, OwnedBonusExpiry::IndirectDamage);
 }
 for(const auto & change : changes)
 {
  if(typeid(packet) == typeid(BattleUnitsChanged))
  {
   if(change.operation == BattleChanges::EOperation::ADD)
   { candidate->addUnit(change.id, change.data); continue; }
   if(change.operation == BattleChanges::EOperation::REMOVE)
   { candidate->removeUnit(change.id); continue; }
  }
  if(change.operation != BattleChanges::EOperation::UPDATE)
   throw std::invalid_argument("Unsupported owned unit operation");
  auto & state = getState(change.id);
  const bool wasAlive = state.alive();
  if(!wasAlive && change.healthDelta > 0 && candidate->resurrectionBlocked(state)) continue;
  validateOwnedStateJson(change.data);
  const auto previousSummoned = state.summoned;
  const auto previousCloned = state.cloned;
  const auto previousGhost = state.ghost;
  state.load(change.data);
  if(state.save() != change.data) throw std::invalid_argument("Owned update requires canonical state JSON");
  if(state.health.getResurrected() > state.health.getCount()
   || (!nativeDefinitions && (state.summoned != previousSummoned
    || state.cloned != previousCloned || state.ghost != previousGhost)))
   throw std::invalid_argument("Owned update changes unsupported lifecycle facts");
  candidate->synchronize();
  if(change.healthDelta < 0)
  {
   candidate->expireBonuses(change.id, OwnedBonusExpiry::BeingAttacked);
   candidate->model.invalidateUnit(change.id);
  }
  if(wasAlive && !state.alive())
  {
   if(state.cloneID >= 0)
   {
    auto * clone = candidate->find(static_cast<uint32_t>(state.cloneID));
    if(clone && clone->alive()) clone->makeGhost();
    state.cloneID = -1;
    candidate->synchronize();
   }
  }
  if(wasAlive != state.alive())
  {
   auto cleanup = candidate->model.transactionCopy();
   if(cleanup.removeNonpersistentSpells(change.id)) candidate->commitModel(std::move(cleanup));
  }
  if(!state.alive() && state.cloned)
  {
   for(const auto & unit : candidate->storage)
    if(unit->cloneID >= 0 && static_cast<uint32_t>(unit->cloneID) == change.id) unit->cloneID = -1;
   candidate->synchronize();
  }
 }
 if(attack)
 {
  candidate->expireBonuses(attack->first, OwnedBonusExpiry::Attack);
  if(!attack->second) candidate->expireBonuses(attack->first, OwnedBonusExpiry::OwnAttack);
 }
 candidate->synchronize();
 auto publication = prepareDefinitionPublication(candidate->model);
 static_assert(std::is_nothrow_copy_assignable_v<battle::CUnitState>);
 static_assert(noexcept(std::declval<FrameUnit &>().restoreBonusCaches(std::declval<const battle::CUnitState::BonusCaches &>())));
 static_assert(std::is_nothrow_move_assignable_v<OwnedBattleBonusModel>);
 static_assert(noexcept(commitDefinitionPublication(std::move(publication))));
 std::vector<battle::CUnitState::BonusCaches> caches;
 for(const auto & unit : candidate->storage) caches.push_back(unit->captureBonusCaches());
 // Destination-bound units remain unqueried until their model/session is published.
 std::vector<std::unique_ptr<FrameUnit>> added;
 added.reserve(candidate->storage.size() - storage.size());
 for(size_t i = storage.size(); i < candidate->storage.size(); ++i)
  added.push_back(std::make_unique<FrameUnit>(this, *candidate->storage[i],
   definitions.at(candidate->storage[i]->creatureId()), &caches[i]));
 storage.reserve(candidate->storage.size());
 for(size_t i = 0; i < storage.size(); ++i)
 {
  storage[i]->battle::CUnitState::operator=(*candidate->storage[i]);
  storage[i]->restoreBonusCaches(caches[i]);
 }
 for(auto & unit : added) storage.push_back(std::move(unit));
 model = std::move(candidate->model);
 for(const auto & unit : storage) unit->bindSession();
 commitDefinitionPublication(std::move(publication));
}

void HypotheticFrameArena::nextRound()
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 if(hasObstacles) throw std::invalid_argument("Owned round requires a captured obstacle-free battle");
 if(round < 0 || round == INT32_MAX) throw std::invalid_argument("Unsupported owned round number");
 for(const auto & unit : storage) validateOwnedRoundState(*unit);
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
  if(sides.at(side).enchanterCounter < 0) throw std::invalid_argument("Unsupported owned round enchanter counter");
 synchronize();
 // A transaction preserves event provenance while rebinding every unit query to
 // the unpublished graph. Ordinary detached states freeze pre-expiry bonuses.
 auto candidate = std::shared_ptr<HypotheticFrameArena>(new HypotheticFrameArena(model.transactionCopy(), definitionContext ? DefinitionMode::OwnedFixture : DefinitionMode::BorrowedNative));
 candidate->round = round; candidate->sides = sides;
 candidate->nativeDefinitions = nativeDefinitions;
 candidate->ordinaryStoreMutators = ordinaryStoreMutators;
 candidate->tacticDistance = tacticDistance; candidate->tacticsSide = tacticsSide;
 candidate->hasObstacles = hasObstacles; candidate->blockedHexes = blockedHexes;
 for(const auto & [id, definition] : definitions)
  candidate->definitions.emplace(id, definition.rebind(candidate->definitionContext ? candidate->definitionContext->view() : nullptr));
 for(const auto & unit : storage)
 {
  candidate->storage.push_back(std::make_unique<FrameUnit>(candidate.get(), *unit, candidate->definitions.at(unit->creatureId())));
  candidate->storage.back()->bindSession();
 }
 candidate->complete = true;
 const bool firstRound = candidate->round == 0;
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  candidate->sides.at(side).castSpells = 0;
  auto & counter = candidate->sides.at(side).enchanterCounter;
  counter = counter > 0 ? counter - 1 : 0;
 }
 ++candidate->round;
 const auto diagnosticMaxima = [&]
 {
  auto diagnostic = candidate->fork();
  std::vector<uint32_t> result;
  for(const auto & unit : diagnostic->storage) result.push_back(unit->getMaxHealth());
  return result;
 };
 const auto maxima = diagnosticMaxima();
 const auto verifyMaxima = [&]
 {
  if(diagnosticMaxima() != maxima)
   throw std::invalid_argument("Owned round changes maximum health");
 };
 for(const auto & unit : candidate->storage)
 {
  if(!firstRound) candidate->expireBonuses(unit->unitId(), OwnedBonusExpiry::Round);
  verifyMaxima();
  unit->afterNewRound();
  candidate->synchronize();
  verifyMaxima();
 }
 auto publication = prepareDefinitionPublication(candidate->model);
 static_assert(std::is_nothrow_copy_assignable_v<battle::CUnitState>);
 static_assert(noexcept(std::declval<FrameUnit &>().restoreBonusCaches(std::declval<const battle::CUnitState::BonusCaches &>())));
 static_assert(std::is_nothrow_move_assignable_v<OwnedBattleBonusModel>);
 static_assert(noexcept(commitDefinitionPublication(std::move(publication))));
 std::vector<battle::CUnitState::BonusCaches> caches;
 for(const auto & unit : candidate->storage) caches.push_back(unit->captureBonusCaches());
 for(size_t i = 0; i < storage.size(); ++i)
 {
  storage[i]->battle::CUnitState::operator=(*candidate->storage[i]);
  storage[i]->restoreBonusCaches(caches[i]);
 }
 model = std::move(candidate->model);
 for(const auto & unit : storage) unit->bindSession();
 commitDefinitionPublication(std::move(publication));
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  sides.at(side).castSpells = candidate->sides.at(side).castSpells;
  sides.at(side).enchanterCounter = candidate->sides.at(side).enchanterCounter;
 }
 round = candidate->round;
}

void HypotheticFrameArena::removeBonuses(uint32_t id, const CSelector & selector)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 auto candidate = model.transactionCopy();
 if(candidate.removeBonuses(id, selector)) commitModel(std::move(candidate));
}

void HypotheticFrameArena::mutateLocalBonuses(uint32_t id, const std::vector<Bonus> & values, bool remove, bool forceAdd)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 synchronize();
 if(!find(id)) return;
 const auto event = makeBonusEvent(values);
 auto candidate = model.transactionCopy();
 bool changed = false;
 if(remove)
 {
  const auto count = candidate.bonusEventSize(event);
  for(size_t i = 0; i < count; ++i)
   changed = candidate.removeBonusEventItem(id, event, i) || changed;
 }
 else if(!values.empty()) changed = candidate.applyBonusEvent(id, event, forceAdd);
 if(changed) commitModel(std::move(candidate));
}

void HypotheticFrameArena::nextOrdinaryRound()
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 if(!nativeDefinitions) throw std::logic_error("Ordinary lifecycle requires native definition bindings");
 std::vector<FrameUnit *> living;
 for(const auto & unit : storage) if(unit->alive()) living.push_back(unit.get());
 for(auto * unit : living) unit->afterNewRound();
 synchronize();
}

void HypotheticFrameArena::nextOrdinaryTurn(uint32_t id, BattleUnitTurnReason reason)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 if(!nativeDefinitions) throw std::logic_error("Ordinary lifecycle requires native definition bindings");
 auto * unit = find(id);
 if(!unit) throw std::invalid_argument("Missing ordinary turn unit");
 removeBonuses(id, Bonus::UntilGetsTurn);
 unit->afterGetsTurn(reason);
 synchronize();
}
