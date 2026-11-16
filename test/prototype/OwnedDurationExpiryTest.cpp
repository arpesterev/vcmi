#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/OwnedBonusGraphSession.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Limiters.h"

class OwnedDurationExpiryTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static std::shared_ptr<Bonus> effect(int duration = BonusDuration::N_TURNS, int turns = 2)
 {
  auto value = std::make_shared<Bonus>(duration, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, SpellID(SpellID::HASTE));
  value->turnsRemain = turns; return value;
 }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 static void sameLists(const BonusList & a, const BonusList & b)
 {
  ASSERT_EQ(a.size(), b.size());
  for(size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i]->toJsonNode(), b[i]->toJsonNode());
 }
 static void same(const OwnedBonusGraphSession & session, const std::vector<CStack *> & units)
 {
  const auto e = session.evaluation();
  for(size_t i = 0; i < units.size(); ++i)
  {
   auto node = e.graph().roots().at(i);
   const auto & exports = e.graph().nodes().at(node).exported;
   const auto & native = units.at(i)->getExportedBonusList();
   ASSERT_EQ(exports.size(), native.size());
   for(size_t j = 0; j < exports.size(); ++j)
    EXPECT_EQ(e.graph().bonuses().at(exports.at(j))->toJsonNode(), native[j]->toJsonNode());
   auto ownedQuery = e.getAllBonuses(node, Selector::all), nativeQuery = units.at(i)->getAllBonuses(Selector::all);
   sameLists(*ownedQuery, *nativeQuery);
  }
 }
 static void sameFrame(HypotheticBattle & frame, CStack * unit)
 {
  auto a = frame.getForUpdate(unit->unitId())->getAllBonuses(Selector::all), b = unit->getAllBonuses(Selector::all);
  sameLists(*a, *b);
 }
};

TEST_F(OwnedDurationExpiryTest, RoundDecrementsRawHiddenExportsAndKeepsInheritedBonuses)
{
 auto * unit = add(); auto hidden = effect(); auto limiter = std::make_shared<CCreatureTypeLimiter>(); limiter->setCreature(creatureByName("core:greenDragon")); hidden->limiter = limiter;
 unit->addNewBonus(hidden); attackerSideHero->addNewBonus(effect());
 auto capture = BonusEvaluationSnapshot::capture({unit}); auto node = capture.graph().roots().at(0);
 auto session = OwnedBonusGraphSession::capture(capture); auto revision = session.revision();
 EXPECT_TRUE(session.expireBonuses(node, OwnedBonusExpiry::Round)); unit->reduceBonusDurations(Bonus::NTurns); same(session, {unit});
 EXPECT_EQ(session.revision(), revision + 1);
 EXPECT_TRUE(session.expireBonuses(node, OwnedBonusExpiry::Round)); unit->reduceBonusDurations(Bonus::NTurns); same(session, {unit});
 revision = session.revision(); EXPECT_FALSE(session.expireBonuses(node, OwnedBonusExpiry::Round)); EXPECT_EQ(session.revision(), revision);
}

TEST_F(OwnedDurationExpiryTest, SharedDescendantsAndAliasesDecrementForEachNativeVisit)
{
 auto * root = add(3); auto * left = add(5); auto * right = add(7); auto * leaf = add(9);
 left->attachTo(*root); right->attachTo(*root); leaf->attachTo(*left); leaf->attachTo(*right);
 struct Detach { CStack * root; CStack * left; CStack * right; CStack * leaf;
  ~Detach() { leaf->detachFrom(*right); leaf->detachFrom(*left); right->detachFrom(*root); left->detachFrom(*root); }
 } detach{root, left, right, leaf};
 auto alias = effect(BonusDuration::N_TURNS, 4); left->addNewBonus(alias); right->addNewBonus(alias); leaf->addNewBonus(alias);
 auto e = BonusEvaluationSnapshot::capture({root, left, right, leaf}); auto session = OwnedBonusGraphSession::capture(e);
 EXPECT_TRUE(session.expireBonuses(e.graph().roots().at(0), OwnedBonusExpiry::Round)); root->reduceBonusDurations(Bonus::NTurns);
 same(session, {root, left, right, leaf}); EXPECT_EQ(alias->turnsRemain, 0);
 EXPECT_TRUE(session.expireBonuses(e.graph().roots().at(0), OwnedBonusExpiry::Round)); root->reduceBonusDurations(Bonus::NTurns);
 same(session, {root, left, right, leaf}); EXPECT_LT(alias->turnsRemain, 0);
}

TEST_F(OwnedDurationExpiryTest, ExpiredNestedAliasesRetainZeroAndForkHistoricalValues)
{
 auto * unit = add(); auto leaf = effect(BonusDuration::N_TURNS, 1); unit->addNewBonus(leaf);
 auto root = effect(BonusDuration::PERMANENT); root->type = BonusType::FLYING;
 BonusParametersOnCombatEvent parameters; parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{leaf, false});
 root->parameters = std::make_shared<BonusParameters>(parameters); unit->addNewBonus(root);
 auto e = BonusEvaluationSnapshot::capture({unit}); auto node = e.graph().roots().at(0);
 auto parent = OwnedBonusGraphSession::capture(e); auto child = parent.fork(); auto sibling = parent.fork();
 auto rootId = e.graph().nodes().at(node).exported.back(); auto event = parent.eventFromBonuses({rootId});
 auto old = child.evaluation(); child.expireBonuses(node, OwnedBonusExpiry::Round); unit->reduceBonusDurations(Bonus::NTurns);
 child.addEventRoot(node, event, 0); unit->addNewBonus(std::make_shared<Bonus>(*root)); same(child, {unit});
 auto now = child.evaluation(); const auto & exports = now.graph().nodes().at(node).exported;
 ASSERT_EQ(exports.size(), 2);
 auto nested = [](const auto & b) { return std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(b->parameters->template toCustom<BonusParametersOnCombatEvent>().effects.at(0)).bonus; };
 EXPECT_EQ(nested(now.graph().bonuses().at(exports.front())), nested(now.graph().bonuses().at(exports.back())));
 EXPECT_EQ(nested(now.graph().bonuses().at(exports.front()))->turnsRemain, 0);
 EXPECT_EQ(nested(old.graph().bonuses().at(rootId))->turnsRemain, 1);
 EXPECT_EQ(nested(parent.evaluation().graph().bonuses().at(rootId))->turnsRemain, 1);
 EXPECT_EQ(nested(sibling.evaluation().graph().bonuses().at(rootId))->turnsRemain, 1);
}

TEST_F(OwnedDurationExpiryTest, PropagationHistoricalMaterializationIsNotRefreshedBeforeExpiry)
{
 auto * source = add(5); auto * recipient = add(7); auto bonus = effect(BonusDuration::N_TURNS, 2);
 bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 bonus->propagationUpdater = std::make_shared<TimesStackSizeUpdater>(); source->addNewBonus(bonus);
 int64_t damage = source->getMaxHealth(); source->damage(damage);
 auto e = BonusEvaluationSnapshot::capture({source, recipient}); auto session = OwnedBonusGraphSession::capture(e);
 session.expireBonuses(e.graph().roots().at(0), OwnedBonusExpiry::Round); source->reduceBonusDurations(Bonus::NTurns); same(session, {source, recipient});
 session.expireBonuses(e.graph().roots().at(0), OwnedBonusExpiry::Round); source->reduceBonusDurations(Bonus::NTurns); same(session, {source, recipient});
 EXPECT_TRUE(recipient->getAllBonuses(selected())->empty());
}

TEST_F(OwnedDurationExpiryTest, TurnReasonsAndCombinedDurationMatchNativeSelectors)
{
 for(auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE, BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::AUTOMATIC_ACTION})
 {
  auto * unit = add(); unit->addNewBonus(effect(BonusDuration::STACK_GETS_TURN | BonusDuration::UNTIL_ATTACK));
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto revision = frame->getForUpdate(unit->unitId())->getTreeVersion();
  frame->expireOwnedTurnBonuses(unit->unitId(), reason); battle()->nextTurn(unit->unitId(), reason); sameFrame(*frame, unit);
  if(reason == BattleUnitTurnReason::UNIT_SPELLCAST) { EXPECT_EQ(frame->getForUpdate(unit->unitId())->getTreeVersion(), revision); }
 }
}

TEST_F(OwnedDurationExpiryTest, CounterAttackKeepsOwnAttackAndNormalAttackExpiresBothPhases)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_ATTACK)); unit->addNewBonus(effect(BonusDuration::UNTIL_OWN_ATTACK));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 frame->expireOwnedAttackBonuses(unit->unitId(), true); unit->removeBonusesRecursive(Bonus::UntilAttack); sameFrame(*frame, unit);
 ASSERT_EQ(unit->getAllBonuses(selected())->size(), 1);
 frame->expireOwnedAttackBonuses(unit->unitId(), false); unit->removeBonusesRecursive(Bonus::UntilAttack); unit->removeBonusesRecursive(Bonus::UntilOwnAttack); sameFrame(*frame, unit);
 EXPECT_TRUE(unit->getAllBonuses(selected())->empty());
}

TEST_F(OwnedDurationExpiryTest, DamageExpiryRequiresNegativeDeltaIncludingDeadTargets)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 int64_t damage = unit->getAvailableHealth(); unit->damage(damage); ASSERT_FALSE(unit->alive());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto revision = frame->getForUpdate(unit->unitId())->getTreeVersion();
 frame->expireOwnedDamageBonuses(unit->unitId(), 0); frame->expireOwnedDamageBonuses(unit->unitId(), 1); sameFrame(*frame, unit);
 EXPECT_EQ(frame->getForUpdate(unit->unitId())->getTreeVersion(), revision);
 frame->expireOwnedDamageBonuses(unit->unitId(), -1); unit->removeBonusesRecursive(Bonus::UntilBeingAttacked); sameFrame(*frame, unit);
}

TEST_F(OwnedDurationExpiryTest, FirstRoundMissingAndNoopPreserveRevisionAndSourceIsolation)
{
 auto * unit = add(); unit->addNewBonus(effect()); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto child = frame->forkOwnedFixture();
 auto revision = child->getForUpdate(unit->unitId())->getTreeVersion();
 child->expireOwnedRoundBonuses(true); child->expireOwnedAttackBonuses(999999, false); child->expireOwnedDamageBonuses(999999, -1);
 child->expireOwnedTurnBonuses(999999, BattleUnitTurnReason::TURN_QUEUE); child->expireOwnedAttackBonuses(unit->unitId(), false);
 EXPECT_EQ(child->getForUpdate(unit->unitId())->getTreeVersion(), revision);
 child->expireOwnedRoundBonuses(false); EXPECT_EQ(child->getForUpdate(unit->unitId())->getAllBonuses(selected())->front()->turnsRemain, 1);
 EXPECT_EQ(frame->getForUpdate(unit->unitId())->getAllBonuses(selected())->front()->turnsRemain, 2); EXPECT_EQ(unit->getAllBonuses(selected())->front()->turnsRemain, 2);
 unit->reduceBonusDurations(Bonus::NTurns); sameFrame(*child, unit);
}

TEST_F(OwnedDurationExpiryTest, SignedDurationEdgesAndUnsupportedEventRejectAtomically)
{
 auto * unit = add(); auto zero = effect(); unit->addNewBonus(zero); zero->turnsRemain = 0;
 auto negative = effect(BonusDuration::N_TURNS, -2); unit->addNewBonus(negative);
 auto e = BonusEvaluationSnapshot::capture({unit}); auto node = e.graph().roots().at(0); auto session = OwnedBonusGraphSession::capture(e);
 EXPECT_TRUE(session.expireBonuses(node, OwnedBonusExpiry::Round)); unit->reduceBonusDurations(Bonus::NTurns); same(session, {unit});
 unit->addNewBonus(effect()); unit->addNewBonus(effect(BonusDuration::N_TURNS, std::numeric_limits<si16>::min()));
 e = BonusEvaluationSnapshot::capture({unit}); session = OwnedBonusGraphSession::capture(e); node = e.graph().roots().at(0);
 const auto revision = session.revision();
 EXPECT_THROW(session.expireBonuses(node, OwnedBonusExpiry::Round), std::invalid_argument); same(session, {unit}); EXPECT_EQ(session.revision(), revision);
 EXPECT_THROW(session.expireBonuses(node, static_cast<OwnedBonusExpiry>(99)), std::invalid_argument); same(session, {unit});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_THROW(frame->expireOwnedTurnBonuses(unit->unitId(), static_cast<BattleUnitTurnReason>(99)), std::invalid_argument);
 ASSERT_EQ(frame->getRound(), 0);
 auto sibling = frame->forkOwnedFixture();
 frame->nextRound(); battle()->nextRound();
 EXPECT_EQ(frame->getRound(), 1); EXPECT_EQ(frame->getRound(), battle()->getRound());
 sameFrame(*frame, unit); EXPECT_EQ(frame->getForUpdate(unit->unitId())->save(), unit->save());
 EXPECT_EQ(sibling->getRound(), 0);
 const auto frameRevision = frame->getTreeVersion();
 std::vector<JsonNode> states;
 for(auto * native : battle()->getUnitsIf([](const battle::Unit *) { return true; }))
  states.push_back(frame->getForUpdate(native->unitId())->save());
 EXPECT_THROW(frame->nextRound(), std::invalid_argument);
 EXPECT_EQ(frame->getRound(), 1); EXPECT_EQ(frame->getTreeVersion(), frameRevision);
 size_t index = 0;
 for(auto * native : battle()->getUnitsIf([](const battle::Unit *) { return true; }))
 {
  EXPECT_EQ(frame->getForUpdate(native->unitId())->save(), states.at(index++));
  auto a = frame->getForUpdate(native->unitId())->getAllBonuses(Selector::all);
  auto b = native->getAllBonuses(Selector::all); sameLists(*a, *b);
 }
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  EXPECT_EQ(frame->getCastSpells(side), battle()->getCastSpells(side));
  EXPECT_EQ(frame->getEnchanterCounter(side), battle()->getEnchanterCounter(side));
  EXPECT_EQ(frame->getUsedSpells(side), battle()->getUsedSpells(side));
 }
 frame->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
 battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
 EXPECT_EQ(frame->getActiveStackID(), battle()->getActiveStackID());
 EXPECT_EQ(frame->getForUpdate(unit->unitId())->save(), unit->save());
 sameFrame(*frame, unit);
 EXPECT_EQ(sibling->getRound(), 0);
}

TEST_F(OwnedDurationExpiryTest, RoundFailurePreservesCompletedUnitPrefixOnly)
{
 auto * first = add(5); auto * second = add(7); first->addNewBonus(effect()); second->addNewBonus(effect(BonusDuration::N_TURNS, std::numeric_limits<si16>::min()));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_THROW(frame->expireOwnedRoundBonuses(false), std::invalid_argument);
 first->reduceBonusDurations(Bonus::NTurns); sameFrame(*frame, first); sameFrame(*frame, second);
 EXPECT_EQ(frame->getForUpdate(first->unitId())->getAllBonuses(selected())->front()->turnsRemain, 1);
}

TEST_F(OwnedDurationExpiryTest, LifecycleCleanupUsesCapturedValuesAndPreservesEventProvenance)
{
 auto * unit = add();
 auto temporary = effect(BonusDuration::PERMANENT);
 auto persistent = effect(BonusDuration::PERMANENT); persistent->sid = SpellID(SpellID::DISRUPTING_RAY);
 unit->addNewBonus(temporary); unit->addNewBonus(persistent);
 auto e = BonusEvaluationSnapshot::capture({unit}); const auto node = e.graph().roots().at(0);
 OwnedSpellPersistenceFacts facts{{SpellID(SpellID::HASTE), false}, {SpellID(SpellID::DISRUPTING_RAY), true}};
 auto parent = OwnedBonusGraphSession::capture(e, facts); auto child = parent.fork();
 facts.at(SpellID(SpellID::HASTE)) = true;
 const auto event = child.makeEvent({*temporary});
 ASSERT_TRUE(child.removeNonpersistentSpells(node));
 EXPECT_TRUE(child.evaluation().getAllBonuses(node, selected())->empty());
 EXPECT_EQ(child.evaluation().getAllBonuses(node, Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY)))->size(), 1);
 EXPECT_EQ(parent.evaluation().getAllBonuses(node, selected())->size(), 1);
 // Cleanup preserves the fork's certified event domain.
 child.addEventRoot(node, event, 0);
 EXPECT_EQ(child.evaluation().getAllBonuses(node, selected())->size(), 1);
 ASSERT_TRUE(child.removeNonpersistentSpells(node));
 EXPECT_TRUE(child.evaluation().getAllBonuses(node, selected())->empty());
}

TEST_F(OwnedDurationExpiryTest, MissingLifecycleFactsRejectWithoutPublishingEarlierRecursiveRemoval)
{
 auto * parent = add(3); auto * child = add(5); child->attachTo(*parent);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{child, parent};
 parent->addNewBonus(effect(BonusDuration::PERMANENT));
 auto unknown = effect(BonusDuration::PERMANENT); unknown->sid = SpellID(SpellID::DISRUPTING_RAY); child->addNewBonus(unknown);
 auto e = BonusEvaluationSnapshot::capture({parent, child}); const auto node = e.graph().roots().at(0);
 auto empty = OwnedBonusGraphSession::capture(e);
 EXPECT_THROW(empty.removeNonpersistentSpells(node), std::invalid_argument); same(empty, {parent, child});
 auto partial = OwnedBonusGraphSession::capture(e, {{SpellID(SpellID::HASTE), false}});
 const auto revision = partial.revision();
 EXPECT_THROW(partial.removeNonpersistentSpells(node), std::invalid_argument);
 EXPECT_EQ(partial.revision(), revision); same(partial, {parent, child});
}
