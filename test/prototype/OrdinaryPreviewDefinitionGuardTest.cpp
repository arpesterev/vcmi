/* Ordinary preview seed qualification tests. GPL-2.0-or-later. */
#include "StdInc.h"
#include "lib/battle/OrdinaryPreviewDefinitionGuard.h"
#include "lib/battle/OwnedBattleBonusModel.h"
#include "AI/BattleAI/HypotheticFrameArena.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/GameLibrary.h"
#include "server/battles/BattleTestFixture.h"

class OrdinaryPreviewDefinitionGuardTestPeer
{
public:
 static void copyDefinitionIdentity(CCreature & target, const CCreature & registered)
 {
  target.idNumber = registered.idNumber;
  target.faction = registered.faction;
  target.level = registered.level;
 }
 static const BonusGraphSnapshot & graph(const HypotheticFrameArena & arena)
 { return arena.model.evaluation().graph(); }
 static TConstBonusListPtr resolve(const HypotheticFrameArena & arena, const std::vector<OwnedBonusValueId> & ids)
 { return arena.model.bonusSession->resolveMembership(ids); }
	static void requireAcyclic(const std::vector<std::vector<OrdinaryPreviewDefinitionGuard::NodeId>> & edges)
	{
		OrdinaryPreviewDefinitionGuard::requireAcyclic(edges, "test topology");
	}
};

namespace
{
using Guard = OrdinaryPreviewDefinitionGuard;

std::shared_ptr<Bonus> effect(int value = 3)
{
	return std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::BLOCK_MAGIC_ABOVE, BonusSource::OTHER, value, ArtifactID(791));
}

CSelector selected()
{
	return Selector::source(BonusSource::OTHER, ArtifactID(791));
}

std::shared_ptr<Bonus> timedEffect(int turns = 3)
{
	auto result = effect();
	result->turnsRemain = turns;
	return result;
}

std::array<int32_t, 8> baseValues(const Creature & creature)
{
	return {creature.getBaseAttack(), creature.getBaseDefense(), creature.getBaseDamageMin(), creature.getBaseDamageMax(),
		creature.getBaseHitPoints(), creature.getBaseSpellPoints(), creature.getBaseSpeed(), creature.getBaseShots()};
}

struct DefinitionFacts
{
	const Creature * identity;
	std::array<int32_t, 8> base;
	std::vector<std::pair<const Bonus *, Bonus>> exports;

	explicit DefinitionFacts(const Creature * creature): identity(creature), base(baseValues(*creature))
	{
		for(const auto & bonus : dynamic_cast<const CBonusSystemNode &>(*creature).getExportedBonusList())
			exports.emplace_back(bonus.get(), *bonus);
	}

	void expectUnchanged() const
	{
		EXPECT_EQ(baseValues(*identity), base);
		const auto & current = dynamic_cast<const CBonusSystemNode &>(*identity).getExportedBonusList();
		ASSERT_EQ(current.size(), exports.size());
		for(size_t i = 0; i < current.size(); ++i)
		{
			EXPECT_EQ(current[i].get(), exports.at(i).first);
			const auto & before = exports.at(i).second;
			const auto & after = *current[i];
			EXPECT_EQ(after.duration, before.duration); EXPECT_EQ(after.val, before.val); EXPECT_EQ(after.turnsRemain, before.turnsRemain);
			EXPECT_EQ(after.valType, before.valType); EXPECT_EQ(after.source, before.source);
			EXPECT_EQ(after.targetSourceType, before.targetSourceType); EXPECT_EQ(after.targetSourceID, before.targetSourceID);
			EXPECT_EQ(after.effectRange, before.effectRange); EXPECT_EQ(after.type, before.type);
			EXPECT_EQ(after.subtype, before.subtype); EXPECT_EQ(after.sid, before.sid); EXPECT_EQ(after.stacking, before.stacking);
			EXPECT_EQ(after.bonusOwner, before.bonusOwner); EXPECT_EQ(after.hidden, before.hidden);
			EXPECT_EQ(after.customIconPath, before.customIconPath); EXPECT_EQ(after.description, before.description);
			EXPECT_EQ(after.parameters, before.parameters); EXPECT_EQ(after.limiter, before.limiter); EXPECT_EQ(after.propagator, before.propagator);
			EXPECT_EQ(after.updater, before.updater); EXPECT_EQ(after.propagationUpdater, before.propagationUpdater);
		}
	}
};
}

TEST(OrdinaryPreviewDefinitionGuardTest, InheritanceSourcePreservesNativeIdentityAndIsNotMutationChild)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	stack.attachToSource(creature);
	auto bonus = timedEffect(); creature.addNewBonus(bonus);
	const auto seed = Guard::captureForOrdinarySeed({&stack}, {});
	ASSERT_EQ(seed.definitions().size(), 1);
	EXPECT_EQ(seed.definitions().front().native, static_cast<const Creature *>(&creature));
	const auto stackId = seed.graph().roots().front();
	EXPECT_EQ(seed.mutableNodes(), std::vector<Guard::NodeId>{stackId});
	EXPECT_TRUE(seed.graph().nodes().at(stackId).children.empty());
	EXPECT_EQ(seed.graph().nodes().at(stackId).inheritanceParents.front(), seed.definitions().front().node);
	const auto bonusId = seed.graph().nodes().at(seed.definitions().front().node).exported.front();
	EXPECT_NE(seed.graph().bonuses().at(bonusId).get(), bonus.get());
	EXPECT_EQ(seed.graph().bonuses().at(bonusId)->turnsRemain, 3);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsCreatureDeepInMutationChildrenBeforePublishing)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE), middle(BonusNodeType::HERO);
	middle.attachTo(stack); creature.attachTo(middle);
	CBonusSystemNode safe(BonusNodeType::STACK_BATTLE);
	auto published = Guard::captureForOrdinarySeed({&safe}, {});
	const auto previous = published.graph();
	EXPECT_THROW(published = Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
	EXPECT_TRUE(published.graph().sameOrigin(previous));
	EXPECT_EQ(published.graph().nodes().size(), 1);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsExplicitCreatureMutationRoot)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	stack.attachToSource(creature);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {&creature}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, ChecksExplicitBroadExpiryRootNotJustStacks)
{
	CCreature creature;
	CBonusSystemNode broad(BonusNodeType::GLOBAL_EFFECTS), middle(BonusNodeType::HERO), stack(BonusNodeType::STACK_BATTLE);
	middle.attachTo(broad); creature.attachTo(middle);
	EXPECT_NO_THROW(Guard::captureForOrdinarySeed({&stack, &broad}, {}));
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {&broad}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsDirectSharedExportButAllowsEqualDistinctValues)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	stack.attachToSource(creature);
	auto bonus = timedEffect(); creature.addNewBonus(bonus); stack.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
	stack.removeBonus(bonus);
	stack.addNewBonus(std::make_shared<Bonus>(*bonus));
	EXPECT_NO_THROW(Guard::captureForOrdinarySeed({&stack}, {}));
	stack.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(bonus->turnsRemain, 3);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsAliasAtNonStackDescendant)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE), first(BonusNodeType::HERO), leaf(BonusNodeType::ARMY);
	stack.attachToSource(creature); first.attachTo(stack); leaf.attachTo(first);
	auto bonus = timedEffect(); creature.addNewBonus(bonus); leaf.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsAliasAgainstDisconnectedDeclaredDefinition)
{
	CCreature definition;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto bonus = timedEffect(); definition.addNewBonus(bonus); stack.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack, &definition}, {}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, DiscoversSiblingStackAndDistantDefinitionInFullNativeClosure)
{
	CCreature creature;
	CBonusSystemNode ancestor(BonusNodeType::HERO), requested(BonusNodeType::STACK_BATTLE), sibling(BonusNodeType::STACK_BATTLE);
	requested.attachTo(ancestor); sibling.attachTo(ancestor); sibling.attachToSource(creature);
	auto bonus = timedEffect(); creature.addNewBonus(bonus); sibling.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&requested}, {}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, ImmutableInheritanceAncestorExportAliasIsNotAFalsePositive)
{
	CCreature creature;
	CBonusSystemNode ancestor(BonusNodeType::HERO), stack(BonusNodeType::STACK_BATTLE);
	stack.attachToSource(ancestor); ancestor.attachToSource(creature);
	auto bonus = timedEffect(); creature.addNewBonus(bonus); ancestor.addNewBonus(bonus);
	EXPECT_NO_THROW(Guard::captureForOrdinarySeed({&stack}, {}));
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {&ancestor}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, PropagationChangesActiveMembershipWithoutChangingDefinitionExports)
{
	CCreature creature;
	CBonusSystemNode root(BonusNodeType::BATTLE_WIDE), stack(BonusNodeType::STACK_BATTLE);
	stack.attachTo(root);
	auto definitionBonus = timedEffect();
	definitionBonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
	creature.addNewBonus(definitionBonus);
	stack.attachToSource(creature);
	const DefinitionFacts baseline(&creature);
	ASSERT_EQ(stack.getLocalBonus(selected()), definitionBonus);
	EXPECT_NO_THROW(Guard::captureForOrdinarySeed({&stack}, {&root}));
	auto aura = timedEffect(); aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	stack.addNewBonus(aura);
	ASSERT_EQ(root.getLocalBonus(selected()), aura);
	EXPECT_TRUE(root.getExportedBonusList().empty());
	const auto seed = Guard::captureForOrdinarySeed({&stack, &root}, {&root});
	const auto rootId = seed.graph().roots().at(1);
	EXPECT_FALSE(seed.graph().nodes().at(rootId).materialized.empty());
	EXPECT_TRUE(seed.graph().nodes().at(rootId).exported.empty());
	root.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(aura->turnsRemain, 2); EXPECT_EQ(definitionBonus->turnsRemain, 3);
	stack.removeBonusesRecursive(selected());
	EXPECT_EQ(root.getLocalBonus(selected()), nullptr);
	EXPECT_EQ(stack.getLocalBonus(selected()), definitionBonus);
	stack.detachFrom(root); stack.detachFromSource(creature);
	baseline.expectUnchanged();
}

TEST(OrdinaryPreviewDefinitionGuardTest, SharedChildClosureChecksAliasesOnceAndPreservesNativeRepeatedExpiry)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE), left(BonusNodeType::HERO), right(BonusNodeType::ARMY), shared(BonusNodeType::HERO);
	stack.attachToSource(creature); left.attachTo(stack); right.attachTo(stack);
	shared.attachTo(left); shared.attachTo(right);
	auto bonus = timedEffect(); creature.addNewBonus(bonus); shared.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
	shared.removeBonus(bonus);
	auto distinct = std::make_shared<Bonus>(*bonus); shared.addNewBonus(distinct);
	const auto seed = Guard::captureForOrdinarySeed({&stack}, {});
	EXPECT_EQ(seed.mutableNodes().size(), 4);
	stack.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(distinct->turnsRemain, 1);
	EXPECT_EQ(bonus->turnsRemain, 3);
}

TEST(OrdinaryPreviewDefinitionGuardTest, CycleValidatorHandlesSelfCycleDiamondAndDeepChainIteratively)
{
	using Peer = OrdinaryPreviewDefinitionGuardTestPeer;
	EXPECT_THROW(Peer::requireAcyclic({{0}}), std::invalid_argument);
	EXPECT_THROW(Peer::requireAcyclic({{1}, {2}, {0}}), std::invalid_argument);
	EXPECT_NO_THROW(Peer::requireAcyclic({{1, 2}, {3}, {3}, {}}));
	std::vector<std::vector<Guard::NodeId>> chain(10000);
	for(size_t index = 1; index < chain.size(); ++index) chain.at(index - 1).push_back(index);
	EXPECT_NO_THROW(Peer::requireAcyclic(chain));
	chain.back().push_back(0);
	EXPECT_THROW(Peer::requireAcyclic(chain), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, SharedReadOnlyComponentsDoNotImplySharedMutableExports)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE); stack.attachToSource(creature);
	auto definitionBonus = timedEffect(), stackBonus = timedEffect();
	auto updater = std::make_shared<IUpdater>();
	definitionBonus->updater = updater; stackBonus->updater = updater;
	creature.addNewBonus(definitionBonus); stack.addNewBonus(stackBonus);
	const auto seed = Guard::captureForOrdinarySeed({&stack}, {});
	const auto definitionId = seed.graph().nodes().at(seed.definitions().front().node).exported.front();
	const auto stackId = seed.graph().nodes().at(seed.graph().roots().front()).exported.front();
	EXPECT_NE(definitionId, stackId);
	EXPECT_EQ(seed.graph().bonuses().at(definitionId)->updater, seed.graph().bonuses().at(stackId)->updater);
	EXPECT_NE(seed.graph().bonuses().at(definitionId)->updater, updater);
}

TEST(OrdinaryPreviewDefinitionGuardTest, ExtraCacheListsPreserveAliasesWithoutBecomingMutableExports)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE); stack.attachToSource(creature);
	auto bonus = timedEffect(); creature.addNewBonus(bonus);
	auto extra = std::make_shared<BonusList>(); extra->push_back(bonus); extra->push_back(bonus);
	const auto seed = Guard::captureForOrdinarySeed({&stack}, {}, {extra});
	const auto bonusId = seed.graph().nodes().at(seed.definitions().front().node).exported.front();
	const auto valueId = seed.graph().graphValueIds().at(bonusId);
	EXPECT_EQ(seed.graph().capturedExtraLists().front(), (std::vector<BonusGraphSnapshot::ValueId>{valueId, valueId}));
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsMalformedInputsAndUnsupportedDefinitionTypes)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE), placeholder(BonusNodeType::CREATURE);
	EXPECT_THROW(Guard::captureForOrdinarySeed({}, {}), std::invalid_argument);
	EXPECT_THROW(Guard::captureForOrdinarySeed({nullptr}, {}), std::invalid_argument);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {nullptr}), std::invalid_argument);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&placeholder}, {}), std::invalid_argument);
	struct CustomCreature : CCreature {} custom;
	EXPECT_THROW(Guard::captureForOrdinarySeed({&custom}, {}), std::invalid_argument);
	stack.getExportedBonusList().push_back(nullptr);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsInheritanceCycleOutsideMutationChildren)
{
	CBonusSystemNode first(BonusNodeType::HERO), second(BonusNodeType::ARMY), stack(BonusNodeType::STACK_BATTLE);
	first.attachToSource(second); second.attachToSource(first); stack.attachToSource(first);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
	// Source-only attachment to these non-source nodes does not create red edges.
	second.detachFromSource(first); first.detachFromSource(second);
}

TEST(OrdinaryPreviewDefinitionGuardTest, RejectsUnsupportedComponentsInRecursivelyDiscoveredNativeNode)
{
	struct UnsupportedUpdater : IUpdater {};
	CBonusSystemNode ancestor(BonusNodeType::HERO), requested(BonusNodeType::STACK_BATTLE), sibling(BonusNodeType::ARMY);
	requested.attachTo(ancestor); sibling.attachTo(ancestor);
	auto bonus = timedEffect(); bonus->updater = std::make_shared<UnsupportedUpdater>(); sibling.addNewBonus(bonus);
	EXPECT_THROW(Guard::captureForOrdinarySeed({&requested}, {}), std::runtime_error);
}

TEST(OrdinaryPreviewDefinitionGuardTest, GenericSnapshotStillAllowsControlledDefinitionEditingTopology)
{
	CCreature creature;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE); creature.attachTo(stack);
	EXPECT_NO_THROW(BonusGraphSnapshot::capture({&stack}));
	EXPECT_THROW(Guard::captureForOrdinarySeed({&stack}, {}), std::invalid_argument);
}

class OrdinaryPreviewDefinitionGuardBattleTest : public BattleTestFixture
{
public:
	void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
};

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, NativeStackPacketsExpiryRoundAndRemovalPreserveAllCapturedDefinitions)
{
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
	ASSERT_NE(unit, nullptr);
	const auto * identity = unit->unitType();
	const auto seed = Guard::captureForOrdinarySeed({battle(), unit}, {});
	std::vector<DefinitionFacts> baseline;
	bool found = false;
	for(const auto & definition : seed.definitions())
	{
		baseline.emplace_back(definition.native);
		found = found || definition.native == identity;
	}
	ASSERT_TRUE(found);
	auto unchanged = [&]
	{
		EXPECT_EQ(unit->unitType(), identity);
		for(const auto & facts : baseline) facts.expectUnchanged();
	};

	Bonus value(BonusDuration::N_TURNS, BonusType::BLOCK_MAGIC_ABOVE, BonusSource::SPELL_EFFECT, 3, SpellID(SpellID::HASTE));
	value.turnsRemain = 3;
	const auto spellSelector = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)).And(Selector::type()(BonusType::BLOCK_MAGIC_ABOVE));
	battle()->addUnitBonus(unit->unitId(), {value});
	auto local = unit->getExportedBonusList().getFirst(spellSelector);
	ASSERT_NE(local, nullptr); EXPECT_EQ(local->turnsRemain, 3); unchanged();
	value.turnsRemain = 7; battle()->updateUnitBonus(unit->unitId(), {value});
	EXPECT_EQ(unit->getExportedBonusList().getFirst(spellSelector), local);
	EXPECT_EQ(local->turnsRemain, 7); unchanged();
	battle()->removeUnitBonus(unit->unitId(), {value});
	EXPECT_EQ(unit->getExportedBonusList().getFirst(spellSelector), nullptr); unchanged();

	auto aura = timedEffect(); aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	unit->addNewBonus(aura);
	ASSERT_FALSE(battle()->getAllBonuses(selected())->empty());
	EXPECT_NO_THROW(Guard::captureForOrdinarySeed({battle(), unit}, {}));
	const auto beforeRound = battle()->getRound();
	ASSERT_EQ(beforeRound, 0);
	battle()->nextRound();
	EXPECT_EQ(aura->turnsRemain, beforeRound == 0 ? 3 : 2); unchanged();
	ASSERT_EQ(battle()->getRound(), 1);
	battle()->nextRound();
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_EQ(aura->turnsRemain, 2); unchanged();
	auto untilTurn = timedEffect(); untilTurn->duration = BonusDuration::STACK_GETS_TURN;
	unit->addNewBonus(untilTurn);
	battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::UNIT_SPELLCAST);
	EXPECT_NE(unit->getExportedBonusList().getFirst(Bonus::UntilGetsTurn), nullptr);
	battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(unit->getExportedBonusList().getFirst(Bonus::UntilGetsTurn), nullptr); unchanged();
	battle()->removeUnit(unit->unitId());
	EXPECT_TRUE(battle()->getAllBonuses(selected())->empty()); unchanged();
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedModelSharesGuardRootsValuesAndExtraCacheAliases)
{
 auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 auto value = timedEffect(); unit->addNewBonus(value);
 auto extra = std::make_shared<BonusList>(); extra->push_back(value); extra->push_back(value);
 auto cacheOnly = timedEffect(); extra->push_back(cacheOnly); extra->push_back(cacheOnly);
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle(), {}, {extra});
 const auto & model = captured.first;
 const auto & guard = captured.second;
 const auto & graph = model.evaluation().graph();
 EXPECT_TRUE(graph.sameOrigin(guard.graph()));
 EXPECT_EQ(graph.roots(), guard.graph().roots());
 EXPECT_EQ(graph.capturedExtraLists(), guard.graph().capturedExtraLists());
 ASSERT_FALSE(model.units().empty());
 const auto facts = std::find_if(model.units().begin(), model.units().end(),
  [&](const auto & candidate) { return candidate.id == unit->unitId(); });
 ASSERT_NE(facts, model.units().end());
 const auto index = static_cast<size_t>(facts - model.units().begin());
 const auto node = facts->node;
 EXPECT_EQ(node, graph.roots().at(index));
 ASSERT_FALSE(graph.nodes().at(node).exported.empty());
 const auto bonus = graph.nodes().at(node).exported.back();
 const auto id = graph.graphValueIds().at(bonus);
 const auto & membership = graph.capturedExtraLists().front();
 ASSERT_EQ(membership.size(), 4);
 EXPECT_EQ(membership.at(0), id); EXPECT_EQ(membership.at(1), id);
 EXPECT_EQ(membership.at(2), membership.at(3)); EXPECT_NE(membership.at(2), id);
 EXPECT_NE(graph.capturedValues().at(membership.at(2)).get(), cacheOnly.get());
 for(const auto & definition : guard.definitions())
  EXPECT_EQ(model.creatureNode(definition.native->getId()), definition.node);
 const auto before = model.getAllBonuses(unit->unitId(), selected())->totalValue();
 value->val = 99;
 EXPECT_EQ(model.getAllBonuses(unit->unitId(), selected())->totalValue(), before);
 EXPECT_EQ(guard.graph().bonuses().at(bonus)->val, 3);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedArenaPreservesProxyMembershipAliases)
{
 auto * native = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 auto value = timedEffect(); native->addNewBonus(value);
 auto list = std::make_shared<BonusList>(); list->push_back(value); list->push_back(value);
 auto caches = native->captureBonusCaches();
 caches.immobilized.list = list; caches.speed.list = list;
 native->restoreBonusCaches(caches);
 auto captured = HypotheticFrameArena::captureForOrdinarySeed(*battle());
 auto unit = captured.first->unit(native->unitId());
 ASSERT_NE(unit, nullptr);
 const auto owned = unit->captureBonusCaches();
 ASSERT_NE(owned.immobilized.membership, nullptr);
 ASSERT_NE(owned.speed.membership, nullptr);
 ASSERT_EQ(owned.speed.membership->size(), 2);
 EXPECT_TRUE(OrdinaryPreviewDefinitionGuardTestPeer::graph(*captured.first).sameOrigin(captured.second.graph()));
 auto combined = *owned.speed.membership;
 combined.insert(combined.end(), owned.immobilized.membership->begin(), owned.immobilized.membership->end());
 const auto values = OrdinaryPreviewDefinitionGuardTestPeer::resolve(*captured.first, combined);
 ASSERT_EQ(values->size(), 4);
 EXPECT_EQ((*values)[0], (*values)[1]);
 EXPECT_EQ((*values)[0], (*values)[2]);
 EXPECT_EQ((*values)[0], (*values)[3]);
 EXPECT_EQ(owned.speed.list, nullptr);
 EXPECT_EQ(owned.immobilized.list, nullptr);
 value->val = 99;
 EXPECT_EQ(unit->getAllBonuses(selected())->totalValue(), 3);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedCaptureRejectsBindingsAndTopologyBeforePublication)
{
 auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 auto extra = std::make_shared<BonusList>(); extra->push_back(timedEffect());
 auto published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle(), {}, {extra});
 const auto previous = published.first.evaluation().graph();
 CBonusSystemNode placeholder(BonusNodeType::CREATURE);
 unit->attachToSource(placeholder);
 EXPECT_THROW(published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_TRUE(published.first.evaluation().graph().sameOrigin(previous));
 EXPECT_TRUE(published.second.graph().sameOrigin(previous));
 EXPECT_EQ(published.first.evaluation().graph().capturedExtraLists(), previous.capturedExtraLists());
 unit->detachFromSource(placeholder);
 auto & definition = *const_cast<CCreature *>(creatureByName("core:pikeman").toCreature());
 definition.attachTo(*unit);
 EXPECT_NO_THROW(OwnedBattleBonusModel::capture(*battle()));
 EXPECT_NO_THROW(HypotheticFrameArena::capture(*battle()));
 EXPECT_THROW(published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_TRUE(published.first.evaluation().graph().sameOrigin(previous));
 EXPECT_TRUE(published.second.graph().sameOrigin(previous));
 EXPECT_EQ(published.first.evaluation().graph().capturedExtraLists(), previous.capturedExtraLists());
 definition.detachFrom(*unit);
 auto arena = HypotheticFrameArena::captureForOrdinarySeed(*battle());
 const auto previousArena = arena.first;
 const auto previousCaches = arena.first->unit(unit->unitId())->captureBonusCaches();
 unit->attachToSource(placeholder);
 EXPECT_THROW(arena = HypotheticFrameArena::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_EQ(arena.first, previousArena);
 const auto currentCaches = arena.first->unit(unit->unitId())->captureBonusCaches();
 EXPECT_EQ(currentCaches.speed.membership, previousCaches.speed.membership);
 EXPECT_EQ(currentCaches.immobilized.membership, previousCaches.immobilized.membership);
 unit->detachFromSource(placeholder);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedEvaluationPreservesOrderedDuplicateRoots)
{
 auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 const auto captured = Guard::captureEvaluationForOrdinarySeed({unit, unit}, {battle(), battle()});
 const auto & graph = captured.first.graph();
 EXPECT_TRUE(graph.sameOrigin(captured.second.graph()));
 EXPECT_EQ(graph.roots(), captured.second.graph().roots());
 ASSERT_EQ(graph.roots().size(), 4);
 EXPECT_EQ(graph.roots().at(0), graph.roots().at(1));
 EXPECT_EQ(graph.roots().at(2), graph.roots().at(3));
 EXPECT_NE(graph.roots().at(0), graph.roots().at(2));
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedModelRejectsDuplicateRegisteredDefinitionBeforePublication)
{
 auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(unit, nullptr);
 auto published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 const auto previous = published.first.evaluation().graph();
 auto & definitions = LIBRARY->creh->objects;
 const auto registered = definitions.at(unit->creatureId().getNum());
 ASSERT_NE(registered, nullptr);
 struct RestoreRegistrySize
 {
  std::vector<std::shared_ptr<CCreature>> & definitions;
  const size_t size;
  ~RestoreRegistrySize() { definitions.resize(size); }
 } restore{definitions, definitions.size()};
 definitions.push_back(registered);
 EXPECT_NO_THROW(OwnedBattleBonusModel::capture(*battle()));
 EXPECT_THROW(published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_TRUE(published.first.evaluation().graph().sameOrigin(previous));
 EXPECT_TRUE(published.second.graph().sameOrigin(previous));
 EXPECT_EQ(published.first.evaluation().graph().roots(), previous.roots());
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, QualifiedModelRejectsForeignDefinitionWithRegisteredIdBeforePublication)
{
 auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(unit, nullptr);
 auto extra = std::make_shared<BonusList>(); extra->push_back(timedEffect());
 auto published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle(), {}, {extra});
 const auto previous = published.first.evaluation().graph();
 auto arena = HypotheticFrameArena::captureForOrdinarySeed(*battle());
 const auto previousArena = arena.first;
 const auto previousCaches = arena.first->unit(unit->unitId())->captureBonusCaches();
 const auto * registered = unit->creatureId().toCreature();
 CCreature foreign;
 OrdinaryPreviewDefinitionGuardTestPeer::copyDefinitionIdentity(foreign, *registered);
 ASSERT_EQ(foreign.getId(), registered->getId());
 ASSERT_NE(&foreign, registered);
 struct DetachSource
 {
  CBonusSystemNode & unit;
  const CBonusSystemNode & source;
  ~DetachSource() { unit.detachFromSource(source); }
 };
 unit->attachToSource(foreign);
 const DetachSource detach{*unit, foreign};
 // Native type/fact capture accepts both identities; model binding must reject.
 EXPECT_NO_THROW(Guard::captureForOrdinarySeed({unit}, {}));
 EXPECT_NO_THROW(Guard::captureEvaluationForOrdinarySeed({unit}, {}));
 EXPECT_NO_THROW(OwnedBattleBonusModel::capture(*battle()));
 EXPECT_THROW(published = OwnedBattleBonusModel::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_TRUE(published.first.evaluation().graph().sameOrigin(previous));
 EXPECT_TRUE(published.second.graph().sameOrigin(previous));
 EXPECT_EQ(published.first.evaluation().graph().roots(), previous.roots());
 EXPECT_EQ(published.first.evaluation().graph().capturedExtraLists(), previous.capturedExtraLists());
 EXPECT_THROW(arena = HypotheticFrameArena::captureForOrdinarySeed(*battle()), std::invalid_argument);
 EXPECT_EQ(arena.first, previousArena);
 const auto currentCaches = arena.first->unit(unit->unitId())->captureBonusCaches();
 EXPECT_EQ(currentCaches.speed.membership, previousCaches.speed.membership);
 EXPECT_EQ(currentCaches.immobilized.membership, previousCaches.immobilized.membership);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryCapturePreservesNativeCreatureAcrossForkAndAcquire)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 const auto id = source->unitId();
 const auto * definition = source->unitType();
 const auto nativeBefore = source->save();
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto sibling = ordinary->fork();
 auto unit = ordinary->unit(id);
 EXPECT_EQ(unit->unitType(), definition);
 EXPECT_EQ(sibling->unit(id)->unitType(), definition);
 auto acquired = unit->acquireState();
 auto reacquired = acquired->acquireState();
 EXPECT_EQ(acquired->unitType(), definition);
 EXPECT_EQ(reacquired->unitType(), definition);
 int64_t damage = unit->getMaxHealth(); unit->damage(damage);
 EXPECT_EQ(unit->getCount(), 9);
 EXPECT_EQ(sibling->unit(id)->getCount(), 10);
 EXPECT_EQ(acquired->getCount(), 10);
 EXPECT_EQ(reacquired->getCount(), 10);
 EXPECT_EQ(source->save(), nativeBefore);
 const auto fixture = HypotheticFrameArena::capture(*battle());
 EXPECT_NE(fixture->unit(id)->unitType(), definition);
 EXPECT_NE(dynamic_cast<const OwnedCreature *>(fixture->unit(id)->unitType()), nullptr);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryAcquiredAliasesRetainLocalEnvironmentThroughReacquire)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 auto * cart = addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"), BattleHex(8, 5), 1);
 ASSERT_NE(source, nullptr); ASSERT_NE(cart, nullptr);
 const auto * definition = source->unitType();
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 std::weak_ptr<HypotheticFrameArena> owner = ordinary;
 auto acquired = ordinary->unit(source->unitId())->acquireState();
 EXPECT_FALSE(acquired->shots.isLimited());
 auto cartView = ordinary->unit(cart->unitId());
 cartView->health.reset();
 EXPECT_TRUE(acquired->shots.isLimited());
 auto reacquired = acquired->acquireState();
 acquired.reset(); cartView.reset(); ordinary.reset();
 EXPECT_FALSE(owner.expired());
 EXPECT_EQ(reacquired->unitType(), definition);
 EXPECT_TRUE(reacquired->shots.isLimited());
 EXPECT_TRUE(cart->alive());
 reacquired.reset();
 EXPECT_TRUE(owner.expired());
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryScalarObservationPreservesBonusPointersAndNativeCacheEpoch)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect(); bonus->val = 7; source->addNewBonus(bonus);
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto sibling = ordinary->fork();
 auto unit = ordinary->unit(source->unitId());
 const auto version = unit->getTreeVersion();
 const auto first = unit->getAllBonuses(selected());
 const auto independent = sibling->unit(source->unitId())->getAllBonuses(selected());
 ASSERT_EQ(first->size(), 1); ASSERT_EQ(independent->size(), 1);
 EXPECT_NE(first->front().get(), independent->front().get());
 int64_t damage = unit->getMaxHealth(); unit->damage(damage);
 const auto second = unit->getAllBonuses(selected());
 ASSERT_EQ(second->size(), 1);
 EXPECT_EQ(first->front().get(), second->front().get());
 EXPECT_EQ(second->front()->val, 7);
 EXPECT_EQ(unit->getTreeVersion(), version);
 EXPECT_EQ(source->getCount(), 10);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryAcquireFreezesBonusesWhileNativeDefinitionsStayIdentical)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect(); bonus->val = 7; source->addNewBonus(bonus);
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto unit = ordinary->unit(source->unitId());
 auto acquired = unit->acquireState();
 unit->removeUnitBonus(selected());
 EXPECT_TRUE(unit->getAllBonuses(selected())->empty());
 EXPECT_EQ(acquired->getAllBonuses(selected())->front()->val, 7);
 auto reacquired = acquired->acquireState();
 EXPECT_EQ(reacquired->getAllBonuses(selected())->front()->val, 7);
 EXPECT_EQ(reacquired->unitType(), source->unitType());
 EXPECT_EQ(source->getAllBonuses(selected())->front()->val, 7);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryCaptureRejectsUnsafeDefinitionTopologyWithoutReplacingPublishedArena)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto published = HypotheticFrameArena::captureOrdinary(*battle());
 const auto before = published;
 auto definition = std::make_unique<CCreature>();
 definition->attachTo(*source);
 EXPECT_THROW(published = HypotheticFrameArena::captureOrdinary(*battle()), std::invalid_argument);
 EXPECT_EQ(published, before);
 definition->detachFrom(*source);
 EXPECT_EQ(published->unit(source->unitId())->unitType(), source->unitType());
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryRoundRetainsExistingScalarLifecycleWithObstacles)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 addForceField(BattleHex(10, 5));
 source->setTurnStatus(battle::UnitTurnStatus::ACTED);
 source->hadMorale = true;
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto unit = ordinary->unit(source->unitId());
 EXPECT_NO_THROW(ordinary->nextOrdinaryRound());
 EXPECT_EQ(unit->getTurnStatus(), battle::UnitTurnStatus::READY);
 EXPECT_FALSE(unit->hadMorale);
 EXPECT_EQ(source->getTurnStatus(), battle::UnitTurnStatus::ACTED);
 EXPECT_TRUE(source->hadMorale);
 EXPECT_NO_THROW(ordinary->nextOrdinaryTurn(source->unitId(), BattleUnitTurnReason::MORALE));
 EXPECT_TRUE(unit->hadMorale);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryCanonicalUpdateAcceptsCloneFlagsWithoutChangingFixturePolicy)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto fixture = HypotheticFrameArena::capture(*battle());
 auto changed = ordinary->unit(source->unitId())->acquireState();
 changed->cloned = true;
 changed->summoned = true;
 BattleUnitsChanged packet;
 UnitChanges update;
 update.id = source->unitId();
 update.operation = BattleChanges::EOperation::UPDATE;
 update.data = changed->save();
 update.healthDelta = 0;
 packet.changedStacks.push_back(update);
 EXPECT_NO_THROW(ordinary->applyStateUpdates(packet));
 EXPECT_TRUE(ordinary->unit(source->unitId())->cloned);
 EXPECT_TRUE(ordinary->unit(source->unitId())->summoned);
 EXPECT_THROW(fixture->applyStateUpdates(packet), std::invalid_argument);
 EXPECT_FALSE(source->cloned);
 EXPECT_FALSE(source->summoned);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryReadOnlyParentSupportsConcurrentLocalForks)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect(); bonus->val = 7; source->addNewBonus(bonus);
 const auto id = source->unitId();
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 std::array<std::shared_ptr<HypotheticFrameArena>, 2> children;
 std::array<std::thread, 2> workers;
 for(size_t i = 0; i < workers.size(); ++i)
  workers[i] = std::thread([&, i]
  {
   children[i] = ordinary->fork();
   children[i]->unit(id)->getAllBonuses(selected());
   ordinary->unit(id)->getAllBonuses(selected());
  });
 for(auto & worker : workers) worker.join();
 ASSERT_NE(children[0], nullptr); ASSERT_NE(children[1], nullptr);
 int64_t damage = children[0]->unit(id)->getMaxHealth();
 children[0]->unit(id)->damage(damage);
 EXPECT_EQ(children[0]->unit(id)->getCount(), 9);
 EXPECT_EQ(children[1]->unit(id)->getCount(), 10);
 EXPECT_EQ(ordinary->unit(id)->getCount(), 10);
 EXPECT_EQ(children[0]->unit(id)->unitType(), source->unitType());
 EXPECT_EQ(children[1]->unit(id)->unitType(), source->unitType());
 EXPECT_EQ(source->getCount(), 10);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryObservedParametersSupportRawEventsButRejectForeignQueryComponents)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect();
 bonus->source = BonusSource::SPELL_EFFECT; bonus->sid = SpellID(SpellID::HASTE);
 const auto selectedValue = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)).And(Selector::type()(BonusType::BLOCK_MAGIC_ABOVE));
 bonus->parameters = std::make_shared<const BonusParameters>(7);
 source->addNewBonus(bonus);
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto foreign = HypotheticFrameArena::captureOrdinary(*battle());
 auto unit = ordinary->unit(source->unitId());
 const auto observed = unit->getAllBonuses(selectedValue);
 ASSERT_EQ(observed->size(), 1);
 Bonus update = *observed->front(); update.turnsRemain = 8;
 EXPECT_NO_THROW(unit->updateUnitBonus({update}));
 const auto current = unit->getAllBonuses(selectedValue);
 ASSERT_EQ(current->size(), 1);
 EXPECT_EQ(current->front()->turnsRemain, 8);
 EXPECT_EQ(current->front()->parameters->toNumber(), 7);
 const auto other = foreign->unit(source->unitId())->getAllBonuses(selectedValue);
 ASSERT_EQ(other->size(), 1);
 EXPECT_THROW(ordinary->makeBonusEvent({*other->front()}), std::invalid_argument);
 EXPECT_NO_THROW(unit->removeUnitBonus(std::vector<Bonus>{*current->front()}));
 EXPECT_TRUE(unit->getAllBonuses(selectedValue)->empty());
 EXPECT_EQ(source->getAllBonuses(selectedValue)->front()->turnsRemain, 3);
}

#include <latch>

class OrdinaryPreviewCacheAccessTestPeer
{
public:
 static std::unique_lock<std::recursive_mutex> tryAccess(const HypotheticFrameArena & arena)
 { return std::unique_lock<std::recursive_mutex>(arena.accessMutex, std::try_to_lock); }
 static const IBonusBearer * replaceMovementTarget(battle::CUnitState & state, const IBonusBearer * target)
 {
  const auto * previous = state.immobilizedPerTurn.target;
  state.immobilizedPerTurn.target = target;
  return previous;
 }
 static const IBonusBearer * replaceCombatTarget(battle::CUnitState & state, const IBonusBearer * target)
 {
  const auto * previous = state.combatValue.target;
  state.combatValue.target = target;
  return previous;
 }
};

namespace
{
class PausedPreviewVersion final : public IBonusBearer
{
 const IBonusBearer & target;
 std::latch & reached;
 std::latch & resume;
 mutable std::atomic<bool> first{true};
 mutable std::atomic<bool> announced{false};
public:
 PausedPreviewVersion(const IBonusBearer & target, std::latch & reached, std::latch & resume)
  : target(target), reached(reached), resume(resume) {}
 void announce() const
 {
  if(!announced.exchange(true)) reached.count_down();
 }
 int32_t getTreeVersion() const override
 {
  const auto version = target.getTreeVersion();
  if(first.exchange(false))
  {
   announce();
   resume.wait();
  }
  return version;
 }
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = {}) const override
 { return target.getAllBonuses(selector, key); }
};
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryMovementGetterHoldsOwnerBeforePerTurnListAndConcurrentFork)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 const auto id = source->unitId();
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto unit = ordinary->unit(id);
 auto caches = unit->captureBonusCaches();
 caches.immobilized.listVersion = -1;
 for(auto & entry : caches.immobilized.entries) entry.version = -1;
 unit->restoreBonusCaches(caches);
 std::latch reached(1), resume(1), forkAttempted(1);
 PausedPreviewVersion target(*unit, reached, resume);
 const auto * previous = OrdinaryPreviewCacheAccessTestPeer::replaceMovementTarget(*unit, &target);
 std::exception_ptr readFailure, forkFailure;
 std::shared_ptr<HypotheticFrameArena> child;
 bool forkObservedOwner = false;
 uint32_t movement = 0;
 std::thread reader([&]
 {
  try { movement = unit->getMovementRange(); unit->canMove(); }
  catch(const std::exception &) { readFailure = std::current_exception(); target.announce(); }
 });
 reached.wait();
 std::thread forker([&]
 {
  try
  {
   auto probe = OrdinaryPreviewCacheAccessTestPeer::tryAccess(*ordinary);
   forkObservedOwner = !probe.owns_lock();
   if(probe.owns_lock()) probe.unlock();
  }
  catch(const std::exception &) { forkFailure = std::current_exception(); }
  forkAttempted.count_down();
  if(!forkFailure)
   try { child = ordinary->fork(); }
   catch(const std::exception &) { forkFailure = std::current_exception(); }
 });
 forkAttempted.wait();
 resume.count_down();
 reader.join(); forker.join();
 OrdinaryPreviewCacheAccessTestPeer::replaceMovementTarget(*unit, previous);
 EXPECT_FALSE(readFailure); EXPECT_FALSE(forkFailure);
 EXPECT_TRUE(forkObservedOwner);
 ASSERT_NE(child, nullptr);
 EXPECT_EQ(child->unit(id)->getMovementRange(), movement);
 EXPECT_EQ(child->unit(id)->canMove(), unit->canMove());
 EXPECT_EQ(child->unit(id)->getTreeVersion(), unit->getTreeVersion());
 const auto parentCaches = unit->captureBonusCaches();
 const auto childCaches = child->unit(id)->captureBonusCaches();
 EXPECT_EQ(parentCaches.immobilized.listVersion, childCaches.immobilized.listVersion);
 EXPECT_EQ(parentCaches.speed.listVersion, childCaches.speed.listVersion);
 EXPECT_EQ(parentCaches.immobilized.membership, childCaches.immobilized.membership);
 EXPECT_EQ(parentCaches.speed.membership, childCaches.speed.membership);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryDerivedCombatGetterKeepsVariantTransactionInsideOwnerDuringFork)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 const auto id = source->unitId();
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 auto unit = ordinary->unit(id);
 std::latch reached(1), resume(1), forkAttempted(1);
 PausedPreviewVersion target(*unit, reached, resume);
 const auto * previous = OrdinaryPreviewCacheAccessTestPeer::replaceCombatTarget(*unit, &target);
 std::exception_ptr readFailure, forkFailure;
 std::shared_ptr<HypotheticFrameArena> child;
 bool forkObservedOwner = false;
 uint64_t combatValue = 0;
 std::thread reader([&]
 {
  try { combatValue = unit->estimateCombatValue(); unit->counterAttacks.available(); }
  catch(const std::exception &) { readFailure = std::current_exception(); target.announce(); }
 });
 reached.wait();
 std::thread forker([&]
 {
  try
  {
   auto probe = OrdinaryPreviewCacheAccessTestPeer::tryAccess(*ordinary);
   forkObservedOwner = !probe.owns_lock();
   if(probe.owns_lock()) probe.unlock();
  }
  catch(const std::exception &) { forkFailure = std::current_exception(); }
  forkAttempted.count_down();
  if(!forkFailure)
   try { child = ordinary->fork(); }
   catch(const std::exception &) { forkFailure = std::current_exception(); }
 });
 forkAttempted.wait(); resume.count_down();
 reader.join(); forker.join();
 OrdinaryPreviewCacheAccessTestPeer::replaceCombatTarget(*unit, previous);
 EXPECT_FALSE(readFailure); EXPECT_FALSE(forkFailure);
 EXPECT_TRUE(forkObservedOwner);
 ASSERT_NE(child, nullptr);
 EXPECT_EQ(child->unit(id)->estimateCombatValue(), combatValue);
 EXPECT_EQ(child->unit(id)->counterAttacks.available(), unit->counterAttacks.available());
 const auto parentCache = unit->captureBonusCaches().combat;
 const auto childCache = child->unit(id)->captureBonusCaches().combat;
 EXPECT_EQ(parentCache.entry.version, childCache.entry.version);
 EXPECT_EQ(parentCache.entry.value, childCache.entry.value);
 EXPECT_EQ(parentCache.variant, childCache.variant);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryObservedParameterRejectsSameAddressWithForeignOwnership)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect();
 bonus->parameters = std::make_shared<const BonusParameters>(7);
 source->addNewBonus(bonus);
 auto ordinary = HypotheticFrameArena::captureOrdinary(*battle());
 const auto observed = ordinary->unit(source->unitId())->getAllBonuses(selected());
 ASSERT_EQ(observed->size(), 1);
 Bonus forged = *observed->front();
 forged.parameters = std::shared_ptr<const BonusParameters>(observed->front()->parameters.get(), [](const BonusParameters *) {});
 EXPECT_THROW(ordinary->makeBonusEvent({forged}), std::invalid_argument);
 EXPECT_NO_THROW(ordinary->makeBonusEvent({*observed->front()}));
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryWarmRequestHitKeepsSnapshotAndSkipsSelector)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect();
 source->addNewBonus(bonus);
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 auto & model = captured.first;
 const auto id = source->unitId();
 ASSERT_EQ(model.queryBonuses(id, selected(), "warm-selected")->size(), 1);
 EXPECT_TRUE(model.queryBonuses(id, Selector::none, "warm-empty")->empty());
 const auto * before = &model.evaluation();
 const auto version = model.nodeVersion(id);
 const auto revision = model.revision();
 auto sibling = model;
 int calls = 0;
 CSelector throwing = [&](const Bonus *) -> bool
 {
  ++calls;
  throw std::runtime_error("A cached request must not invoke its selector");
 };
 auto first = model.queryBonuses(id, throwing, "warm-selected");
 ASSERT_EQ(first->size(), 1);
 EXPECT_EQ(&model.evaluation(), before);
 EXPECT_TRUE(model.queryBonuses(id, throwing, "warm-empty")->empty());
 EXPECT_EQ(&model.evaluation(), before);
 EXPECT_EQ(calls, 0);
 EXPECT_EQ(model.nodeVersion(id), version);
 EXPECT_EQ(model.revision(), revision);
 first->front()->val = 999;
 bonus->val = 42;
 auto second = model.queryBonuses(id, throwing, "warm-selected");
 ASSERT_EQ(second->size(), 1);
 EXPECT_EQ(second->front()->val, 3);
 EXPECT_NE(second->front().get(), first->front().get());
 EXPECT_EQ(sibling.queryBonuses(id, throwing, "warm-selected")->front()->val, 3);
 model.invalidateUnit(id);
 EXPECT_NE(model.nodeVersion(id), version);
 EXPECT_EQ(sibling.nodeVersion(id), version);
 EXPECT_EQ(model.queryBonuses(id, selected(), "warm-empty")->size(), 1);
 EXPECT_TRUE(sibling.queryBonuses(id, throwing, "warm-empty")->empty());
 EXPECT_EQ(calls, 0);
}

TEST(OrdinaryPreviewDefinitionGuardTest, ValidEmptyAcceptedCacheRecordsEmptyRequestsWithoutCallingSelector)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
 auto evaluation = BonusEvaluationSnapshot::capture({&native}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType();
  facts.originalOwner = node.getOwner();
  return facts;
 });
 const auto node = evaluation.graph().roots().front();
 auto session = OwnedBonusGraphSession::capture(evaluation);
 const auto version = session.nodeVersion(node);
 const auto revision = session.revision();
 int calls = 0;
 CSelector throwing = [&](const Bonus *) -> bool
 {
  ++calls;
  throw std::runtime_error("An empty accepted list must not invoke its selector");
 };
 EXPECT_TRUE(session.queryMembership(node, throwing).empty());
 EXPECT_TRUE(session.queryMembership(node, throwing, "empty").empty());
 EXPECT_TRUE(session.queryMembership(node, throwing, "empty").empty());
 EXPECT_EQ(calls, 0);
 EXPECT_EQ(session.nodeVersion(node), version);
 EXPECT_EQ(session.revision(), revision);
 const auto observed = session.evaluation();
 const auto & cache = observed.graph().nodeCaches().at(node);
 EXPECT_TRUE(cache.valid);
 EXPECT_TRUE(cache.accepted.empty());
 EXPECT_TRUE(cache.requests.at("empty").empty());
 EXPECT_FALSE(cache.requests.contains(""));
 session.invalidateNodeAndDescendants(node);
 const auto invalidated = session.nodeVersion(node);
 EXPECT_TRUE(session.queryMembership(node, throwing, "empty").empty());
 EXPECT_EQ(calls, 0);
 EXPECT_EQ(session.nodeVersion(node), invalidated);
 EXPECT_TRUE(session.evaluation().graph().nodeCaches().at(node).valid);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryWarmSelectorFailureKeepsPublicationAndNestedValues)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto bonus = timedEffect();
 auto nested = timedEffect();
 BonusParametersOnCombatEvent parameters;
 parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested, false});
 bonus->parameters = std::make_shared<BonusParameters>(parameters);
 source->addNewBonus(bonus);
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 auto & model = captured.first;
 const auto id = source->unitId();
 ASSERT_EQ(model.queryBonuses(id, selected(), "warm")->size(), 1);
 auto sibling = model;
 const auto * before = &model.evaluation();
 const auto version = model.nodeVersion(id);
 const auto revision = model.revision();
 int calls = 0;
 CSelector corrupt = [&](const Bonus * value) -> bool
 {
  ++calls;
  if(!selected()(value)) return false;
  const auto & effect = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(value->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
  const_cast<Bonus *>(effect.bonus.get())->val = 999;
  throw std::runtime_error("selector failure");
 };
 EXPECT_THROW(model.queryBonuses(id, corrupt, "failed"), std::runtime_error);
 EXPECT_EQ(&model.evaluation(), before);
 EXPECT_THROW(model.queryBonuses(id, corrupt), std::runtime_error);
 EXPECT_EQ(&model.evaluation(), before);
 EXPECT_GT(calls, 0);
 EXPECT_EQ(model.nodeVersion(id), version);
 EXPECT_EQ(model.revision(), revision);
 for(auto * branch : {&model, &sibling})
 {
  auto values = branch->queryBonuses(id, selected(), "failed");
  ASSERT_EQ(values->size(), 1);
  const auto & effect = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(values->front()->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
  EXPECT_EQ(effect.bonus->val, 3);
 }
 EXPECT_EQ(nested->val, 3);
 model.invalidateUnit(id);
 const auto * cold = &model.evaluation();
 EXPECT_THROW(model.queryBonuses(id, corrupt, "failed-cold"), std::runtime_error);
 EXPECT_EQ(&model.evaluation(), cold);
 EXPECT_EQ(model.queryBonuses(id, selected(), "failed-cold")->size(), 1);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryWarmRequestPreservesCopyBankIdentityAcrossForks)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 source->addNewBonus(timedEffect());
 auto parent = HypotheticFrameArena::captureOrdinary(*battle());
 const auto id = source->unitId();
 auto unit = parent->unit(id);
 auto first = unit->getAllBonuses(selected(), "stable");
 ASSERT_EQ(first->size(), 1);
 auto child = parent->fork();
 auto sibling = parent->fork();
 int calls = 0;
 CSelector throwing = [&](const Bonus *) -> bool
 {
  ++calls;
  throw std::runtime_error("A copied warm request must not invoke its selector");
 };
 auto retained = unit->getAllBonuses(throwing, "stable");
 ASSERT_EQ(retained->size(), 1);
 EXPECT_EQ(retained->front().get(), first->front().get());
 auto childValues = child->unit(id)->getAllBonuses(throwing, "stable");
 auto siblingValues = sibling->unit(id)->getAllBonuses(throwing, "stable");
 ASSERT_EQ(childValues->size(), 1);
 ASSERT_EQ(siblingValues->size(), 1);
 EXPECT_NE(childValues->front().get(), first->front().get());
 EXPECT_NE(siblingValues->front().get(), first->front().get());
 EXPECT_NE(childValues->front().get(), siblingValues->front().get());
 EXPECT_EQ(child->unit(id)->getAllBonuses(throwing, "stable")->front().get(), childValues->front().get());
 EXPECT_EQ(calls, 0);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinarySharedEvaluationKeepsForkCachesAndEpochsLocal)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 source->addNewBonus(timedEffect());
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 auto & parent = captured.first;
 auto child = parent;
 auto sibling = parent;
 const auto id = source->unitId();
 const auto * shared = &parent.evaluation();
 EXPECT_EQ(&child.evaluation(), shared);
 EXPECT_EQ(&sibling.evaluation(), shared);
 const auto version = parent.nodeVersion(id);
 EXPECT_TRUE(child.queryBonuses(id, Selector::none, "branch-key")->empty());
 EXPECT_EQ(parent.queryBonuses(id, selected(), "branch-key")->size(), 1);
 EXPECT_EQ(sibling.queryBonuses(id, selected(), "branch-key")->size(), 1);
 EXPECT_EQ(&parent.evaluation(), shared);
 EXPECT_EQ(&child.evaluation(), shared);
 EXPECT_EQ(&sibling.evaluation(), shared);
 child.invalidateUnit(id);
 EXPECT_NE(child.nodeVersion(id), version);
 EXPECT_EQ(parent.nodeVersion(id), version);
 EXPECT_EQ(sibling.nodeVersion(id), version);
 EXPECT_EQ(&child.evaluation(), shared);
 EXPECT_EQ(child.queryBonuses(id, selected(), "branch-key")->size(), 1);
 EXPECT_EQ(&child.evaluation(), shared);
 const auto childVersion = child.nodeVersion(id);
 auto grandchild = child;
 EXPECT_EQ(&grandchild.evaluation(), shared);
 EXPECT_EQ(grandchild.nodeVersion(id), childVersion);
 EXPECT_EQ(grandchild.queryBonuses(id, selected(), "branch-key")->size(), 1);
 EXPECT_EQ(parent.nodeVersion(id), version);
 EXPECT_EQ(sibling.nodeVersion(id), version);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinarySharedEvaluationReplacesOnlyChangedBranchFacts)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 10);
 ASSERT_NE(source, nullptr);
 ASSERT_NE(recipient, nullptr);
 auto scaled = timedEffect();
 scaled->val = 1;
 scaled->updater = std::make_shared<TimesStackSizeUpdater>();
 source->addNewBonus(scaled);
 auto adjacent = timedEffect();
 auto limiter = std::make_shared<UnitAdjacentLimiter>();
 limiter->targetUnit = source->creatureId();
 adjacent->limiter = limiter;
 recipient->addNewBonus(adjacent);
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 auto & parent = captured.first;
 auto child = parent;
 auto sibling = parent;
 const auto sourceId = source->unitId();
 const auto recipientId = recipient->unitId();
 const auto * shared = &parent.evaluation();
 ASSERT_EQ(parent.getAllBonuses(sourceId, selected())->size(), 1);
 EXPECT_EQ(parent.getAllBonuses(sourceId, selected())->front()->val, 10);
 ASSERT_EQ(parent.getAllBonuses(recipientId, selected())->size(), 1);
 const auto before = parent.units();
 const auto revision = parent.revision();
 int64_t damage = source->getMaxHealth();
 source->damage(damage);
 ASSERT_EQ(source->getCount(), 9);
 source->position = BattleHex(10, 5);
 scaled->val = 42;
 adjacent->val = 99;
 EXPECT_EQ(parent.getAllBonuses(sourceId, selected())->front()->val, 10);
 EXPECT_EQ(parent.getAllBonuses(recipientId, selected())->front()->val, 3);
 auto observations = child.units();
 for(auto & facts : observations)
  if(facts.id == sourceId) facts = child.observeUnit(sourceId, *source);
 ASSERT_TRUE(child.synchronizeUnits(observations));
 EXPECT_NE(&child.evaluation(), shared);
 EXPECT_EQ(&parent.evaluation(), shared);
 EXPECT_EQ(&sibling.evaluation(), shared);
 EXPECT_TRUE(child.evaluation().graph().sameOrigin(parent.evaluation().graph()));
 EXPECT_EQ(parent.units(), before);
 EXPECT_EQ(sibling.units(), before);
 EXPECT_EQ(parent.revision(), revision);
 EXPECT_EQ(sibling.revision(), revision);
 EXPECT_GT(child.revision(), revision);
 EXPECT_EQ(child.getAllBonuses(sourceId, selected())->front()->val, 9);
 EXPECT_TRUE(child.getAllBonuses(recipientId, selected())->empty());
 EXPECT_EQ(parent.getAllBonuses(sourceId, selected())->front()->val, 10);
 EXPECT_EQ(sibling.getAllBonuses(sourceId, selected())->front()->val, 10);
 EXPECT_EQ(parent.getAllBonuses(recipientId, selected())->front()->val, 3);
 EXPECT_EQ(sibling.getAllBonuses(recipientId, selected())->front()->val, 3);
 const auto * changed = &child.evaluation();
 EXPECT_FALSE(child.synchronizeUnits(observations));
 EXPECT_EQ(&child.evaluation(), changed);
 auto invalid = observations;
 for(auto & facts : invalid) if(facts.id == sourceId) facts.count = 0;
 EXPECT_THROW(child.synchronizeUnits(invalid), std::invalid_argument);
 EXPECT_EQ(&child.evaluation(), changed);
 EXPECT_EQ(child.units(), observations);
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinarySharedEvaluationKeepsBonusReplacementAndRetainedSnapshotLocal)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto spellBonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
  BonusSource::SPELL_EFFECT, 3, SpellID(SpellID::HASTE));
 spellBonus->turnsRemain = 3;
 source->addNewBonus(spellBonus);
 const auto selectedSpell = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 const auto id = source->unitId();
 auto captured = OwnedBattleBonusModel::captureForOrdinarySeed(*battle());
 auto & parent = captured.first;
 auto child = parent;
 auto sibling = parent;
 const auto * shared = &parent.evaluation();
 auto exported = parent.evaluation();
 const auto sourceFacts = std::find_if(parent.units().begin(), parent.units().end(),
  [id](const auto & facts) { return facts.id == id; });
 ASSERT_NE(sourceFacts, parent.units().end());
 const auto sourceNode = sourceFacts->node;
 auto replacement = *parent.queryBonuses(id, selectedSpell)->front();
 replacement.turnsRemain = 7;
 const auto event = parent.makeBonusEvent({replacement});
 ASSERT_TRUE(child.applyBonusEvent(id, event, false));
 EXPECT_NE(&child.evaluation(), shared);
 EXPECT_EQ(&parent.evaluation(), shared);
 EXPECT_EQ(&sibling.evaluation(), shared);
 EXPECT_EQ(child.queryBonuses(id, selectedSpell)->front()->turnsRemain, 7);
 EXPECT_EQ(parent.queryBonuses(id, selectedSpell)->front()->turnsRemain, 3);
 EXPECT_EQ(sibling.queryBonuses(id, selectedSpell)->front()->turnsRemain, 3);
 EXPECT_EQ(exported.getAllBonuses(sourceNode, selectedSpell)->front()->turnsRemain, 3);
 auto survivor = [&]()
 {
  auto temporary = child;
  return OwnedBattleBonusModel(temporary);
 }();
 EXPECT_EQ(&survivor.evaluation(), &child.evaluation());
 child = parent;
 EXPECT_EQ(&child.evaluation(), shared);
 EXPECT_NE(&survivor.evaluation(), shared);
 EXPECT_EQ(survivor.queryBonuses(id, selectedSpell)->front()->turnsRemain, 7);
 EXPECT_EQ(source->getAllBonuses(selectedSpell)->front()->turnsRemain, 3);
}

TEST(OrdinaryPreviewDefinitionGuardTest, SharedEvaluationRetainsOuterSelectorTransactionAcrossReentrantReads)
{
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
 native.addNewBonus(timedEffect());
 auto evaluation = BonusEvaluationSnapshot::capture({&native}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType();
  facts.originalOwner = node.getOwner();
  return facts;
 });
 const auto node = evaluation.graph().roots().front();
 auto session = OwnedBonusGraphSession::capture(evaluation);
 ASSERT_EQ(session.query(node, selected(), "warm")->size(), 1);
 auto sibling = session.fork();
 const auto version = session.nodeVersion(node);
 bool entered = false;
 CSelector reentrant = [&](const Bonus * value)
 {
  if(!entered)
  {
   entered = true;
   EXPECT_TRUE(session.query(node, Selector::none, "nested")->empty());
  }
  return selected()(value);
 };
 EXPECT_EQ(session.query(node, reentrant, "outer")->size(), 1);
 EXPECT_TRUE(entered);
 EXPECT_EQ(session.nodeVersion(node), version);
 EXPECT_EQ(sibling.nodeVersion(node), version);
 const auto after = session.evaluation();
 const auto & requests = after.graph().nodeCaches().at(node).requests;
 EXPECT_TRUE(requests.contains("outer"));
 EXPECT_FALSE(requests.contains("nested"));
 EXPECT_EQ(sibling.query(node, selected(), "nested")->size(), 1);
 CSelector throwing = [](const Bonus *) -> bool { throw std::runtime_error("selector failure"); };
 EXPECT_THROW(session.query(node, throwing, "failed"), std::runtime_error);
 EXPECT_EQ(session.nodeVersion(node), version);
 EXPECT_FALSE(session.evaluation().graph().nodeCaches().at(node).requests.contains("failed"));
 EXPECT_EQ(session.query(node, selected(), "failed")->size(), 1);
 EXPECT_EQ(sibling.query(node, selected(), "failed")->size(), 1);
}


#include "lib/bonuses/BonusEvaluationFacts.h"

TEST(OrdinaryPreviewDefinitionGuardTest, CapturedFactionTablePreservesLegacyOverrideAndMissingFactSemantics)
{
 using Table = std::map<CreatureID, FactionID>;
 static_assert(std::is_const_v<decltype(BonusLimiterFacts::capturedCreatureSourceFactions)::element_type>);
 Bonus subject;
 subject.source = BonusSource::CREATURE_ABILITY;
 subject.sid = CreatureID(37);
 BonusList empty;
 BonusLimiterFacts facts;
 facts.capturedCreatureSourceFactions = CapturedCreatureSourceFactionTable::capture(Table{{CreatureID(37), FactionID(2)}});
 EXPECT_EQ(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), FactionID(2));
 facts.creatureSourceFactions.emplace(Table{{CreatureID(37), FactionID(4)}});
 SnapshotBonusLimiterContext legacy(facts, subject, empty, empty);
 facts.creatureSourceFactions->at(CreatureID(37)) = FactionID(5);
 EXPECT_EQ(legacy.sourceCreatureFaction(), FactionID(4));
 EXPECT_EQ(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), FactionID(5));
 facts.creatureSourceFactions->clear();
 EXPECT_THROW(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), std::out_of_range);
 facts.creatureSourceFactions.reset();
 EXPECT_EQ(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), FactionID(2));
 subject.sid = CreatureID(38);
 EXPECT_THROW(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), std::out_of_range);
 facts.capturedCreatureSourceFactions = CapturedCreatureSourceFactionTable::capture(Table{});
 EXPECT_THROW(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), std::out_of_range);
 facts.capturedCreatureSourceFactions.reset();
 try
 {
  SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction();
  FAIL() << "Missing source-faction facts were accepted";
 }
 catch(const std::logic_error & error)
 {
  EXPECT_STREQ(error.what(), "Missing limiter fact: creature source factions");
 }
 facts.creatureSourceFactions.emplace(Table{{CreatureID(38), FactionID(7)}});
 EXPECT_EQ(SnapshotBonusLimiterContext(facts, subject, empty, empty).sourceCreatureFaction(), FactionID(7));
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, CapturedFactionTableSharesOwnedValuesAcrossNodesAndSurvivesNativeChanges)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto * native = const_cast<CCreature *>(source->creatureId().toCreature());
 const auto * other = creatureByName("core:pikeman").toCreature();
 ASSERT_NE(native->getFactionID(), other->getFactionID());
 const auto id = native->getId();
 const auto originalFaction = native->getFactionID();
 std::optional<BonusEvaluationFactCapture> definitions(BonusEvaluationFactCapture::captureDefinitions());
 auto stackFacts = definitions->captureNode(*source);
 auto creatureFacts = definitions->captureNode(*native);
 auto battleFacts = definitions->captureNode(*battle());
 ASSERT_NE(stackFacts.capturedCreatureSourceFactions, nullptr);
 EXPECT_EQ(stackFacts.capturedCreatureSourceFactions, creatureFacts.capturedCreatureSourceFactions);
 EXPECT_EQ(stackFacts.capturedCreatureSourceFactions, battleFacts.capturedCreatureSourceFactions);
 EXPECT_FALSE(stackFacts.creatureSourceFactions.has_value());
 EXPECT_FALSE(creatureFacts.creatureSourceFactions.has_value());
 EXPECT_FALSE(battleFacts.creatureSourceFactions.has_value());
 auto later = BonusEvaluationFactCapture::captureDefinitions().captureNode(*source);
 EXPECT_NE(later.capturedCreatureSourceFactions, stackFacts.capturedCreatureSourceFactions);
 EXPECT_EQ(later.capturedCreatureSourceFactions->at(id), stackFacts.capturedCreatureSourceFactions->at(id));
 CCreature saved;
 OrdinaryPreviewDefinitionGuardTestPeer::copyDefinitionIdentity(saved, *native);
 struct RestoreIdentity
 {
  CCreature & target;
  const CCreature & saved;
  ~RestoreIdentity() { OrdinaryPreviewDefinitionGuardTestPeer::copyDefinitionIdentity(target, saved); }
 } restore{*native, saved};
 OrdinaryPreviewDefinitionGuardTestPeer::copyDefinitionIdentity(*native, *other);
 EXPECT_NE(native->getFactionID(), originalFaction);
 EXPECT_EQ(stackFacts.capturedCreatureSourceFactions->at(id), originalFaction);
 EXPECT_EQ(creatureFacts.capturedCreatureSourceFactions->at(id), originalFaction);
 Bonus subject;
 subject.source = BonusSource::CREATURE_ABILITY;
 subject.sid = id;
 BonusList empty;
 auto retained = std::make_unique<SnapshotBonusLimiterContext>(stackFacts, subject, empty, empty);
 std::weak_ptr<const CapturedCreatureSourceFactionTable> lifetime = stackFacts.capturedCreatureSourceFactions;
 definitions.reset();
 stackFacts = {}; creatureFacts = {}; battleFacts = {};
 EXPECT_FALSE(lifetime.expired());
 EXPECT_EQ(retained->sourceCreatureFaction(), originalFaction);
 retained.reset();
 EXPECT_TRUE(lifetime.expired());
}

TEST(OrdinaryPreviewDefinitionGuardTest, CapturedFactionTableSurvivesSnapshotForkExportAndReplacement)
{
 using Table = std::map<CreatureID, FactionID>;
 std::weak_ptr<const CapturedCreatureSourceFactionTable> lifetime;
 std::optional<OwnedBonusGraphSession> survivor;
 std::optional<BonusEvaluationSnapshot> exported;
 BonusGraphSnapshot::NodeId nodeId = 0;
 {
  CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
  auto value = timedEffect();
  value->source = BonusSource::CREATURE_ABILITY;
  value->sid = CreatureID(37);
  value->limiter = std::make_shared<FactionLimiter>();
  native.addNewBonus(value);
  BonusLimiterFacts facts;
  facts.type = native.getNodeType();
  facts.originalOwner = native.getOwner();
  facts.factionProvider = true;
  facts.faction = FactionID(2);
  facts.capturedCreatureSourceFactions = CapturedCreatureSourceFactionTable::capture(Table{{CreatureID(37), FactionID(2)}});
  lifetime = facts.capturedCreatureSourceFactions;
  auto capture = [&](const CBonusSystemNode &) { return facts; };
  auto evaluation = BonusEvaluationSnapshot::capture({&native}, capture);
  nodeId = evaluation.graph().roots().front();
  const auto owners = facts.capturedCreatureSourceFactions.use_count();
  {
   auto replacement = evaluation.withGraph(evaluation.graph());
   EXPECT_EQ(facts.capturedCreatureSourceFactions.use_count(), owners + 1);
   EXPECT_EQ(replacement.getAllBonuses(nodeId, Selector::all)->size(), 1);
  }
  EXPECT_EQ(facts.capturedCreatureSourceFactions.use_count(), owners);
  auto parent = OwnedBonusGraphSession::capture(std::move(evaluation));
  survivor.emplace(parent.fork());
  exported.emplace(parent.evaluation());
  facts.faction = FactionID(4);
  auto different = BonusEvaluationSnapshot::capture({&native}, capture);
  EXPECT_TRUE(different.getAllBonuses(nodeId, Selector::all)->empty());
  EXPECT_EQ(parent.query(nodeId, Selector::all)->size(), 1);
  EXPECT_EQ(survivor->query(nodeId, Selector::all)->size(), 1);
  const auto valueId = exported->graph().nodes().at(nodeId).exported.front();
  survivor->setTurnsRemain(valueId, 7);
  EXPECT_EQ(survivor->query(nodeId, Selector::all)->front()->turnsRemain, 7);
  EXPECT_EQ(parent.query(nodeId, Selector::all)->front()->turnsRemain, 3);
  value->val = 99;
 }
 EXPECT_FALSE(lifetime.expired());
 EXPECT_EQ(survivor->query(nodeId, Selector::all)->front()->val, 3);
 EXPECT_EQ(exported->getAllBonuses(nodeId, Selector::all)->front()->turnsRemain, 3);
 survivor.reset();
 EXPECT_FALSE(lifetime.expired());
 exported.reset();
 EXPECT_TRUE(lifetime.expired());
}

TEST_F(OrdinaryPreviewDefinitionGuardBattleTest, OrdinaryAndFixtureDefinitionModesPreservePublicationAcrossAllArenaPaths)
{
 auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
 ASSERT_NE(source, nullptr);
 auto value = timedEffect();
 value->source = BonusSource::SPELL_EFFECT;
 value->sid = SpellID(SpellID::HASTE);
 source->addNewBonus(value);
 const auto choose = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE));
 const auto id = source->unitId();
 const auto nativeBefore = source->save();
 const auto * nativeDefinition = source->unitType();
 for(bool ordinary : {false, true})
 {
  auto parent = ordinary ? HypotheticFrameArena::captureOrdinary(*battle()) : HypotheticFrameArena::capture(*battle());
  auto child = parent->fork();
  auto sibling = parent->fork();
  const auto * descriptor = child->unit(id)->unitType();
  const auto * owned = dynamic_cast<const OwnedCreature *>(descriptor);
  EXPECT_EQ(owned == nullptr, ordinary);
  if(ordinary) EXPECT_EQ(descriptor, nativeDefinition);
  else EXPECT_NE(descriptor, parent->unit(id)->unitType());
  const auto parentVersion = parent->revision();
  const auto definitionVersion = descriptor->getBonusBearer()->getTreeVersion();
  auto acquired = child->unit(id)->acquireState();
  child->moveUnit(id, BattleHex(7, 5));
  EXPECT_EQ(child->unit(id)->getPosition(), BattleHex(7, 5));
  EXPECT_EQ(parent->unit(id)->getPosition(), BattleHex(5, 5));
  auto replacement = *value; replacement.turnsRemain = 8;
  child->applyBonusEvent(id, child->makeBonusEvent({replacement}), false);
  EXPECT_EQ(child->unit(id)->getAllBonuses(choose)->front()->turnsRemain, 8);
  EXPECT_EQ(sibling->unit(id)->getAllBonuses(choose)->front()->turnsRemain, 3);
  auto changed = child->unit(id)->acquireState();
  int64_t damage = changed->getMaxHealth();
  changed->damage(damage);
  BattleUnitsChanged packet;
  UnitChanges update;
  update.id = id;
  update.operation = BattleChanges::EOperation::UPDATE;
  update.data = changed->save();
  update.healthDelta = -damage;
  packet.changedStacks.push_back(update);
  child->applyStateUpdates(packet);
  EXPECT_EQ(child->unit(id)->getCount(), 9);
  EXPECT_EQ(parent->unit(id)->getCount(), 10);
  const auto beforeInvalid = child->unit(id)->save();
  const auto beforeInvalidRevision = child->revision();
  packet.changedStacks.front().data["unknown-ordinary-context-field"] = JsonNode(true);
  EXPECT_THROW(child->applyStateUpdates(packet), std::invalid_argument);
  EXPECT_EQ(child->unit(id)->save(), beforeInvalid);
  EXPECT_EQ(child->revision(), beforeInvalidRevision);
  child->nextRound();
  EXPECT_EQ(child->unit(id)->unitType(), descriptor);
  EXPECT_EQ(parent->revision(), parentVersion);
  EXPECT_EQ(acquired->getCount(), 10);
  if(ordinary) EXPECT_EQ(descriptor->getBonusBearer()->getTreeVersion(), definitionVersion);
  else EXPECT_GT(descriptor->getBonusBearer()->getTreeVersion(), definitionVersion);
  auto retained = owned ? owned->retain() : std::shared_ptr<const OwnedCreature>{};
  acquired.reset(); changed.reset(); child.reset();
  if(retained) { EXPECT_GT(retained->getBonusBearer()->getTreeVersion(), definitionVersion); }
 }
 EXPECT_EQ(source->save(), nativeBefore);
 EXPECT_EQ(source->getAllBonuses(choose)->front()->turnsRemain, 3);
}


TEST(OrdinaryPreviewDefinitionGuardTest, CustomFactCaptureDetachesMutableFactionTableBeforeSnapshotPublication)
{
 auto mutableSource = std::make_shared<std::map<CreatureID, FactionID>>();
 mutableSource->emplace(CreatureID(37), FactionID(2));
 auto & retainedEntry = mutableSource->at(CreatureID(37));
 CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
 auto value = timedEffect();
 value->source = BonusSource::CREATURE_ABILITY;
 value->sid = CreatureID(37);
 value->limiter = std::make_shared<FactionLimiter>();
 native.addNewBonus(value);
 BonusLimiterFacts facts;
 facts.type = native.getNodeType();
 facts.originalOwner = native.getOwner();
 facts.factionProvider = true;
 facts.faction = FactionID(2);
 facts.capturedCreatureSourceFactions = CapturedCreatureSourceFactionTable::capture(*mutableSource);
 const auto captured = facts.capturedCreatureSourceFactions;
 auto evaluation = BonusEvaluationSnapshot::capture({&native}, [&](const CBonusSystemNode &) { return facts; });
 const auto node = evaluation.graph().roots().front();
 auto parent = OwnedBonusGraphSession::capture(evaluation);
 auto child = parent.fork();
 retainedEntry = FactionID(4);
 mutableSource->emplace(CreatureID(38), FactionID(5));
 facts.capturedCreatureSourceFactions = CapturedCreatureSourceFactionTable::capture(*mutableSource);
 EXPECT_EQ(captured->at(CreatureID(37)), FactionID(2));
 EXPECT_THROW(captured->at(CreatureID(38)), std::out_of_range);
 EXPECT_EQ(facts.capturedCreatureSourceFactions->at(CreatureID(37)), FactionID(4));
 EXPECT_EQ(evaluation.getAllBonuses(node, Selector::all)->size(), 1);
 EXPECT_EQ(parent.query(node, Selector::all)->size(), 1);
 EXPECT_EQ(child.query(node, Selector::all)->size(), 1);
 auto replacement = BonusEvaluationSnapshot::capture({&native}, [&](const CBonusSystemNode &) { return facts; });
 EXPECT_TRUE(replacement.getAllBonuses(node, Selector::all)->empty());
 mutableSource.reset();
 EXPECT_EQ(child.query(node, Selector::all)->front()->val, 3);
}
