#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/bonuses/BonusGraphSnapshot.h"
#include "lib/bonuses/OwnedBonusGraphSession.h"
#include "lib/battle/OwnedBattleBonusModel.h"
#include "lib/bonuses/CBonusSystemNode.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Updaters.h"

namespace
{
std::shared_ptr<Bonus> cacheBonus(int value = 7)
{
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, value, ArtifactID(799));
	result->turnsRemain = 3;
	return result;
}
CSelector cacheSelector()
{
	return Selector::source(BonusSource::OTHER, ArtifactID(799));
}
}

TEST(OwnedCacheCaptureTest, InitialValidEmptyCacheIsNotCold)
{
	CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
	auto graph = BonusGraphSnapshot::capture({&node});
	const auto & cache = graph.nodeCaches().at(graph.roots().front());
	EXPECT_TRUE(cache.valid);
	EXPECT_TRUE(cache.accepted.empty());
	EXPECT_TRUE(cache.requests.empty());
}

TEST(OwnedCacheCaptureTest, CapturePrecedesVisitorsWithoutWarmingColdNodes)
{
	CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
	node.addNewBonus(cacheBonus());
	auto cold = BonusGraphSnapshot::capture({&node});
	EXPECT_FALSE(cold.nodeCaches().at(cold.roots().front()).valid);
	auto beforeVisitor = BonusGraphSnapshot::capture({&node}, [&](size_t, const CBonusSystemNode & current)
	{
		current.getAllBonuses(cacheSelector(), "visitor");
	});
	const auto & cache = beforeVisitor.nodeCaches().at(beforeVisitor.roots().front());
	EXPECT_FALSE(cache.valid);
	EXPECT_TRUE(cache.accepted.empty());
	EXPECT_TRUE(cache.requests.empty());
	auto warmed = BonusGraphSnapshot::capture({&node});
	EXPECT_TRUE(warmed.nodeCaches().at(warmed.roots().front()).valid);
	EXPECT_EQ(warmed.nodeCaches().at(warmed.roots().front()).requests.at("visitor").size(), 1);
}

TEST(OwnedCacheCaptureTest, RawAcceptedAndRequestListsShareOneOwnedIdentity)
{
	std::optional<BonusGraphSnapshot> graph;
	{
		CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
		auto value = cacheBonus();
		node.addNewBonus(value);
		node.getAllBonuses(cacheSelector(), "raw");
		graph = BonusGraphSnapshot::capture({&node});
		const auto & record = graph->nodes().at(graph->roots().front());
		const auto & cache = graph->nodeCaches().at(graph->roots().front());
		ASSERT_EQ(cache.accepted.size(), 1);
		ASSERT_EQ(record.exported.size(), 1);
		EXPECT_EQ(cache.accepted, cache.requests.at("raw"));
		EXPECT_EQ(cache.accepted.front(), graph->graphValueIds().at(record.exported.front()));
		EXPECT_EQ(graph->capturedValues().at(cache.accepted.front()), graph->bonuses().at(record.exported.front()));
		EXPECT_NE(graph->capturedValues().at(cache.accepted.front()).get(), value.get());
		value->val = 55;
	}
	const auto & cache = graph->nodeCaches().at(graph->roots().front());
	EXPECT_EQ(graph->capturedValues().at(cache.accepted.front())->val, 7);
}

TEST(OwnedCacheCaptureTest, InvalidRequestPayloadsAreNotRevived)
{
	CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
	node.addNewBonus(cacheBonus());
	node.getAllBonuses(cacheSelector(), "old");
	node.nodeHasChanged();
	auto graph = BonusGraphSnapshot::capture({&node});
	const auto & cache = graph.nodeCaches().at(graph.roots().front());
	EXPECT_FALSE(cache.valid);
	EXPECT_TRUE(cache.accepted.empty());
	EXPECT_TRUE(cache.requests.empty());
}

TEST(OwnedCacheCaptureTest, DetachedCopiesPreserveNestedAndComponentAliases)
{
	auto leaf = cacheBonus();
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters.push_back(std::make_shared<IUpdater>());
	leaf->updater = composite;
	auto root = cacheBonus();
	root->updater = composite;
	BonusParametersOnCombatEvent parameters;
	parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{leaf, false});
	root->parameters = std::make_shared<BonusParameters>(parameters);
	auto copied = BonusGraphSnapshot::detachValues({root, leaf, root});
	ASSERT_EQ(copied.size(), 3);
	EXPECT_EQ(copied[0], copied[2]);
	EXPECT_NE(copied[0], root);
	EXPECT_EQ(copied[0]->updater, copied[1]->updater);
	EXPECT_NE(copied[0]->updater, composite);
	const auto & nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(copied[0]->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
	EXPECT_EQ(nested.bonus, copied[1]);
	const auto * copiedUpdater = dynamic_cast<const CompositeUpdater *>(copied[0]->updater.get());
	ASSERT_NE(copiedUpdater, nullptr);
	EXPECT_NE(copiedUpdater->updaters.at(0), composite->updaters.at(0));
	copied[1]->val = 99;
	EXPECT_EQ(leaf->val, 7);
}

class OwnedCacheUpdaterCaptureTest : public BattleTestFixture
{
public:
	void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
};

TEST_F(OwnedCacheUpdaterCaptureTest, CacheOnlyUpdaterOutputsKeepDistinctIdentitiesAndSharedComponents)
{
	auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
	auto updater = std::make_shared<TimesStackSizeUpdater>();
	for(int i = 0; i < 2; ++i)
	{
		auto value = cacheBonus();
		value->updater = updater;
		stack->addNewBonus(value);
	}
	auto native = stack->getAllBonuses(cacheSelector(), "updated");
	ASSERT_EQ(native->size(), 2);
	auto graph = BonusGraphSnapshot::capture({stack});
	const auto & cache = graph.nodeCaches().at(graph.roots().front());
	const auto & request = cache.requests.at("updated");
	ASSERT_EQ(request.size(), 2);
	EXPECT_NE(request[0], request[1]);
	const auto & first = graph.capturedValues().at(request[0]);
	const auto & second = graph.capturedValues().at(request[1]);
	EXPECT_EQ(first->val, native->front()->val);
	EXPECT_EQ(first->val, second->val);
	EXPECT_EQ(first->updater, second->updater);
	EXPECT_NE(first.get(), native->front().get());
	for(const auto & raw : graph.bonuses())
	{
		EXPECT_NE(first, raw);
		EXPECT_NE(second, raw);
	}
}

TEST(OwnedCacheCaptureTest, ProxyListsShareGraphAliasesWithoutAddingGraphRoots)
{
	CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
	auto raw = cacheBonus();
	node.addNewBonus(raw);
	auto cacheOnly = cacheBonus(13);
	BonusParametersOnCombatEvent parameters;
	parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{raw, false});
	cacheOnly->parameters = std::make_shared<BonusParameters>(parameters);
	auto held = std::make_shared<BonusList>();
	held->push_back(cacheOnly);
	held->push_back(raw);
	auto graph = BonusGraphSnapshot::capture({&node}, {}, {held, held});
	ASSERT_EQ(graph.bonuses().size(), 1);
	ASSERT_EQ(graph.capturedValues().size(), 2);
	ASSERT_EQ(graph.capturedExtraLists().size(), 2);
	const auto & ids = graph.capturedExtraLists().front();
	EXPECT_EQ(ids, graph.capturedExtraLists().back());
	ASSERT_EQ(ids.size(), 2);
	EXPECT_EQ(ids[1], graph.graphValueIds().front());
	const auto & copy = graph.capturedValues().at(ids[0]);
	const auto & nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(copy->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
	EXPECT_EQ(nested.bonus, graph.bonuses().front());
	EXPECT_NE(copy, cacheOnly);
	EXPECT_FALSE(graph.nodeCaches().at(graph.roots().front()).valid);
}

TEST(OwnedCacheCaptureTest, UnknownCacheOnlyComponentsFailBeforeVisitors)
{
	class UnknownUpdater final : public IUpdater {};
	CBonusSystemNode node(BonusNodeType::GLOBAL_EFFECTS);
	node.addNewBonus(cacheBonus());
	auto held = std::make_shared<BonusList>();
	auto unsupported = cacheBonus();
	unsupported->updater = std::make_shared<UnknownUpdater>();
	held->push_back(unsupported);
	bool visited = false;
	EXPECT_THROW(BonusGraphSnapshot::capture({&node}, [&](size_t, const CBonusSystemNode &) { visited = true; }, {held}), std::runtime_error);
	EXPECT_FALSE(visited);
	auto after = BonusGraphSnapshot::capture({&node});
	EXPECT_FALSE(after.nodeCaches().at(after.roots().front()).valid);
	EXPECT_EQ(after.bonuses().size(), 1);
}

TEST_F(OwnedCacheUpdaterCaptureTest, RawDurationAliasChangesDoNotRewriteCachedUpdaterClones)
{
	auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10);
	auto raw = cacheBonus();
	auto updated = cacheBonus();
	updated->updater = std::make_shared<TimesStackSizeUpdater>();
	stack->addNewBonus(raw);
	stack->addNewBonus(updated);
	CSelector three = cacheSelector().And([](const Bonus * value) { return value->turnsRemain == 3; });
	ASSERT_EQ(stack->getAllBonuses(three, "three-turns")->size(), 2);
	auto evaluation = BonusEvaluationSnapshot::capture({stack});
	const auto node = evaluation.graph().roots().front();
	auto session = OwnedBonusGraphSession::capture(evaluation);
	for(auto id : evaluation.graph().nodes().at(node).exported)
		if(cacheSelector()(evaluation.graph().bonuses().at(id).get()))
			session.setTurnsRemain(id, 2);
	raw->turnsRemain = 2;
	updated->turnsRemain = 2;
	auto actual = session.query(node, three, "three-turns");
	auto expected = stack->getAllBonuses(three, "three-turns");
	ASSERT_EQ(actual->size(), 2);
	ASSERT_EQ(expected->size(), 2);
	for(size_t i = 0; i < actual->size(); ++i)
	{
		EXPECT_EQ((*actual)[i]->turnsRemain, (*expected)[i]->turnsRemain);
		EXPECT_EQ((*actual)[i]->val, (*expected)[i]->val);
	}
	auto newKey = session.query(node, three, "three-turns-new");
	ASSERT_EQ(newKey->size(), 1);
	EXPECT_EQ(newKey->front()->turnsRemain, 3);
	EXPECT_EQ(stack->getAllBonuses(three, "three-turns-new")->size(), 1);
	stack->nodeHasChanged();
	session.invalidateNodeAndDescendants(node);
	EXPECT_TRUE(session.query(node, three, "three-turns")->empty());
	EXPECT_TRUE(stack->getAllBonuses(three, "three-turns")->empty());
}

TEST(OwnedCacheCaptureTest, SyntheticEpochExhaustionPreservesValidRequestAndValues)
{
	CBonusSystemNode native(BonusNodeType::GLOBAL_EFFECTS);
	native.addNewBonus(cacheBonus());
	native.getAllBonuses(cacheSelector(), "retained");
	auto evaluation = BonusEvaluationSnapshot::capture({&native}, [](const CBonusSystemNode & node)
	{
		BonusLimiterFacts facts;
		facts.type = node.getNodeType();
		facts.originalOwner = node.getOwner();
		return facts;
	});
	const auto node = evaluation.graph().roots().front();
	// Synthetic owned boundary fixture; the native oracle remains unchanged.
	auto & cache = const_cast<BonusGraphSnapshot::NodeCache &>(evaluation.graph().nodeCaches().at(node));
	cache.epoch = std::numeric_limits<int32_t>::max();
	auto session = OwnedBonusGraphSession::capture(evaluation);
	const auto revision = session.revision();
	EXPECT_THROW(session.invalidateNodeAndDescendants(node), std::overflow_error);
	EXPECT_EQ(session.nodeVersion(node), std::numeric_limits<int32_t>::max());
	EXPECT_EQ(session.revision(), revision);
	CSelector reject = [](const Bonus *) { return false; };
	auto retained = session.query(node, reject, "retained");
	ASSERT_EQ(retained->size(), 1);
	EXPECT_EQ(retained->front()->val, 7);
	EXPECT_TRUE(session.query(node, reject)->empty());
	EXPECT_EQ(native.getAllBonuses(cacheSelector(), "retained")->size(), 1);
}

TEST_F(OwnedCacheUpdaterCaptureTest, FreshModelDiagnosticsDoNotRefreshObservableNativeCache)
{
 auto * stack=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 auto value=cacheBonus(1); value->updater=std::make_shared<TimesStackSizeUpdater>(); stack->addNewBonus(value);
 auto cold=OwnedBattleBonusModel::capture(*battle());
 EXPECT_EQ(cold.getAllBonuses(stack->unitId(),cacheSelector())->front()->val,10);
 ASSERT_EQ(stack->getAllBonuses(cacheSelector())->front()->val,10);
 auto model=OwnedBattleBonusModel::capture(*battle());
 ASSERT_EQ(model.queryBonuses(stack->unitId(),cacheSelector())->front()->val,10);
 const auto epoch=model.nodeVersion(stack->unitId());
 int64_t damage=stack->getMaxHealth(); stack->damage(damage); ASSERT_EQ(stack->getCount(),9);
 auto facts=model.units();
 for(auto & fact:facts)
  fact=model.observeUnit(fact.id,*dynamic_cast<const battle::CUnitState *>(battle()->battleGetUnitByID(fact.id)));
 ASSERT_TRUE(model.synchronizeUnits(facts));
 auto coldFacts=cold.units();
 for(auto & fact:coldFacts)
  fact=cold.observeUnit(fact.id,*dynamic_cast<const battle::CUnitState *>(battle()->battleGetUnitByID(fact.id)));
 ASSERT_TRUE(cold.synchronizeUnits(coldFacts));
 EXPECT_EQ(cold.queryBonuses(stack->unitId(),cacheSelector())->front()->val,9);
 EXPECT_EQ(model.getAllBonuses(stack->unitId(),cacheSelector())->front()->val,9);
 EXPECT_EQ(model.queryBonuses(stack->unitId(),cacheSelector())->front()->val,10);
 EXPECT_EQ(stack->getAllBonuses(cacheSelector())->front()->val,10);
 EXPECT_EQ(model.nodeVersion(stack->unitId()),epoch);
 battle()->updateUnit(stack->unitId(),stack->save(),-1);
 model.invalidateUnit(stack->unitId());
 EXPECT_NE(model.nodeVersion(stack->unitId()),epoch);
 EXPECT_EQ(model.queryBonuses(stack->unitId(),cacheSelector())->front()->val,9);
 EXPECT_EQ(stack->getAllBonuses(cacheSelector())->front()->val,9);
 EXPECT_EQ(model.getAllBonuses(stack->unitId(),cacheSelector())->front()->val,9);
}

TEST_F(OwnedCacheUpdaterCaptureTest, NewlyAdjacentFactsRemainInvisibleUntilRecipientInvalidation)
{
 auto * source=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10);
 auto * recipient=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(9,5),10);
 auto value=cacheBonus(); auto limiter=std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit=source->creatureId(); value->limiter=limiter; recipient->addNewBonus(value);
 ASSERT_TRUE(recipient->getAllBonuses(cacheSelector())->empty());
 auto model=OwnedBattleBonusModel::capture(*battle());
 ASSERT_TRUE(model.queryBonuses(recipient->unitId(),cacheSelector())->empty());
 const auto epoch=model.nodeVersion(recipient->unitId());
 // Scoped facts fixture: no move packet or its battle-wide invalidation.
 source->position=BattleHex(8,5);
 auto facts=model.units();
 for(auto & fact:facts)
  fact=model.observeUnit(fact.id,*dynamic_cast<const battle::CUnitState *>(battle()->battleGetUnitByID(fact.id)));
 ASSERT_TRUE(model.synchronizeUnits(facts));
 EXPECT_EQ(model.getAllBonuses(recipient->unitId(),cacheSelector())->size(),1);
 EXPECT_TRUE(model.queryBonuses(recipient->unitId(),cacheSelector())->empty());
 EXPECT_TRUE(recipient->getAllBonuses(cacheSelector())->empty());
 EXPECT_EQ(model.nodeVersion(recipient->unitId()),epoch);
 battle()->updateUnit(recipient->unitId(),recipient->save(),-1);
 model.invalidateUnit(recipient->unitId());
 EXPECT_EQ(model.queryBonuses(recipient->unitId(),cacheSelector())->size(),1);
 EXPECT_EQ(recipient->getAllBonuses(cacheSelector())->size(),1);
}
