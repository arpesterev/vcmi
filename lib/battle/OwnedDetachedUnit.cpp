/* Owned detached unit proposal. GPL-2.0-or-later. */
#include "StdInc.h"
#include "OwnedDetachedUnit.h"
#include "../bonuses/OwnedBonusValueCopier.h"

OwnedUnitEnvironmentHandle::OwnedUnitEnvironmentHandle(std::shared_ptr<const battle::IUnitEnvironment> environment)
 : environment(std::move(environment))
{
 if(!this->environment)
  throw std::invalid_argument("Missing owned unit environment");
}

OwnedUnitEnvironmentSnapshot::OwnedUnitEnvironmentSnapshot(OwnedBattleBonusModel model)
 : model(std::move(model)) {}

OwnedUnitEnvironmentHandle OwnedUnitEnvironmentSnapshot::capture(OwnedBattleBonusModel model)
{
 return OwnedUnitEnvironmentHandle(std::shared_ptr<const OwnedUnitEnvironmentSnapshot>(
  new OwnedUnitEnvironmentSnapshot(std::move(model))));
}

bool OwnedUnitEnvironmentSnapshot::unitHasAmmoCart(const battle::Unit * unit) const
{
 if(!unit) throw std::invalid_argument("Missing ammunition query unit");
 return model.ammoForUnit(unit->unitId(), unit->unitSide(), unit->isHypnotized());
}

PlayerColor OwnedUnitEnvironmentSnapshot::unitEffectiveOwner(const battle::Unit * unit) const
{
 if(!unit) throw std::invalid_argument("Missing owner query unit");
 return model.ownerForUnit(unit->unitSide(), unit->isHypnotized());
}

namespace battle
{
OwnedDetachedUnit::OwnedDetachedUnit(Identity identity, PreviewCreatureBinding creature,
 std::shared_ptr<const FrozenBonuses> frozenBonuses, OwnedUnitEnvironmentHandle environment)
 : identity(identity), creature(std::move(creature)), frozenBonuses(std::move(frozenBonuses)), environment(std::move(environment))
{
 if(!this->creature || !this->frozenBonuses || !this->environment.get())
  throw std::invalid_argument("Missing owned detached unit storage");
 localInit(this->environment.get());
}

std::shared_ptr<OwnedDetachedUnit> OwnedDetachedUnit::capture(const CUnitState & state,
 OwnedBattleBonusModel model, uint32_t unitId, PreviewCreatureBinding creature,
 OwnedUnitEnvironmentHandle environment)
{
 if(!creature || state.unitId() != unitId || state.creatureId() != creature->getId())
  throw std::invalid_argument("Owned detached unit identity mismatch");
 const auto found = std::find_if(model.units().begin(), model.units().end(),
  [unitId](const auto & facts) { return facts.id == unitId; });
 if(found == model.units().end() || found->creature != creature->getId()
  || found->side != state.unitSide() || found->originalOwner != state.unitOwner())
  throw std::invalid_argument("Owned detached model identity mismatch");
 if(model.observeUnit(unitId, state) != *found)
  throw std::invalid_argument("Owned detached state was not synchronized with its model");
 const auto evaluated = model.queryBonuses(unitId, Selector::all);
 auto frozen = std::make_shared<FrozenBonuses>();
 OwnedBonusValueCopier captureCopies;
 for(const auto & bonus : *evaluated)
  frozen->push_back(captureCopies.copy(bonus));
 Identity identity{state.unitBaseAmount(), unitId, state.unitSide(), state.unitOwner(), state.unitSlot(), state.unitLevel()};
 auto result = std::shared_ptr<OwnedDetachedUnit>(new OwnedDetachedUnit(identity, std::move(creature), std::move(frozen), std::move(environment)));
 *result = state;
 return result;
}

OwnedDetachedUnit & OwnedDetachedUnit::operator=(const CUnitState & other)
{
 CUnitState::operator=(other);
 shots.setEnv(environment.get());
 return *this;
}

std::shared_ptr<Unit> OwnedDetachedUnit::acquire() const { return acquireState(); }

std::shared_ptr<CUnitState> OwnedDetachedUnit::acquireState() const
{
 auto result = std::shared_ptr<OwnedDetachedUnit>(new OwnedDetachedUnit(identity, creature, frozenBonuses, environment));
 *result = static_cast<const CUnitState &>(*this);
 return result;
}

TConstBonusListPtr OwnedDetachedUnit::getAllBonuses(const CSelector & selector, const std::string &) const
{
 BonusList copies;
 OwnedBonusValueCopier queryCopies;
 for(const auto & bonus : *frozenBonuses)
  copies.push_back(queryCopies.copy(bonus));
 auto result = std::make_shared<BonusList>();
 copies.getBonuses(*result, selector);
 return result;
}

void OwnedDetachedUnit::spendMana(ServerCallback *, int spellCost) const
{
 if(spellCost != 1)
  logGlobal->warn("Unexpected spell cost %d for creature", spellCost);
 const_cast<OwnedDetachedUnit *>(this)->casts.use(spellCost);
}
}
