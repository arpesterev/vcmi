/*
 * PlanningBattleSnapshot.cpp, part of VCMI engine
 * GPL-2.0-or-later; see license.txt.
 */
#include "../../StdInc.h"
#include "../../../../lib/callback/PlanningBattleTerrain.h"
#include "PlanningBattleSnapshot.h"
#include "../../../../lib/battle/BattleInfo.h"
#include "../../../../lib/battle/CObstacleInstance.h"
#include "../../../../lib/battle/Unit.h"
#include "../../../../lib/bonuses/Limiters.h"
#include "../../../../lib/bonuses/Updaters.h"
#include "../../../../lib/GameSettings.h"
#include "../../../../lib/json/JsonNode.h"
#include "../../../../lib/callback/CGameInfoCallback.h"
#include "../../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../../lib/mapping/TerrainTile.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../../lib/mapObjects/CGCreature.h"
#include <vcmi/spells/Spell.h>
#include "../../../../lib/spells/CSpellHandler.h"

namespace NK2AI::experimental
{
namespace
{
struct UnsupportedSnapshotQuery : std::logic_error
{
	UnsupportedSnapshotQuery() : std::logic_error("unsupported snapshot game-state access") {}
};
}

struct PlanningBattleSnapshot::Impl
{
	class SnapshotCallback final : public CGameInfoCallback
	{
		GameSettings settings;
		std::array<int, 5> dates{};
	public:
		TerrainTile tile;
		std::array<const CArmedInstance *, 2> armies{};
		explicit SnapshotCallback(const IGameInfoCallback & source, TerrainId terrain)
		{
			tile.terrainType = terrain;
			settings.loadBase(source.getSettings().getFullConfig());
			for(size_t index = 0; index < dates.size(); ++index)
				dates[index] = source.getDate(static_cast<Date>(index));
		}
		CGameState & gameState() override { throw UnsupportedSnapshotQuery(); }
		const CGameState & gameState() const override { throw UnsupportedSnapshotQuery(); }
		const IGameSettings & getSettings() const override { return settings; }
		int getDate(Date mode) const override { return dates.at(static_cast<size_t>(mode)); }
		const CGObjectInstance * getObjInstance(ObjectInstanceID id) const override
		{
			for(const auto * army : armies)
				if(army && army->id == id)
					return army;
			return nullptr;
		}
		const CGHeroInstance * getHero(ObjectInstanceID) const override { return nullptr; }
		const CGTownInstance * getTown(ObjectInstanceID) const override { return nullptr; }
		// This callback is a one-tile combat world, never a map-query proxy.
		const TerrainTile * getTile(int3, bool = true) const override { return &tile; }
		const TerrainTile * getTileUnchecked(int3) const override { return &tile; }
	};

	std::string failure = "invalid army";
	std::string capturedSource;
	bool unmodelledOwnMagic = false;
	SnapshotCallback callback;
	int3 battleTile;
	std::array<std::unique_ptr<CArmedInstance>, 2> armies;
	std::unique_ptr<BattleInfo> combat; // destroyed before callback and copied armies

	Impl(const IGameInfoCallback & source, TerrainId terrain, int3 tile) : callback(source, terrain), battleTile(tile) {}

	bool copyArmy(const CArmedInstance & source, size_t side)
	{
		if(!source.appearance) { failure = "missing army appearance"; return false; }
		if(source.stacksCount() == 0 || source.stacksCount() > GameConstants::ARMY_SIZE)
			{ failure = "empty or oversized army"; return false; }
		auto copy = std::make_unique<CArmedInstance>(&callback, BonusNodeType::ARMY, true);
		copy->id = ObjectInstanceID(side);
		copy->tempOwner = source.getOwner();
		copy->appearance = source.appearance;
		// Preserve initialized geometry while locating the visitable tile at the
		// destination. Native-terrain queries must not use the hero origin.
		copy->pos = battleTile + copy->getVisitableOffset();
		copy->formation = source.formation;
		for(const auto & [slot, original] : source.Slots())
		{
			if(!original || !original->getCreature() || original->getCount() <= 0)
				return false;
			// The convenience constructor ignores its hypothetical flag in the
			// current engine. Select the node constructor before attaching type.
			auto stack = std::make_unique<CStackInstance>(&callback, BonusNodeType::STACK_INSTANCE, true);
			stack->setType(original->getCreatureID());
			stack->setCount(original->getCount());
			// Copy the complete effective snapshot exactly once. Otherwise the
			// normal creature parent would duplicate the copied base statistics.
			stack->detachFromSource(*stack->getCreature());
			for(const auto & bonus : *original->getAllBonuses(Selector::all))
			{
				// Engine setup reapplies these for the destination context.
				if(bonus->source == BonusSource::TERRAIN_NATIVE || bonus->source == BonusSource::TERRAIN_OVERLAY)
					continue;
				// Preserve already evaluated own-hero specialties whose inputs
				// cannot change in this no-transform, no-level-up combat subset.
				// Do not freeze size/position/army-dependent or propagated effects.
				const bool ownSpecialty = side == 0 && dynamic_cast<const CGHeroInstance *>(&source)
					&& bonus->source == BonusSource::HERO_SPECIAL;
				const bool staticLimiter = ownSpecialty && bonus->limiter
					&& typeid(*bonus->limiter) == typeid(CCreatureTypeLimiter);
				const bool staticUpdater = ownSpecialty && bonus->updater
					&& (typeid(*bonus->updater) == typeid(TimesHeroLevelUpdater)
						|| typeid(*bonus->updater) == typeid(GrowsWithLevelUpdater));
				if((bonus->limiter && !staticLimiter) || (bonus->updater && !staticUpdater)
					|| bonus->propagator || bonus->propagationUpdater)
				{
					failure = "dynamic bonus type " + std::to_string(static_cast<int>(bonus->type));
					return false;
				}
				auto frozen = std::make_shared<Bonus>(*bonus);
				if(staticLimiter) frozen->limiter.reset();
				if(staticUpdater) frozen->updater.reset();
				stack->addNewBonus(frozen);
			}
			copy->putStack(slot, std::move(stack));
		}
		// putStack computes army morale. The original effective morale is
		// already in the copied stacks; do not add a second army modifier.
		copy->removeBonuses(Selector::all);
		armies[side] = std::move(copy);
		callback.armies[side] = armies[side].get();
		return true;
	}
};

PlanningBattleSnapshot::PlanningBattleSnapshot(std::unique_ptr<Impl> implementation)
	: impl(std::move(implementation)) {}
PlanningBattleSnapshot::~PlanningBattleSnapshot() = default;

std::unique_ptr<PlanningBattleSnapshot> PlanningBattleSnapshot::create(
	const IGameInfoCallback & visibleInfo, const CArmedInstance & attacker,
	const CArmedInstance & defender, const PlanningBattleContext & context, std::string * unknownReason)
{
	return createOwned(visibleInfo, attacker, defender, context, true, unknownReason);
}

std::unique_ptr<PlanningBattleSnapshot> PlanningBattleSnapshot::createOwned(
	const IGameInfoCallback & visibleInfo, const CArmedInstance & attacker,
	const CArmedInstance & defender, const PlanningBattleContext & context, bool initializeBattle, std::string * unknownReason)
try
{
	if(unknownReason) *unknownReason = "none";
	auto unknown = [unknownReason](const std::string & reason) -> std::unique_ptr<PlanningBattleSnapshot>
	{
		if(unknownReason) *unknownReason = reason;
		return nullptr;
	};
	if(!context.tile.isValid() || context.terrain == TerrainId::NONE || context.battlefield == BattleField::NONE)
		return unknown("invalid destination context");
	// Validate geometry before any effective bonus query can invoke terrain limiters.
	if(!attacker.appearance || !defender.appearance)
		return unknown("missing army appearance");
	// This snapshot follows a no-spell policy. Affordable own magic is retained
	// as a directional-use restriction, never silently converted into weakness.
	bool unmodelledOwnMagic = false;
	if(dynamic_cast<const CGHeroInstance *>(&defender))
		return unknown("enemy hero");
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(&attacker))
	{
		if(hero->getCommander()) return unknown("commander");
		if(hero->getArt(ArtifactPosition::MACH1) || hero->getArt(ArtifactPosition::MACH2)
			|| hero->getArt(ArtifactPosition::MACH3)) return unknown("war machine");
		if(context.layout.tacticsAllowed && hero->tacticFormationEnabled
			&& hero->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION) > 0) return unknown("tactics");
		if(hero->hasSpellbook())
			for(const auto spell : hero->getSpellsInSpellbook())
				if(spell.toSpell()->isCombat() && hero->getSpellCost(spell.toSpell()) <= hero->mana)
					unmodelledOwnMagic = true;
	}
	// Exact text descriptor, not a collision-prone hash. The owning gateway
	// compares a recapture before publication after releasing its state lock.
	std::ostringstream sourceState;
	auto append = [&sourceState](const std::string & value) { sourceState << '|' << value.size() << ':' << value << '|'; };
	append(visibleInfo.getSettings().getFullConfig().toString());
	for(int mode = 0; mode < 5; ++mode) sourceState << ':' << visibleInfo.getDate(static_cast<Date>(mode));
	sourceState << ':' << context.tile.x << ':' << context.tile.y << ':' << context.tile.z
		<< ':' << context.terrain.getNum() << ':' << context.battlefield.getNum()
		<< ':' << context.layout.tacticsAllowed << ':' << context.layout.obstaclesAllowed;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		for(const auto hex : context.layout.units[side]) sourceState << ':' << hex.toInt();
		for(const auto hex : context.layout.warMachines[side]) sourceState << ':' << hex.toInt();
		sourceState << ':' << context.layout.commanders[side].toInt();
	}
	for(const auto * army : {&attacker, &defender})
	{
		sourceState << ':' << army->id.getNum() << ':' << army->getOwner().getNum()
			<< ':' << army->getTreeVersion() << ':' << static_cast<int>(army->formation)
			<< ':' << army->pos.x << ':' << army->pos.y << ':' << army->pos.z;
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(army))
		{
			sourceState << ':' << hero->mana << ':' << hero->hasSpellbook() << ':' << hero->movementPointsRemaining() << ':' << hero->level;
			for(const auto spell : hero->getSpellsInSpellbook()) sourceState << ':' << spell.getNum();
		}
		append("army");
		for(const auto & [slot, stack] : army->Slots())
		{
			if(!stack || !stack->getCreature() || stack->getCount() <= 0) return unknown("invalid stack");
			sourceState << ':' << slot.getNum() << ':' << stack->getCreatureID().getNum()
				<< ':' << stack->getCount() << ':' << stack->getTreeVersion()
				<< ':' << stack->getCreature()->getTreeVersion();
			for(const auto & bonus : *stack->getAllBonuses(Selector::all)) append(bonus->toJsonNode().toString());
			append("stack-end");
		}
		append("army-end");
	}
	auto implementation = std::make_unique<Impl>(visibleInfo, context.terrain, context.tile);
	implementation->capturedSource = sourceState.str();
	implementation->unmodelledOwnMagic = unmodelledOwnMagic;
	if(!implementation->copyArmy(attacker, 0) || !implementation->copyArmy(defender, 1))
		return unknown(implementation->failure);
	BattleSideArray<const CArmedInstance *> copies{implementation->armies[0].get(), implementation->armies[1].get()};
	BattleSideArray<const CGHeroInstance *> noHeroes{nullptr, nullptr};
	if(initializeBattle)
	{
		implementation->combat = BattleInfo::setupBattle(&implementation->callback, context.tile,
			context.terrain, context.battlefield, copies, noHeroes, context.layout, nullptr);
		implementation->combat->localInit();
	}
	return std::unique_ptr<PlanningBattleSnapshot>(new PlanningBattleSnapshot(std::move(implementation)));
}

catch(const UnsupportedSnapshotQuery & error)
{
	if(unknownReason) *unknownReason = error.what();
	return nullptr;
}

std::unique_ptr<PlanningBattleSnapshot> PlanningBattleSnapshot::createNeutralEncounter(
	const IGameInfoCallback & visibleInfo, const CGHeroInstance & attacker,
	const CGCreature & defender, const PlanningBattleContext & context, std::string * unknownReason)
try
{
	if(unknownReason) *unknownReason = "none";
	auto unknown = [unknownReason](const std::string & reason) -> std::unique_ptr<PlanningBattleSnapshot>
	{
		if(unknownReason) *unknownReason = reason;
		return nullptr;
	};
	if(!attacker.appearance || !defender.appearance) return unknown("missing army appearance");
	// This entry point is stricter than the raw snapshot test adapter: only our
	// current hero and an ordinary visible neutral object may be inspected.
	const auto * publicInfo = dynamic_cast<const CGameInfoCallback *>(&visibleInfo);
	if(!publicInfo || !publicInfo->getPlayerID() || *publicInfo->getPlayerID() != attacker.getOwner()
		|| publicInfo->getObj(attacker.id, false) != &attacker
		|| publicInfo->getObj(defender.id, false) != &defender || defender.ID != Obj::MONSTER
		|| typeid(defender) != typeid(CGCreature)
		|| context.tile != defender.visitablePos())
		return unknown("visibility, owner or ordinary-object eligibility");
	const auto terrain = getNeutralBattleTerrain(*publicInfo, context.tile);
	if(!terrain || *terrain != context.terrain)
		return unknown("unknown destination terrain");
	const auto fields = publicInfo->getBattlefieldCandidates(context.tile);
	if(std::find(fields.begin(), fields.end(), context.battlefield) == fields.end())
		return unknown("unknown battlefield variant");
	if((defender.getOwner() != PlayerColor::NEUTRAL && defender.getOwner() != PlayerColor::UNFLAGGABLE)
		|| defender.CArmedInstance::stacksCount() != 1
		|| !defender.hasStackAtSlot(SlotID(0)))
		return unknown("nonordinary neutral army");
	const int split = defender.getNumberOfStacks(&attacker); // Pure engine rule.
	const auto amount = defender.getStackCount(SlotID(0));
	if(split < 1 || split > GameConstants::ARMY_SIZE || split > amount)
		return unknown("invalid neutral split");
	const CCreature * upgrade = nullptr;
	if(split > 1 && defender.containsUpgradedStack() && !defender.getCreature()->upgrades.empty())
	{
		// A singleton upgrade set has a deterministic outcome even though the
		// server still draws an index. Never draw that authoritative RNG here.
		const auto & choices = defender.getCreature()->upgrades;
		if(choices.size() != 1) return unknown("multiple neutral upgrades");
		upgrade = choices.begin()->toCreature();
		// Different-faction upgrades can change formation morale; stack XP can
		// change intrinsic bonuses on upgrade. These require a richer adapter.
		if(upgrade->getFactionID() != defender.getCreature()->getFactionID()
			|| upgrade->hasBonusOfType(BonusType::UNDEAD) != defender.getCreature()->hasBonusOfType(BonusType::UNDEAD)
			|| defender.getStack(SlotID(0)).getTotalExperience() != 0)
			return unknown("upgrade changes morale or stack experience");
	}
	auto result = createOwned(visibleInfo, attacker, defender, context, false, unknownReason);
	if(!result) return nullptr;
	// Prepare the encounter before a single detached battle initialization.
	auto & army = *result->impl->armies[1];
	auto & original = army.getStack(SlotID(0));
	const auto bonuses = original.getAllBonuses(Selector::all);
	const auto baseCount = amount / split;
	const auto largerStacks = amount % split;
	for(int slot = 1; slot < split; ++slot)
	{
		auto stack = std::make_unique<CStackInstance>(&result->impl->callback, BonusNodeType::STACK_INSTANCE, true);
		stack->setType(original.getCreatureID());
		stack->setCount(baseCount + (slot < largerStacks ? 1 : 0));
		stack->detachFromSource(*stack->getCreature());
		for(const auto & bonus : *bonuses) stack->addNewBonus(std::make_shared<Bonus>(*bonus));
		army.putStack(SlotID(slot), std::move(stack));
	}
	army.setStackCount(SlotID(0), baseCount + (largerStacks > 0 ? 1 : 0));
	if(upgrade)
	{
		const SlotID middle(split / 2);
		const auto oldIntrinsic = defender.getCreature()->getAllBonuses(Selector::all);
		const auto newIntrinsic = upgrade->getAllBonuses(Selector::all);
		const auto effective = defender.getStack(SlotID(0)).getAllBonuses(Selector::all);
		// Effective lists have already resolved named stacking alternatives.
		// Replacing a winning old intrinsic bonus could expose a suppressed
		// external bonus that cannot be recovered from this flattened snapshot.
		for(const auto & list : {oldIntrinsic, newIntrinsic, effective})
			for(const auto & bonus : *list)
				if(!bonus->stacking.empty() && bonus->stacking != "ALWAYS") return unknown("upgrade stacking alternatives");
		std::vector<std::shared_ptr<Bonus>> desired;
		for(const auto & bonus : *effective)
			if(std::find(oldIntrinsic->begin(), oldIntrinsic->end(), bonus) == oldIntrinsic->end())
				desired.push_back(bonus);
		for(const auto & bonus : *newIntrinsic)
			if(std::find(desired.begin(), desired.end(), bonus) == desired.end()) desired.push_back(bonus);
		auto stack = std::make_unique<CStackInstance>(&result->impl->callback, BonusNodeType::STACK_INSTANCE, true);
		stack->setType(upgrade);
		stack->setCount(army.getStackCount(middle));
		stack->detachFromSource(*upgrade);
		for(const auto & bonus : desired)
		{
			if(bonus->source == BonusSource::TERRAIN_NATIVE || bonus->source == BonusSource::TERRAIN_OVERLAY) continue;
			if(bonus->limiter || bonus->updater || bonus->propagator || bonus->propagationUpdater) return unknown("dynamic upgraded bonus");
			stack->addNewBonus(std::make_shared<Bonus>(*bonus));
		}
		army.eraseStack(middle);
		army.putStack(middle, std::move(stack));
		result->impl->capturedSource += ":upgrade:" + std::to_string(upgrade->getIndex())
			+ ":" + std::to_string(upgrade->getTreeVersion());
		for(const auto & bonus : *newIntrinsic)
		{
			const auto text = bonus->toJsonNode().toString();
			result->impl->capturedSource += ":" + std::to_string(text.size()) + ":" + text;
		}
	}
	army.removeBonuses(Selector::all);
	BattleSideArray<const CArmedInstance *> copies{result->impl->armies[0].get(), &army};
	BattleSideArray<const CGHeroInstance *> noHeroes{nullptr, nullptr};
	const auto layout = BattleLayout::createDefaultLayout(result->impl->callback, copies[BattleSide::ATTACKER], copies[BattleSide::DEFENDER]);
	result->impl->combat = BattleInfo::setupBattle(&result->impl->callback, context.tile,
		context.terrain, context.battlefield, copies, noHeroes, layout, nullptr);
	result->impl->combat->localInit();
	result->impl->capturedSource += ":neutral-split:" + std::to_string(split);
	return result;
}

catch(const UnsupportedSnapshotQuery & error)
{
	if(unknownReason) *unknownReason = error.what();
	return nullptr;
}

const CBattleInfoCallback & PlanningBattleSnapshot::battle() const { return *impl->combat; }
const IGameInfoCallback & PlanningBattleSnapshot::frozenGameInfo() const { return impl->callback; }
const std::string & PlanningBattleSnapshot::capturedSourceState() const { return impl->capturedSource; }
bool PlanningBattleSnapshot::hasUnmodelledOwnMagic() const { return impl->unmodelledOwnMagic; }
std::string PlanningBattleSnapshot::physicsState() const
{
	// No adventure position/movement/resource counters here. These belong to
	// publication revalidation, not the identity of an identical combat model.
	std::ostringstream result;
	auto append = [&result](const std::string & value) { result << '|' << value.size() << ':' << value << '|'; };
	append(impl->callback.getSettings().getFullConfig().toString());
	const auto & battle = *impl->combat;
	result << ':' << battle.battleTerrainType().getNum() << ':' << battle.battleGetBattlefieldType().getNum();
	for(const auto * unit : battle.battleAliveUnits())
	{
		result << ':' << unit->unitId() << ':' << static_cast<int>(unit->unitSide()) << ':' << unit->unitOwner().getNum()
			<< ':' << unit->creatureId().getNum() << ':' << unit->unitSlot().getNum() << ':' << unit->getPosition().toInt()
			<< ':' << unit->getCount() << ':' << unit->getAvailableHealth();
		for(const auto & bonus : *unit->getAllBonuses(Selector::all)) append(bonus->toJsonNode().toString());
		append("unit-end");
	}
	for(const auto & obstacle : battle.battleGetAllObstacles())
		result << ':' << obstacle->ID << ':' << static_cast<int>(obstacle->obstacleType) << ':' << obstacle->pos.toInt();
	return result.str();
}

}
