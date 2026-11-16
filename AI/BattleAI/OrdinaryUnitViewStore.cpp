/* Preview-local store prototype. GPL-2.0-or-later. */
#include "StdInc.h"
#include "OrdinaryUnitViewStore.h"
#include "HypotheticFrameArena.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

OrdinaryUnitViewStore::OrdinaryUnitViewStore(std::shared_ptr<HypotheticFrameArena> value)
 : arena(std::move(value))
{
 if(!arena) throw std::invalid_argument("Missing preview store seed");
 arena->ordinaryStoreMutators = true;
}
std::shared_ptr<OrdinaryUnitViewStore> OrdinaryUnitViewStore::captureFixture(const BattleInfo & battle)
{
 std::set<uint32_t> ids;
 for(const auto * value : battle.getUnitsIf([](const battle::Unit *) { return true; }))
  if(!value || !dynamic_cast<const battle::CUnitState *>(value) || !ids.insert(value->unitId()).second)
   throw std::invalid_argument("Preview seed requires unique native unit states");
 return std::shared_ptr<OrdinaryUnitViewStore>(new OrdinaryUnitViewStore(HypotheticFrameArena::capture(battle)));
}
std::shared_ptr<OrdinaryUnitViewStore> OrdinaryUnitViewStore::captureOrdinary(const BattleInfo & battle)
{
 return std::shared_ptr<OrdinaryUnitViewStore>(new OrdinaryUnitViewStore(HypotheticFrameArena::captureOrdinary(battle)));
}
std::shared_ptr<OrdinaryUnitViewStore> OrdinaryUnitViewStore::fork() const
{
 auto result = std::shared_ptr<OrdinaryUnitViewStore>(new OrdinaryUnitViewStore(arena->fork()));
 result->nextId = nextId;
 return result;
}
std::shared_ptr<StackWithBonuses> OrdinaryUnitViewStore::unit(uint32_t id) const
{ return arena->unit(id); }
battle::Units OrdinaryUnitViewStore::units(const battle::UnitFilter & predicate) const
{ return arena->units(predicate); }
std::shared_ptr<StackWithBonuses> OrdinaryUnitViewStore::getForUpdate(uint32_t id)
{
 auto value = unit(id);
 if(value) stackStates.insert_or_assign(id, value);
 return value;
}
std::shared_ptr<StackWithBonuses> OrdinaryUnitViewStore::copyUnitState(uint32_t id, const battle::CUnitState & state)
{
 auto value = getForUpdate(id);
 if(value) value->battle::CUnitState::operator=(state);
 return value;
}
bool OrdinaryUnitViewStore::removeUnit(uint32_t id)
{ return arena->removeUnit(id); }
void OrdinaryUnitViewStore::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
 BattleUnitsChanged packet;
 UnitChanges change;
 change.id = id;
 change.operation = BattleChanges::EOperation::UPDATE;
 change.data = data;
 change.healthDelta = healthDelta;
 packet.changedStacks.push_back(std::move(change));
 arena->applyStateUpdates(packet);
}
void OrdinaryUnitViewStore::addUnit(const battle::UnitInfo & info)
{
 if(unit(info.id)) throw std::invalid_argument("Duplicate preview unit ID");
 auto definition = arena->definitions.find(info.type);
 if(definition == arena->definitions.end()) throw std::invalid_argument("Preview addition requires a captured creature");
 auto scalarInfo = info;
 scalarInfo.type = CreatureID::NONE;
 UnitChanges change(info.id, BattleChanges::EOperation::ADD);
 scalarInfo.save(change.data);
 change.data["type"] = JsonNode(definition->second->getJsonKey());
 BattleUnitsChanged packet;
 packet.changedStacks.push_back(std::move(change));
 arena->applyStateUpdates(packet);
 getForUpdate(info.id);
}
uint32_t OrdinaryUnitViewStore::nextUnitId() const
{
 while(unit(nextId))
 {
  if(nextId == std::numeric_limits<uint32_t>::max()) throw std::overflow_error("Preview unit IDs exhausted");
  ++nextId;
 }
 if(nextId == std::numeric_limits<uint32_t>::max()) throw std::overflow_error("Preview unit IDs exhausted");
 return nextId++;
}
std::weak_ptr<const battle::IUnitEnvironment> OrdinaryUnitViewStore::environmentLease() const
{ return std::shared_ptr<const battle::IUnitEnvironment>(arena); }
const IBonusBearer * OrdinaryUnitViewStore::getBonusBearer() const
{ return arena.get(); }
bool OrdinaryUnitViewStore::unitHasAmmoCart(const battle::Unit * value) const
{ return arena->unitHasAmmoCart(value); }
PlayerColor OrdinaryUnitViewStore::unitEffectiveOwner(const battle::Unit * value) const
{ return arena->unitEffectiveOwner(value); }

void OrdinaryUnitViewStore::addUnit(uint32_t id, const JsonNode & data)
{
 arena->addUnit(id, data);
 getForUpdate(id);
}
void OrdinaryUnitViewStore::moveUnit(uint32_t id, const BattleHex & destination)
{ arena->moveUnit(id, destination); }
void OrdinaryUnitViewStore::nextRound()
{ arena->nextOrdinaryRound(); }
void OrdinaryUnitViewStore::nextTurn(uint32_t id, BattleUnitTurnReason reason)
{ arena->nextOrdinaryTurn(id, reason); }
int32_t OrdinaryUnitViewStore::revision() const
{ return arena->revision(); }

void OrdinaryUnitViewStore::applyStackEffect(const SetStackEffect & effect)
{ arena->applyOrdinaryStackEffect(effect); }

void OrdinaryUnitViewStore::applyStateUpdates(const CPackForClient & packet)
{ arena->applyStateUpdates(packet); }
