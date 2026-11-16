/*
 * StackWithBonuses.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/OwnedStackEffect.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/battle/actions/NoTargetActions.h"
#include "StackWithBonuses.h"
#include "HypotheticFrameArena.h"
#include "OrdinaryUnitViewStore.h"
#include "../../lib/battle/BattleInfo.h"

#include <vcmi/events/EventBus.h>

#include "../../lib/battle/BattleLayout.h"
#include "../../lib/CStack.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"

void actualizeEffect(TBonusListPtr target, const Bonus & ef)
{
	for(auto & bonus : *target) //TODO: optimize
	{
		if(bonus->source == BonusSource::SPELL_EFFECT && bonus->type == ef.type && bonus->subtype == ef.subtype)
		{
			if(bonus->turnsRemain < ef.turnsRemain)
			{
				bonus.reset(new Bonus(*bonus));

				bonus->turnsRemain = ef.turnsRemain;
			}
		}
	}
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack)
	: battle::CUnitState(),
	origBearer(Stack->getBonusBearer()),
	owner(Owner),
	type(Stack->unitType()),
	baseAmount(Stack->unitBaseAmount()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	localInit(Owner);

	battle::CUnitState::operator=(*Stack);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack)
	: battle::CUnitState(),
	origBearer(Stack->getBonusBearer()),
	owner(Owner),
	type(Stack->unitType()),
	baseAmount(Stack->unitBaseAmount()),
	id(Stack->unitId()),
	side(Stack->unitSide()),
	player(Stack->unitOwner()),
	slot(Stack->unitSlot()),
	treeVersionLocal(0)
{
	localInit(Owner);

	auto state = Stack->acquireState();
	battle::CUnitState::operator=(*state);
}

StackWithBonuses::StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info)
	: battle::CUnitState(),
	origBearer(nullptr),
	owner(Owner),
	baseAmount(info.count),
	id(info.id),
	side(info.side),
	slot(SlotID::SUMMONED_SLOT_PLACEHOLDER),
	treeVersionLocal(0)
{
	type = info.type.toCreature();
	origBearer = type->getBonusBearer();

	player = Owner->getSidePlayer(side);

	localInit(Owner);

	position = info.position;
	summoned = info.summoned;
}

StackWithBonuses::StackWithBonuses(const Creature * Type, uint32_t amount, uint32_t Id,
 BattleSide Side, PlayerColor player_, SlotID Slot)
 : treeVersionLocal(0), origBearer(nullptr), owner(nullptr), type(Type),
 baseAmount(amount), id(Id), side(Side), player(player_), slot(Slot) {}

StackWithBonuses::~StackWithBonuses() = default;

StackWithBonuses & StackWithBonuses::operator=(const battle::CUnitState & other)
{
	battle::CUnitState::operator=(other);
	return *this;
}

const Creature * StackWithBonuses::unitType() const
{
	return type;
}

int32_t StackWithBonuses::unitBaseAmount() const
{
	return baseAmount;
}

uint32_t StackWithBonuses::unitId() const
{
	return id;
}

BattleSide StackWithBonuses::unitSide() const
{
	return side;
}

PlayerColor StackWithBonuses::unitOwner() const
{
	return player;
}

SlotID StackWithBonuses::unitSlot() const
{
	return slot;
}

TConstBonusListPtr StackWithBonuses::getAllBonuses(const CSelector & selector, const std::string & cachingStr) const
{
	auto ret = std::make_shared<BonusList>();
	TConstBonusListPtr originalList = origBearer->getAllBonuses(selector, cachingStr);

	vstd::copy_if(*originalList, std::back_inserter(*ret), [this](const std::shared_ptr<Bonus> & b)
	{
		return !vstd::contains(bonusesToRemove, b);
	});


	for(const Bonus & bonus : bonusesToUpdate)
	{
		if(selector(&bonus))
		{
			if(ret->getFirst(Selector::source(BonusSource::SPELL_EFFECT, bonus.sid).And(Selector::typeSubtype(bonus.type, bonus.subtype))))
			{
				actualizeEffect(ret, bonus);
			}
			else
			{
				auto b = std::make_shared<Bonus>(bonus);
				ret->push_back(b);
			}
		}
	}

	for(auto & bonus : bonusesToAdd)
	{
		auto b = std::make_shared<Bonus>(bonus);
		if(selector(b.get()))
			ret->push_back(b);
	}
	//TODO limiters?
	return ret;
}

int32_t StackWithBonuses::getTreeVersion() const
{
	auto result = owner->getTreeVersion();

	if(bonusesToAdd.empty() && bonusesToUpdate.empty() && bonusesToRemove.empty())
		return result;
	else
		return result + treeVersionLocal;
}

void StackWithBonuses::addUnitBonus(const std::vector<Bonus> & bonus)
{
 if(mutateLocalBonuses(LocalBonusMutation::Add, bonus)) return;
 if(hasOwnedFrame()) throw std::logic_error("Owned frame bonus import is not implemented");
	vstd::concatenate(bonusesToAdd, bonus);
	treeVersionLocal++;
}

void StackWithBonuses::updateUnitBonus(const std::vector<Bonus> & bonus)
{
 if(mutateLocalBonuses(LocalBonusMutation::Update, bonus)) return;
 if(hasOwnedFrame()) throw std::logic_error("Owned frame bonus import is not implemented");
	//TODO: optimize, actualize to last value

	vstd::concatenate(bonusesToUpdate, bonus);
	treeVersionLocal++;
}

void StackWithBonuses::removeUnitBonus(const std::vector<Bonus> & bonus)
{
 if(mutateLocalBonuses(LocalBonusMutation::Remove, bonus)) return;
 if(hasOwnedFrame()) throw std::logic_error("Owned frame bonus import is not implemented");
	for(auto & one : bonus)
	{
		CSelector selector([&one](const Bonus * b) -> bool
		{
			//compare everything but turnsRemain, limiter and propagator
			return one.duration == b->duration
				&& one.type == b->type
				&& one.subtype == b->subtype
				&& one.source == b->source
				&& one.val == b->val
				&& one.sid == b->sid
				&& one.valType == b->valType
				&& one.effectRange == b->effectRange;
		});

		removeUnitBonus(selector);
	}
}

void StackWithBonuses::removeUnitBonus(const CSelector & selector)
{
 if(removeLocalBonuses(selector)) return;
 if(hasOwnedFrame()) throw std::logic_error("Owned frame bonus import is not implemented");
	TConstBonusListPtr toRemove = origBearer->getBonuses(selector);

	for(auto b : *toRemove)
		bonusesToRemove.insert(b);

	vstd::erase_if(bonusesToAdd, [&](const Bonus & b){return selector(&b);});
	vstd::erase_if(bonusesToUpdate, [&](const Bonus & b){return selector(&b);});

	treeVersionLocal++;
}

std::string StackWithBonuses::getDescription() const
{
	std::ostringstream oss;
	oss << unitOwner().toString();
	oss << " battle stack [" << unitId() << "]: " << getCount() << " of ";
	if(type)
		oss << type->getJsonKey();
	else
		oss << "[UNDEFINED TYPE]";

	oss << " from slot " << slot;

	return oss.str();
}

void StackWithBonuses::spendMana(ServerCallback * server, const int spellCost) const
{
	//TODO: evaluate cast use
}

HypotheticBattle::HypotheticBattle(const Environment * ENV, Subject realBattle)
	: BattleProxy(realBattle),
	env(ENV),
	bonusTreeVersion(1)
{
	auto activeUnit = realBattle->battleActiveUnit();
	activeUnitId = activeUnit ? activeUnit->unitId() : -1;

	nextId = 0x00F00000;

	const auto * parent = dynamic_cast<const HypotheticBattle *>(realBattle->getBattle());
	if(parent && parent->ordinaryStore)
		ordinaryStore = parent->ordinaryStore->fork();
	else if(const auto * native = dynamic_cast<const BattleInfo *>(realBattle->getBattle()))
		ordinaryStore = OrdinaryUnitViewStore::captureOrdinary(*native);
	else
		for(const auto * unit : realBattle->getBattle()->getUnitsIf([](const battle::Unit *) { return true; }))
			unitOrder.push_back(unit->unitId());

	localEnvironment.reset(new HypotheticEnvironment(this, env));
	serverCallback.reset(new HypotheticServerCallback(this));
}

bool HypotheticBattle::unitHasAmmoCart(const battle::Unit * unit) const
{
 if(ordinaryStore) return ordinaryStore->unitHasAmmoCart(unit);
 if(ownedArena) return ownedArena->unitHasAmmoCart(unit);
	//FIXME: check ammocart alive state here
	return false;
}

PlayerColor HypotheticBattle::unitEffectiveOwner(const battle::Unit * unit) const
{
 if(ordinaryStore) return ordinaryStore->unitEffectiveOwner(unit);
 if(ownedArena) return ownedArena->unitEffectiveOwner(unit);
	return battleGetOwner(unit);
}

std::shared_ptr<StackWithBonuses> HypotheticBattle::getForUpdate(uint32_t id)
{
 if(ordinaryStore)
 {
  auto value = ordinaryStore->getForUpdate(id);
  if(value) stackStates.insert_or_assign(id, value);
  return value;
 }
 if(ownedArena) return ownedArena->unit(id);
	auto iter = stackStates.find(id);

	if(iter == stackStates.end())
	{
		const battle::Unit * s = subject->battleGetUnitByID(id);

		auto ret = std::make_shared<StackWithBonuses>(this, s);
		stackStates[id] = ret;
		return ret;
	}
	else
	{
		return iter->second;
	}
}

std::shared_ptr<StackWithBonuses> HypotheticBattle::copyUnitState(uint32_t id, const battle::CUnitState & state)
{
	auto unit = getForUpdate(id);
	*unit = state;
	return unit;
}

void HypotheticBattle::damageUnit(uint32_t id, int64_t & amount)
{
	getForUpdate(id)->damage(amount);
}

void HypotheticBattle::recordUnitAttack(uint32_t id, bool ranged, bool counter)
{
	getForUpdate(id)->afterAttack(ranged, counter);
}

battle::Units HypotheticBattle::getUnitsIf(const battle::UnitFilter & predicate) const
{
 if(ordinaryStore) return ordinaryStore->units(predicate);
 if(ownedArena) return ownedArena->units(predicate);
	battle::Units result;
	result.reserve(unitOrder.size());
	for(uint32_t id : unitOrder)
	{
		auto changed = stackStates.find(id);
		const battle::Unit * unit = changed != stackStates.end()
			? changed->second.get() : subject->battleGetUnitByID(id);
		if(predicate(unit))
			result.push_back(unit);
	}
	return result;
}

BattleID HypotheticBattle::getBattleID() const
{
 if(ownedArena) return ownedBattleID;
	return subject->getBattle()->getBattleID();
}

int32_t HypotheticBattle::getActiveStackID() const
{
	return activeUnitId;
}

void HypotheticBattle::nextRound()
{
 if(ordinaryStore) { ordinaryStore->nextRound(); return; }
 if(ownedArena) { ownedArena->nextRound(); return; }
	//TODO:HypotheticBattle::nextRound
	for(auto unit : battleAliveUnits())
	{
		auto forUpdate = getForUpdate(unit->unitId());
		//TODO: update Bonus::NTurns effects
		forUpdate->afterNewRound();
	}
}

void HypotheticBattle::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
 if(ordinaryStore)
 {
  ordinaryStore->nextTurn(unitId, reason);
  activeUnitId = unitId;
  getForUpdate(unitId);
  return;
 }
 if(ownedArena)
 {
  BattleSetActiveStack packet;
  packet.battleID = getBattleID();
  packet.stack = unitId;
  packet.reason = reason;
  applyOwnedStatePacket(packet);
  return;
 }
	activeUnitId = unitId;
	auto unit = getForUpdate(unitId);

	unit->removeUnitBonus(Bonus::UntilGetsTurn);

	unit->afterGetsTurn(reason);
}

void HypotheticBattle::addUnit(uint32_t id, const JsonNode & data)
{
 if(ordinaryStore)
 {
  ordinaryStore->addUnit(id, data);
  getForUpdate(id);
  return;
 }
 if(ownedArena) { ownedArena->addUnit(id, data); return; }
	battle::UnitInfo info;
	info.load(id, data);
	auto newUnit = std::make_shared<StackWithBonuses>(this, info);
	stackStates[newUnit->unitId()] = newUnit;
	unitOrder.push_back(newUnit->unitId());
}

void HypotheticBattle::moveUnit(uint32_t id, const BattleHex & destination)
{
 if(ordinaryStore) { ordinaryStore->moveUnit(id, destination); getForUpdate(id); return; }
 if(ownedArena) { ownedArena->moveUnit(id, destination); return; }
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);
	changed->position = destination;
}

void HypotheticBattle::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
 if(ordinaryStore) { ordinaryStore->updateUnit(id, data, healthDelta); getForUpdate(id); return; }
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement updateUnit");
	std::shared_ptr<StackWithBonuses> changed = getForUpdate(id);

	changed->load(data);

	if(healthDelta < 0)
	{
		changed->removeUnitBonus(Bonus::UntilBeingAttacked);
	}
}

void HypotheticBattle::removeUnit(uint32_t id)
{
 if(ordinaryStore) { ordinaryStore->removeUnit(id); getForUpdate(id); return; }
 if(ownedArena) { ownedArena->removeUnit(id); return; }
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();

		auto toRemove = getForUpdate(toRemoveId);

		if(!toRemove->ghost)
		{
			toRemove->onRemoved();

			//TODO: emulate detachFromAll() somehow

			//stack may be removed instantly (not being killed first)
			//handle clone remove also here
			if(toRemove->cloneID >= 0)
			{
				ids.insert(toRemove->cloneID);
				toRemove->cloneID = -1;
			}

			for(const auto * unit : getUnitsIf([](const battle::Unit *) { return true; }))
			{
				if(unit->acquireState()->cloneID == toRemoveId)
					getForUpdate(unit->unitId())->cloneID = -1;
			}
		}

		ids.erase(toRemoveId);
	}
}

void HypotheticBattle::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement addUnitBonus");
	getForUpdate(id)->addUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement updateUnitBonus");
	getForUpdate(id)->updateUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement removeUnitBonus");
	getForUpdate(id)->removeUnitBonus(bonus);
	bonusTreeVersion++;
}

void HypotheticBattle::setWallState(EWallPart partOfWall, EWallState state)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement setWallState");
	//TODO:HypotheticBattle::setWallState
}

void HypotheticBattle::addObstacle(const ObstacleChanges & changes)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement addObstacle");
	//TODO:HypotheticBattle::addObstacle
}

void HypotheticBattle::updateObstacle(const ObstacleChanges& changes)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement updateObstacle");
	//TODO:HypotheticBattle::updateObstacle
}

void HypotheticBattle::removeObstacle(uint32_t id)
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement removeObstacle");
	//TODO:HypotheticBattle::removeObstacle
}

uint32_t HypotheticBattle::nextUnitId() const
{
 if(ordinaryStore) return ordinaryStore->nextUnitId();
	return nextId++;
}

int64_t HypotheticBattle::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	return (damage.min + damage.max) / 2;
}

std::vector<SpellID> HypotheticBattle::getUsedSpells(BattleSide side) const
{
 if(ownedArena) return ownedArena->sides.at(side).usedSpells;
	// TODO
	return {};
}

int3 HypotheticBattle::getLocation() const
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getLocation");
	// TODO
	return int3(-1, -1, -1);
}

BattleLayout HypotheticBattle::getLayout() const
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getLayout");
	return subject->getBattle()->getLayout();
}

int32_t HypotheticBattle::getTreeVersion() const
{
 if(ordinaryStore) return ordinaryStore->revision();
 if(ownedArena) return ownedArena->revision();
	return getBonusBearer()->getTreeVersion() + bonusTreeVersion;
}

ServerCallback * HypotheticBattle::getServerCallback()
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getServerCallback");
	return serverCallback.get();
}

const scripting::Pool & HypotheticBattle::getScriptContextPool() const
{
 if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getScriptContextPool");
	return subject->getBattle()->getScriptContextPool();
}

void HypotheticBattle::makeWait(const battle::Unit * activeStack)
{
	auto unit = getForUpdate(activeStack->unitId());

	resetActiveUnit();
	unit->setTurnStatus(battle::UnitTurnStatus::WAITING);
}

HypotheticBattle::HypotheticServerCallback::HypotheticServerCallback(HypotheticBattle * owner_)
	:owner(owner_)
{
}

void HypotheticBattle::HypotheticServerCallback::complain(const std::string & problem)
{
	logAi->error(problem);
}

bool HypotheticBattle::HypotheticServerCallback::describeChanges() const
{
	return false;
}

vstd::RNG * HypotheticBattle::HypotheticServerCallback::getRNG()
{
	return &rngStub;
}

bool HypotheticBattle::HypotheticServerCallback::rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int percentageChance)
{
	// same averaging the RNG stub does - the hypothetical battle must stay deterministic
	return percentageChance >= 50;
}

void HypotheticBattle::HypotheticServerCallback::apply(CPackForClient & pack)
{
	logAi->error("Package of type %s is not allowed in battle evaluation", typeid(pack).name());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleLogMessage & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleStackMoved & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleUnitsChanged & pack)
{
 if(owner->ordinaryStore)
 {
  if(pack.battleID != owner->getBattleID())
   throw std::invalid_argument("Foreign ordinary unit packet battle");
  owner->ordinaryStore->applyStateUpdates(pack);
  for(const auto & change : pack.changedStacks) owner->getForUpdate(change.id);
  return;
 }
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(SetStackEffect & pack)
{
 if(owner->ordinaryStore)
 {
  if(pack.battleID != owner->getBattleID())
   throw std::invalid_argument("Foreign ordinary effect battle");
  owner->ordinaryStore->applyStackEffect(pack);
  for(const auto & row : pack.toRemove) owner->getForUpdate(row.first);
  for(const auto & row : pack.toUpdate) owner->getForUpdate(row.first);
  for(const auto & row : pack.toAdd) owner->getForUpdate(row.first);
  return;
 }
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(StacksInjured & pack)
{
 if(owner->ordinaryStore)
 {
  if(pack.battleID != owner->getBattleID())
   throw std::invalid_argument("Foreign ordinary injury packet battle");
  owner->ordinaryStore->applyStateUpdates(pack);
  for(const auto & stack : pack.stacks) owner->getForUpdate(stack.newState.id);
  return;
 }
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleObstaclesChanged & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(CatapultAttack & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

HypotheticBattle::HypotheticEnvironment::HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment)
	: owner(owner_),
	env(upperEnvironment)
{

}

const Services * HypotheticBattle::HypotheticEnvironment::services() const
{
	return env->services();
}

const Environment::BattleCb * HypotheticBattle::HypotheticEnvironment::battle(const BattleID & battleID) const
{
	assert(battleID == owner->getBattleID());
	return owner;
}

const Environment::GameCb * HypotheticBattle::HypotheticEnvironment::game() const
{
	return env->game();
}

HypotheticBattle::HypotheticBattle(std::shared_ptr<HypotheticFrameArena> arena, int32_t active, BattleID battleID)
 : BattleProxy(nullptr), env(nullptr), ownedArena(std::move(arena)), ownedBattleID(battleID), bonusTreeVersion(1), activeUnitId(active), nextId(0x00F00000) {}
std::shared_ptr<HypotheticBattle> HypotheticBattle::captureOwnedFixture(const BattleInfo & battle)
{ return std::shared_ptr<HypotheticBattle>(new HypotheticBattle(HypotheticFrameArena::capture(battle), battle.getActiveStackID(), battle.getBattleID())); }
std::shared_ptr<HypotheticBattle> HypotheticBattle::forkOwnedFixture() const
{
 if(!ownedArena) throw std::logic_error("Owned parent frame required");
 return std::shared_ptr<HypotheticBattle>(new HypotheticBattle(ownedArena->fork(), activeUnitId, ownedBattleID));
}

std::optional<PlayerColor> HypotheticBattle::getPlayerID() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getPlayerID");
 return BattleProxy::getPlayerID(); }

TStacks HypotheticBattle::getStacksIf(const TStackFilter & predicate) const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getStacksIf");
 return BattleProxy::getStacksIf(predicate); }

BattleField HypotheticBattle::getBattlefieldType() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getBattlefieldType");
 return BattleProxy::getBattlefieldType(); }

TerrainId HypotheticBattle::getTerrainType() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getTerrainType");
 return BattleProxy::getTerrainType(); }

IBattleInfo::ObstacleCList HypotheticBattle::getAllObstacles() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getAllObstacles");
 return BattleProxy::getAllObstacles(); }

PlayerColor HypotheticBattle::getSidePlayer(BattleSide side) const
{ if(ownedArena) return ownedArena->sidePlayer(side);
 return BattleProxy::getSidePlayer(side); }

const CArmedInstance * HypotheticBattle::getSideArmy(BattleSide side) const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getSideArmy");
 return BattleProxy::getSideArmy(side); }

const CGHeroInstance * HypotheticBattle::getSideHero(BattleSide side) const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getSideHero");
 return BattleProxy::getSideHero(side); }

ui8 HypotheticBattle::getTacticDist() const
{ if(ownedArena) return ownedArena->tacticDistance;
 return BattleProxy::getTacticDist(); }

BattleSide HypotheticBattle::getTacticsSide() const
{ if(ownedArena) return ownedArena->tacticsSide;
 return BattleProxy::getTacticsSide(); }

int32_t HypotheticBattle::getRound() const
{ if(ownedArena) return ownedArena->round;
 return BattleProxy::getRound(); }

const CGTownInstance * HypotheticBattle::getDefendedTown() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getDefendedTown");
 return BattleProxy::getDefendedTown(); }

EWallState HypotheticBattle::getWallState(EWallPart partOfWall) const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getWallState");
 return BattleProxy::getWallState(partOfWall); }

EGateState HypotheticBattle::getGateState() const
{ if(ownedArena) throw std::logic_error("Owned fixture facade does not implement getGateState");
 return BattleProxy::getGateState(); }

int32_t HypotheticBattle::getCastSpells(BattleSide side) const
{ if(ownedArena) return ownedArena->sides.at(side).castSpells;
 return BattleProxy::getCastSpells(side); }

int32_t HypotheticBattle::getEnchanterCounter(BattleSide side) const
{ if(ownedArena) return ownedArena->sides.at(side).enchanterCounter;
 return BattleProxy::getEnchanterCounter(side); }

const IBonusBearer * HypotheticBattle::getBonusBearer() const
{ if(ordinaryStore) return ordinaryStore->getBonusBearer();
 if(ownedArena) return ownedArena.get();
 return BattleProxy::getBonusBearer(); }

OwnedLogicalBonusEvent HypotheticBattle::makeOwnedBonusEvent(const std::vector<Bonus> & values) const
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require owned fixture mode");
 return ownedArena->makeBonusEvent(values);
}
OwnedLogicalBonusEvent HypotheticBattle::ownedEventFromUnitExports(uint32_t id, const CSelector & selector) const
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require owned fixture mode");
 return ownedArena->eventFromUnitExports(id, selector);
}
void HypotheticBattle::addUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event)
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require owned fixture mode");
 ownedArena->applyBonusEvent(id, event, true);
}
void HypotheticBattle::updateUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event)
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require owned fixture mode");
 ownedArena->applyBonusEvent(id, event, false);
}

void HypotheticBattle::removeUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event)
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require an owned frame");
 ownedArena->removeBonusEvent(id, event);
}
OwnedStackEffect HypotheticBattle::makeOwnedScalarStackEffect(const SetStackEffect & effect) const
{
 if(!ownedArena) throw std::logic_error("Scalar packet certification requires an owned frame");
 if(effect.battleID != getBattleID()) throw std::invalid_argument("Foreign scalar effect battle");
 const auto validate = [](const auto & entries)
 {
  for(const auto & [id, values] : entries)
   for(const auto & value : values)
    if(value.limiter || value.updater || value.propagator || value.propagationUpdater || value.parameters)
     throw std::invalid_argument("Scalar effect packet contains a bonus component");
 };
 validate(effect.toRemove);
 validate(effect.toUpdate);
 validate(effect.toAdd);
 OwnedStackEffect result;
 const auto convert = [this](const auto & input, auto & output)
 {
  output.reserve(input.size());
  for(const auto & [id, values] : input)
   output.emplace_back(id, makeOwnedBonusEvent(values));
 };
 convert(effect.toRemove, result.toRemove);
 convert(effect.toUpdate, result.toUpdate);
 convert(effect.toAdd, result.toAdd);
 return result;
}
void HypotheticBattle::applyOwnedStackEffect(const OwnedStackEffect & effect)
{
 if(!ownedArena) throw std::logic_error("Typed bonus events require an owned frame");
 for(const auto & [id, event] : effect.toRemove) removeUnitBonus(id, event);
 for(const auto & [id, event] : effect.toUpdate) updateUnitBonus(id, event);
 for(const auto & [id, event] : effect.toAdd) addUnitBonus(id, event);
}
int HypotheticBattle::applyOwnedDefendBonuses(uint32_t id)
{
 if(!ownedArena) throw std::logic_error("Owned defend effects require an owned frame");
 const auto unit = ownedArena->unit(id);
 if(!unit) throw std::out_of_range("Missing owned defend unit");
 const auto effects = makeDefendBonusEffects(*unit);
 updateUnitBonus(id, makeOwnedBonusEvent(effects.bonuses));
 return effects.defenseIncrease;
}


void HypotheticBattle::expireOwnedRoundBonuses(bool firstRound)
{
 if(!ownedArena) throw std::logic_error("Owned expiry requires an owned frame");
 if(firstRound) return;
 for(const auto * unit : ownedArena->units([](const battle::Unit *) { return true; }))
  ownedArena->expireBonuses(unit->unitId(), OwnedBonusExpiry::Round);
}
void HypotheticBattle::expireOwnedTurnBonuses(uint32_t id, BattleUnitTurnReason reason)
{
 if(!ownedArena) throw std::logic_error("Owned expiry requires an owned frame");
 switch(reason)
 {
  case BattleUnitTurnReason::TURN_QUEUE:
  case BattleUnitTurnReason::MORALE:
  case BattleUnitTurnReason::HERO_SPELLCAST:
  case BattleUnitTurnReason::AUTOMATIC_ACTION: break;
  case BattleUnitTurnReason::UNIT_SPELLCAST: return;
  default: throw std::invalid_argument("Unsupported owned turn reason");
 }
 ownedArena->expireBonuses(id, OwnedBonusExpiry::GetsTurn);
}
void HypotheticBattle::expireOwnedAttackBonuses(uint32_t id, bool counter)
{
 if(!ownedArena) throw std::logic_error("Owned expiry requires an owned frame");
 ownedArena->expireBonuses(id, OwnedBonusExpiry::Attack);
 if(!counter) ownedArena->expireBonuses(id, OwnedBonusExpiry::OwnAttack);
}
void HypotheticBattle::expireOwnedDamageBonuses(uint32_t id, int64_t healthDelta)
{
 if(!ownedArena) throw std::logic_error("Owned expiry requires an owned frame");
 if(healthDelta < 0) ownedArena->expireBonuses(id, OwnedBonusExpiry::BeingAttacked);
}

OwnedStackEffect HypotheticBattle::makeOwnedFreshStackEffect(const OwnedFreshStackEffect & effect) const
{
 if(!ownedArena) throw std::logic_error("Fresh component certification requires an owned frame");
 if(effect.battleID != getBattleID()) throw std::invalid_argument("Foreign fresh effect battle");
 std::vector<TUpdaterPtr> updaters;
 for(const auto & value : effect.updaters)
 {
  if(value.stepSize <= 0 || value.minimum > value.maximum)
   throw std::invalid_argument("Invalid fresh stack-size updater");
  for(const auto endpoint : {value.minimum, value.maximum})
  {
   const auto product = static_cast<int64_t>(endpoint) * value.stepValue;
   if(product < std::numeric_limits<int>::min() || product > std::numeric_limits<int>::max())
    throw std::invalid_argument("Fresh stack-size updater product overflows int");
  }
  updaters.push_back(std::make_shared<TimesStackSizeUpdater>(value.minimum, value.maximum, value.stepSize, value.stepValue));
 }
 std::vector<TLimiterPtr> limiters;
 for(const auto & value : effect.limiters)
 {
  if(value.minimum > value.maximum) throw std::invalid_argument("Invalid fresh creature-level limiter");
  limiters.push_back(std::make_shared<CreatureLevelLimiter>(value.minimum, value.maximum));
 }
 std::vector<TPropagatorPtr> propagators;
 for(const auto type : effect.propagators)
 {
  switch(type)
  {
   case BonusNodeType::STACK_INSTANCE: case BonusNodeType::STACK_BATTLE:
   case BonusNodeType::ARMY: case BonusNodeType::ARTIFACT: case BonusNodeType::CREATURE:
   case BonusNodeType::ARTIFACT_INSTANCE: case BonusNodeType::HERO: case BonusNodeType::PLAYER:
   case BonusNodeType::TEAM: case BonusNodeType::TOWN_AND_VISITOR: case BonusNodeType::BATTLE_WIDE:
   case BonusNodeType::COMMANDER: case BonusNodeType::GLOBAL_EFFECTS: case BonusNodeType::BOAT:
   case BonusNodeType::TOWN: break;
   default: throw std::invalid_argument("Invalid fresh propagator node type");
  }
  propagators.push_back(std::make_shared<CPropagatorNodeType>(type));
 }
 std::vector<std::vector<Bonus>> groups;
 const auto collect = [&](const auto & entries)
 {
  for(const auto & [id, values] : entries)
  {
   auto & group = groups.emplace_back();
   for(const auto & value : values)
   {
    const auto & scalar = value.scalar;
    if(scalar.updater || scalar.propagationUpdater || scalar.limiter || scalar.propagator || scalar.parameters)
     throw std::invalid_argument("Fresh effect scalar contains a native component handle");
    auto & bonus = group.emplace_back(scalar);
    if(value.updater) bonus.updater = updaters.at(*value.updater);
    if(value.propagationUpdater) bonus.propagationUpdater = updaters.at(*value.propagationUpdater);
    if(value.limiter) bonus.limiter = limiters.at(*value.limiter);
    if(value.propagator) bonus.propagator = propagators.at(*value.propagator);
   }
  }
 };
 collect(effect.toRemove);
 collect(effect.toUpdate);
 collect(effect.toAdd);
 const auto events = ownedArena->importComponentEvents(groups);
 OwnedStackEffect result;
 size_t index = 0;
 const auto convert = [&](const auto & input, auto & output)
 {
  output.reserve(input.size());
  for(const auto & [id, values] : input) output.emplace_back(id, events.at(index++));
 };
 convert(effect.toRemove, result.toRemove);
 convert(effect.toUpdate, result.toUpdate);
 convert(effect.toAdd, result.toAdd);
 return result;
}

void HypotheticBattle::expireOwnedIndirectDamageBonuses(const std::vector<uint32_t> & ids)
{
 if(!ownedArena) throw std::logic_error("Indirect expiry requires an owned frame");
 for(auto id : ids) ownedArena->expireBonuses(id, OwnedBonusExpiry::IndirectDamage);
}

OwnedStackEffect HypotheticBattle::captureOwnedScalarSequenceCleanup(const std::vector<uint32_t> & ids) const
{
 if(!ownedArena) throw std::logic_error("Sequence cleanup requires an owned frame");
 SetStackEffect packet;
 packet.battleID = getBattleID();
 for(auto id : ids)
 {
  const auto unit = ownedArena->unit(id);
  if(!unit) throw std::invalid_argument("Missing sequence cleanup unit");
  const auto bonuses = unit->getAllBonuses(Bonus::untilAfterAttackSequence);
  std::vector<Bonus> values;
  for(const auto & bonus : *bonuses) values.push_back(*bonus);
  if(!values.empty()) packet.toRemove.emplace_back(id, std::move(values));
 }
 return makeOwnedScalarStackEffect(packet);
}

void HypotheticBattle::applyOwnedStatePacket(CPackForClient & packet)
{
 if(!ownedArena) throw std::logic_error("State packet dispatch requires an owned frame");
 if(typeid(packet) == typeid(StartAction))
 {
  const auto & value = static_cast<const StartAction &>(packet);
  if(value.battleID != getBattleID()) throw std::invalid_argument("Foreign action packet battle");
  if(!value.turnStatus && !value.castSpellThisTurn) return;
  if(value.turnStatus)
   switch(*value.turnStatus)
   {
    case battle::UnitTurnStatus::READY:
    case battle::UnitTurnStatus::WAITING:
    case battle::UnitTurnStatus::ACTED:
    case battle::UnitTurnStatus::DEFENDED: break;
    default: throw std::invalid_argument("Unsupported owned turn status");
   }
  auto unit = ownedArena->unit(value.ba.stackNumber);
  if(!unit || !unit->alive()) throw std::invalid_argument("StartAction requires a living unit for turn changes");
  // These setters do not allocate or consult hero/server/action execution context.
  if(value.turnStatus) unit->setTurnStatus(*value.turnStatus);
  if(value.castSpellThisTurn) unit->setCastSpellThisTurn(true);
  return;
 }
 if(typeid(packet) == typeid(BattleSpellCast))
 {
  const auto & value = static_cast<const BattleSpellCast &>(packet);
  if(value.battleID != getBattleID()) throw std::invalid_argument("Foreign spell packet battle");
  if(!value.castByHero || value.side == BattleSide::NONE) return;
  if(value.side != BattleSide::ATTACKER && value.side != BattleSide::DEFENDER)
   throw std::invalid_argument("Unsupported owned spell side");
  auto & side = ownedArena->sides.at(value.side);
  // Append first: allocation failure must leave both history and counter unchanged.
  side.usedSpells.push_back(value.spellID);
  ++side.castSpells;
  return;
 }
 if(typeid(packet) == typeid(BattleTacticsEnded))
 {
  const auto & value = static_cast<const BattleTacticsEnded &>(packet);
  if(value.battleID != getBattleID()) throw std::invalid_argument("Foreign tactics packet battle");
  ownedArena->tacticDistance = 0;
  return;
 }
 if(typeid(packet) == typeid(BattleNextRound))
 {
  const auto & value = static_cast<const BattleNextRound &>(packet);
  if(value.battleID != getBattleID()) throw std::invalid_argument("Foreign round packet battle");
  ownedArena->nextRound();
  return;
 }
 if(typeid(packet) == typeid(BattleSetActiveStack))
 {
  auto & turn = static_cast<BattleSetActiveStack &>(packet);
  if(turn.battleID != getBattleID()) throw std::invalid_argument("Foreign state packet battle");
  auto unit = ownedArena->unit(turn.stack);
  if(!unit || !unit->alive()) throw std::invalid_argument("Active packet requires a living unit");
  // Expiry validates the reason before publishing. Remaining state changes cannot throw.
  expireOwnedTurnBonuses(turn.stack, turn.reason);
  activeUnitId = turn.stack;
  unit->afterGetsTurn(turn.reason);
  return;
 }
 if(typeid(packet) == typeid(BattleStackMoved))
 {
  auto & move = static_cast<BattleStackMoved &>(packet);
  if(move.battleID != getBattleID()) throw std::invalid_argument("Foreign state packet battle");
  if(move.tilesToMove.empty()) throw std::invalid_argument("Movement packet requires a destination");
  auto unit = ownedArena->unit(move.stack);
  // Native getStack uses onlyAlive=true for movement.
  if(!unit || !unit->alive()) return;
  BattleStatePackVisitor visitor(*this);
  move.visitTyped(visitor);
  return;
 }
 if(typeid(packet) == typeid(BattleUnitsChanged))
 {
  auto & updates = static_cast<BattleUnitsChanged &>(packet);
  if(updates.battleID != getBattleID()) throw std::invalid_argument("Foreign state packet battle");
  ownedArena->applyStateUpdates(updates);
  return;
 }
 if(typeid(packet) == typeid(BattleAttack))
 {
  auto & attack = static_cast<BattleAttack &>(packet);
  if(attack.battleID != getBattleID() || attack.attackerChanges.battleID != getBattleID())
   throw std::invalid_argument("Foreign attack packet battle");
  ownedArena->applyStateUpdates(attack);
  return;
 }
 if(typeid(packet) == typeid(StacksInjured))
 {
  const auto & injured = static_cast<const StacksInjured &>(packet);
  if(injured.battleID != getBattleID()) throw std::invalid_argument("Foreign injury packet battle");
  ownedArena->applyStateUpdates(injured);
  return;
 }
 throw std::invalid_argument("Unsupported owned state packet");
}
