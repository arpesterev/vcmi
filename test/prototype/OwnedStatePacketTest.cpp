#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/OwnedStackEffect.h"
#include "lib/gameState/GameStatePackVisitor.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/json/JsonBonus.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/GameLibrary.h"
#include "lib/bonuses/BonusGraphSnapshot.h"
#include "lib/serializer/JsonSerializer.h"

class OwnedStatePacketTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static std::shared_ptr<Bonus> effect(int duration, int value = 7)
 { return std::make_shared<Bonus>(duration, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, SpellID(SpellID::HASTE)); }
 void same(HypotheticBattle & frame, CStack * native)
 {
  auto unit = frame.getForUpdate(native->unitId());
  EXPECT_EQ(unit->save(), native->save());
  auto a = unit->getAllBonuses(Selector::all), b = native->getAllBonuses(Selector::all);
  ASSERT_EQ(a->size(), b->size());
  std::multiset<std::string> ownedValues, nativeValues;
  for(const auto & value : *a) ownedValues.insert(value->toJsonNode().toString());
  for(const auto & value : *b) nativeValues.insert(value->toJsonNode().toString());
  EXPECT_EQ(ownedValues, nativeValues) << "FULL BONUS MULTISET unit=" << native->unitId();
  for(size_t i = 0; i < a->size(); ++i)
   EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode())
    << "BONUS INDEX unit=" << native->unitId() << " index=" << i
    << "\nOWNED JSON\n" << (*a)[i]->toJsonNode().toString()
    << "\nNATIVE JSON\n" << (*b)[i]->toJsonNode().toString();
 }
 void applyNative(CPackForClient & packet)
 { GameStatePackVisitor visitor(*server.gameState); packet.visit(visitor); }
};

TEST_F(OwnedStatePacketTest, AllTurnReasonsMatchActualNativePacketAndForkIsolation)
{
 for(auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE, BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::AUTOMATIC_ACTION})
 {
  auto * unit = add(); unit->addNewBonus(effect(BonusDuration::STACK_GETS_TURN));
  unit->setTurnStatus(battle::UnitTurnStatus::ACTED); unit->setCastSpellThisTurn(true);
  auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto child = parent->forkOwnedFixture();
  auto original = unit->save(); auto active = parent->getActiveStackID();
  BattleSetActiveStack packet; packet.battleID = battle()->getBattleID(); packet.stack = unit->unitId(); packet.reason = reason;
  child->applyOwnedStatePacket(packet); applyNative(packet); same(*child, unit);
  EXPECT_EQ(child->getActiveStackID(), battle()->getActiveStackID());
  EXPECT_EQ(parent->getForUpdate(unit->unitId())->save(), original); EXPECT_EQ(parent->getActiveStackID(), active);
 }
}

TEST_F(OwnedStatePacketTest, MovementUsesNativeVisitorAndMissingDeadTargetsAreNoops)
{
 auto * unit = add(); auto * dead = add(7); int64_t loss = dead->getAvailableHealth(); dead->damage(loss);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 BattleStackMoved packet; packet.battleID = battle()->getBattleID(); packet.stack = unit->unitId(); packet.tilesToMove.insert(BattleHex(9, 5));
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 auto revision = frame->getTreeVersion();
 for(auto id : {unit->unitId(), dead->unitId(), uint32_t(999999)})
 { packet.stack = id; frame->applyOwnedStatePacket(packet); applyNative(packet); }
 EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, unit); same(*frame, dead);
}

TEST_F(OwnedStatePacketTest, UnsupportedPacketsAndInvalidDomainsLeaveStateUntouched)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::STACK_GETS_TURN));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto revision = frame->getTreeVersion(); const auto active = frame->getActiveStackID();
 BattleAttack attack; BattleNextRound round; BattleUnitsChanged changes; StacksInjured injured; SetStackEffect effects;
 for(CPackForClient * packet : {static_cast<CPackForClient *>(&attack), static_cast<CPackForClient *>(&round), static_cast<CPackForClient *>(&changes), static_cast<CPackForClient *>(&injured), static_cast<CPackForClient *>(&effects)})
 { EXPECT_THROW(frame->applyOwnedStatePacket(*packet), std::invalid_argument); same(*frame, unit); }
 BattleSetActiveStack turn; turn.battleID = battle()->getBattleID(); turn.stack = unit->unitId(); turn.reason = static_cast<BattleUnitTurnReason>(99);
 EXPECT_THROW(frame->applyOwnedStatePacket(turn), std::invalid_argument);
 turn.reason = BattleUnitTurnReason::MORALE; turn.battleID = BattleID::NONE;
 EXPECT_THROW(frame->applyOwnedStatePacket(turn), std::invalid_argument);
 turn.battleID = battle()->getBattleID(); turn.stack = 999999;
 EXPECT_THROW(frame->applyOwnedStatePacket(turn), std::invalid_argument);
 BattleStackMoved move; move.battleID = battle()->getBattleID(); move.stack = unit->unitId();
 EXPECT_THROW(frame->applyOwnedStatePacket(move), std::invalid_argument);
 EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(frame->getActiveStackID(), active); same(*frame, unit);
}

TEST_F(OwnedStatePacketTest, IndirectPhaseExpiresBeforeLoadsRegardlessOfDamageAndRepeatedTargets)
{
 auto * a = add(); auto * b = add(7); auto shared = effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE);
 a->addNewBonus(shared); b->addNewBonus(shared); a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 frame->expireOwnedIndirectDamageBonuses({a->unitId(), b->unitId(), a->unitId()});
 // The game-state visitor performs this complete loop before loading any new state.
 for(auto * unit : {a, b, a}) unit->removeBonusesRecursive(Bonus::UntilTakingIndirectDamage);
 same(*frame, a); same(*frame, b);
 EXPECT_TRUE(sibling->getForUpdate(a->unitId())->hasBonus(Bonus::UntilTakingIndirectDamage));
 const auto revision = frame->getTreeVersion(); frame->expireOwnedIndirectDamageBonuses({999999, a->unitId()}); EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedStatePacketTest, SequenceCheckpointUsesFilteredQueriesAndNativeValueRemovalOrder)
{
 auto * a = add(); auto * b = add(7);
 auto selected = effect(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE); a->addNewBonus(selected); b->addNewBonus(selected);
 auto hidden = effect(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE, 11); auto limiter = std::make_shared<CCreatureTypeLimiter>(); limiter->setCreature(creatureByName("core:greenDragon")); hidden->limiter = limiter; a->addNewBonus(hidden);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto cleanup = frame->captureOwnedScalarSequenceCleanup({a->unitId(), b->unitId(), a->unitId()});
 SetStackEffect native; native.battleID = battle()->getBattleID();
 for(auto * unit : {a, b, a}) { std::vector<Bonus> values; const auto bonusList = unit->getAllBonuses(Bonus::untilAfterAttackSequence); for(auto & value : *bonusList) values.push_back(*value); native.toRemove.emplace_back(unit->unitId(), values); }
 // Distinct late effects survive; value-identical late effects match native removal.
 auto late = effect(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE, 12);
 frame->addUnitBonus(a->unitId(), frame->makeOwnedBonusEvent({*late, *selected})); a->addNewBonus(late); a->addNewBonus(std::make_shared<Bonus>(*selected));
 frame->applyOwnedStackEffect(cleanup); applyNative(native); same(*frame, a); same(*frame, b);
 EXPECT_EQ(sibling->getForUpdate(a->unitId())->getAllBonuses(Bonus::untilAfterAttackSequence)->size(), 1);
}

TEST_F(OwnedStatePacketTest, VisibleSequenceComponentsRejectWithoutRemovingEarlierRecipients)
{
 auto * a = add(); auto * b = add(7); a->addNewBonus(effect(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE));
 auto component = effect(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE); auto limiter = std::make_shared<CCreatureTypeLimiter>(); limiter->setCreature(creatureByName("core:woodElf")); component->limiter = limiter; b->addNewBonus(component);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto revision = frame->getTreeVersion();
 EXPECT_THROW(frame->captureOwnedScalarSequenceCleanup({a->unitId(), b->unitId()}), std::invalid_argument);
 EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, a); same(*frame, b);
}

TEST_F(OwnedStatePacketTest, DeathResurrectionCloneAndMixedUnitPacketsMatchNative)
{
 auto * original = add(); auto * clone = add(7); original->cloneID = clone->unitId(); clone->cloned = true;
 original->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto beforeOriginal = original->save(); auto beforeClone = clone->save();
 auto dying = original->acquireState(); int64_t loss = dying->getAvailableHealth(); dying->damage(loss);
 UnitChanges death(original->unitId(), BattleChanges::EOperation::UPDATE); death.data = dying->save(); death.healthDelta = -loss;
 UnitChanges resurrection(clone->unitId(), BattleChanges::EOperation::UPDATE); resurrection.data = clone->save(); resurrection.healthDelta = 1;
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks = {death, resurrection, UnitChanges(clone->unitId(), BattleChanges::EOperation::REMOVE)};
 auto sibling = frame->forkOwnedFixture(); auto retained = frame->getForUpdate(clone->unitId());
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, original); same(*frame, clone);
 EXPECT_EQ(frame->getForUpdate(clone->unitId()).get(), retained.get()); EXPECT_TRUE(retained->ghost);
 EXPECT_EQ(sibling->getForUpdate(original->unitId())->save(), beforeOriginal);
 EXPECT_EQ(sibling->getForUpdate(clone->unitId())->save(), beforeClone);
}

TEST_F(OwnedStatePacketTest, OrderedLivingUpdatesMatchNativeAndPreserveRetainedHandles)
{
 auto * a = add(); auto * b = add(7); a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED)); b->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto scaled = effect(BonusDuration::PERMANENT, 2); scaled->updater = std::make_shared<TimesStackSizeUpdater>(); a->addNewBonus(scaled);
 // Warm the native cache so the comparator covers negative-delta invalidation.
 a->getAllBonuses(Selector::all);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retainedA = frame->getForUpdate(a->unitId()); auto retainedB = frame->getForUpdate(b->unitId());
 auto oldA = a->save(); auto oldB = b->save();
 auto aState = a->acquireState(); auto bState = b->acquireState();
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID();
 const auto append = [&](const auto & state, int64_t loss)
 {
  const auto id = state->unitId(); state->damage(loss);
  UnitChanges change(id, BattleChanges::EOperation::UPDATE); change.healthDelta = -loss; change.data = state->save(); packet.changedStacks.push_back(change);
 };
 append(aState, a->getMaxHealth()); append(bState, 1); append(aState, 1);
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, a); same(*frame, b);
 EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retainedA.get()); EXPECT_EQ(frame->getForUpdate(b->unitId()).get(), retainedB.get());
 EXPECT_EQ(retainedA->save(), a->save()); EXPECT_EQ(retainedB->save(), b->save());
 EXPECT_EQ(sibling->getForUpdate(a->unitId())->save(), oldA); EXPECT_EQ(sibling->getForUpdate(b->unitId())->save(), oldB);
}

TEST_F(OwnedStatePacketTest, ZeroDeltaTurnAndAmmoStatePreservesDamageExpiry)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(unit->unitId());
 auto state = unit->acquireState(); state->setTurnStatus(battle::UnitTurnStatus::WAITING); state->setCastSpellThisTurn(true); state->shots.use(1);
 UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = state->save();
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks.push_back(change);
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 EXPECT_TRUE(retained->hasBonus(Bonus::UntilBeingAttacked)); EXPECT_EQ(retained->save(), unit->save());
 const auto revision = frame->getTreeVersion(); frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit); EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedStatePacketTest, CompleteLivingAttackOrdersAttackerVictimsAndFinalExpiry)
{
 for(bool counter : {false, true})
 {
  auto * attacker = add(); auto * victim = add(7);
  attacker->addNewBonus(effect(BonusDuration::UNTIL_ATTACK)); attacker->addNewBonus(effect(BonusDuration::UNTIL_OWN_ATTACK, 8)); attacker->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED, 9)); victim->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
  auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(attacker->unitId());
  BattleAttack packet; packet.battleID = battle()->getBattleID(); packet.attackerChanges.battleID = packet.battleID; packet.stackAttacking = attacker->unitId(); packet.flags = counter ? BattleAttack::COUNTER : 0;
  auto aState = attacker->acquireState(); aState->setTurnStatus(battle::UnitTurnStatus::ACTED);
  UnitChanges changed(attacker->unitId(), BattleChanges::EOperation::UPDATE); changed.data = aState->save(); packet.attackerChanges.changedStacks.push_back(changed);
  auto vState = victim->acquireState();
  for(auto state : {vState, aState, vState})
  {
   int64_t loss = 1; state->damage(loss); BattleStackAttacked entry; entry.stackAttacked = state->unitId(); entry.newState.id = state->unitId(); entry.newState.healthDelta = -loss; entry.newState.data = state->save(); packet.bsa.push_back(entry);
  }
  frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, attacker); same(*frame, victim);
  EXPECT_EQ(frame->getForUpdate(attacker->unitId()).get(), retained.get());
  EXPECT_FALSE(retained->hasBonus(Bonus::UntilAttack)); EXPECT_EQ(retained->hasBonus(Bonus::UntilOwnAttack), counter); EXPECT_FALSE(retained->hasBonus(Bonus::UntilBeingAttacked));
 }
}

TEST_F(OwnedStatePacketTest, InvalidLateLivingUpdateRejectsStateAndExpiryAtomically)
{
 auto * a = add(); auto * b = add(7); a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(a->unitId()); const auto before = retained->save(); const auto revision = frame->getTreeVersion();
 auto state = a->acquireState(); int64_t loss = 1; state->damage(loss);
 UnitChanges first(a->unitId(), BattleChanges::EOperation::UPDATE); first.data = state->save(); first.healthDelta = -1;
 UnitChanges late(b->unitId(), BattleChanges::EOperation::UPDATE); late.data = b->save(); late.data["state"]["unexpected"] = JsonNode(true);
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks = {first, late};
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, a); same(*frame, b);
 // A corrected lethal final item commits both staged updates.
 auto dead = b->acquireState(); loss = dead->getAvailableHealth(); dead->damage(loss); packet.changedStacks.back().data = dead->save(); packet.changedStacks.back().healthDelta = -loss;
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, a); same(*frame, b);
 EXPECT_FALSE(frame->getForUpdate(b->unitId())->alive());
 EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retained.get());
}

TEST_F(OwnedStatePacketTest, PositiveHealthChangesMatchNativeAndMalformedCanonicalPayloadReject)
{
 auto * unit = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto before = unit->save(); const auto revision = frame->getTreeVersion();
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = before; change.healthDelta = 1; packet.changedStacks.push_back(change);
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 packet.changedStacks[0].healthDelta = -1; packet.changedStacks[0].data = before; packet.changedStacks[0].data["state"]["health"]["fullUnits"] = JsonNode(static_cast<int64_t>(INT32_MAX));
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 packet.changedStacks[0].data = before; packet.changedStacks[0].data["state"]["position"] = JsonNode(4.5);
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(frame->getForUpdate(unit->unitId())->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, unit);
}

TEST_F(OwnedStatePacketTest, InvalidLateAttackVictimDoesNotPublishAttackerChangesOrExpiry)
{
 auto * attacker = add(); auto * victim = add(7); attacker->addNewBonus(effect(BonusDuration::UNTIL_ATTACK));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto revision = frame->getTreeVersion();
 BattleAttack packet; packet.battleID = battle()->getBattleID(); packet.attackerChanges.battleID = packet.battleID; packet.stackAttacking = attacker->unitId();
 auto state = attacker->acquireState(); state->setTurnStatus(battle::UnitTurnStatus::ACTED); UnitChanges change(attacker->unitId(), BattleChanges::EOperation::UPDATE); change.data = state->save(); packet.attackerChanges.changedStacks.push_back(change);
 BattleStackAttacked entry; entry.stackAttacked = victim->unitId(); entry.newState.id = victim->unitId(); entry.newState.data = victim->save(); entry.newState.data["state"]["cloned"] = JsonNode(true); packet.bsa.push_back(entry);
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument); same(*frame, attacker); same(*frame, victim); EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedStatePacketTest, RawTopHealthAndZeroDeltaLoadsMatchNativeWithoutClamping)
{
 auto * unit = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto retained = frame->getForUpdate(unit->unitId());
 for(int64_t top : {static_cast<int64_t>(unit->getMaxHealth()) + 1, static_cast<int64_t>(INT32_MAX)})
 {
  UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = unit->save();
  change.data["state"]["health"]["fullUnits"] = JsonNode(int64_t(1));
  change.data["state"]["health"]["firstHPleft"] = JsonNode(top);
  BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks = {change};
  frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
  EXPECT_EQ(retained->health.getFirstHPleft(), top);
 }
 const auto before = retained->save(); const auto revision = frame->getTreeVersion();
 for(int64_t invalid : {int64_t(-1), int64_t(INT32_MAX) + 1})
 {
  UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = before;
  change.data["state"]["health"]["firstHPleft"] = JsonNode(invalid);
  BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks = {change};
  EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
  EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision);
 }
}

TEST_F(OwnedStatePacketTest, CandidateCountDependentMaximumHealthMatchesNative)
{
 auto * unit = add(); auto scaled = effect(BonusDuration::PERMANENT, 1); scaled->type = BonusType::STACK_HEALTH; scaled->updater = std::make_shared<TimesStackSizeUpdater>(); unit->addNewBonus(scaled); unit->nodeHasChanged();
 int64_t healing = INT32_MAX; unit->health.heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); frame->getTreeVersion(); const auto before = unit->save();
 auto damaged = unit->acquireState(); int64_t loss = unit->getMaxHealth(); damaged->damage(loss);
 UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.healthDelta = -loss; change.data = damaged->save();
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID(); packet.changedStacks.push_back(change);
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 EXPECT_GT(frame->getForUpdate(unit->unitId())->health.getFirstHPleft(), frame->getForUpdate(unit->unitId())->getMaxHealth());
}

TEST_F(OwnedStatePacketTest, CandidateExpiryMaximumChangesMatchOtherRecipients)
{
 auto * source = add(); auto * recipient = add(7);
 // Recursive removal can change another unit's maximum without loading its state.
 recipient->attachTo(*source);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{recipient, source};
 auto bonus = effect(BonusDuration::UNTIL_ATTACK, 2); bonus->type = BonusType::STACK_HEALTH; recipient->addNewBonus(bonus);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); frame->getTreeVersion();
 BattleAttack packet; packet.battleID = battle()->getBattleID(); packet.attackerChanges.battleID = packet.battleID; packet.stackAttacking = source->unitId();
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, source); same(*frame, recipient);
}

TEST_F(OwnedStatePacketTest, ConstantMaximumPartialHealthLossMatchesNativeAcrossRepeatedUpdates)
{
 auto * unit = add(); int64_t initialLoss = 2; unit->damage(initialLoss);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto state = unit->acquireState();
 BattleUnitsChanged packet; packet.battleID = battle()->getBattleID();
 for(int64_t requested : {int64_t(1), static_cast<int64_t>(unit->getMaxHealth()) - 2})
 {
  const auto previousCount = state->health.getCount(); const auto previousHP = state->health.getFirstHPleft();
  auto loss = requested; state->damage(loss);
  if(requested != 1) { EXPECT_LT(state->health.getCount(), previousCount); EXPECT_GT(state->health.getFirstHPleft(), previousHP); }
  UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.healthDelta = -loss; change.data = state->save(); packet.changedStacks.push_back(change);
 }
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
}

TEST_F(OwnedStatePacketTest, IndirectPacketMatchesNativeRepeatedDamageAndRetainedHandles)
{
 auto * a = add(); auto * b = add(7);
 auto indirect = effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE);
 a->addNewBonus(indirect); b->addNewBonus(indirect);
 a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED, 8));
 b->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED, 9));
 a->addNewBonus(effect(BonusDuration::UNTIL_ATTACK, 10));
 auto scaled = effect(BonusDuration::PERMANENT, 2); scaled->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, 3); a->addNewBonus(scaled);
 a->getAllBonuses(Selector::all);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retainedA = frame->getForUpdate(a->unitId()); auto retainedB = frame->getForUpdate(b->unitId());
 const auto beforeA = a->save(), beforeB = b->save();
 StacksInjured packet; packet.battleID = battle()->getBattleID();
 auto stateA = a->acquireState(), stateB = b->acquireState();
 for(const auto & state : {stateA, stateB, stateA})
 {
  int64_t loss = state->unitId() == a->unitId() ? a->getMaxHealth() : 1; state->damage(loss);
  BattleStackAttacked entry; entry.stackAttacked = state->unitId(); entry.newState.id = state->unitId();
  entry.newState.healthDelta = -loss; entry.newState.data = state->save();
  entry.flags = BattleStackAttacked::SPELL_EFFECT | BattleStackAttacked::SECONDARY; packet.stacks.push_back(entry);
 }
 frame->applyOwnedStatePacket(packet);
 EXPECT_EQ(a->save(), beforeA); EXPECT_EQ(b->save(), beforeB);
 EXPECT_TRUE(a->hasBonus(Bonus::UntilTakingIndirectDamage)); EXPECT_TRUE(b->hasBonus(Bonus::UntilTakingIndirectDamage));
 applyNative(packet); same(*frame, a); same(*frame, b);
 EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retainedA.get()); EXPECT_EQ(frame->getForUpdate(b->unitId()).get(), retainedB.get());
 EXPECT_EQ(retainedA->save(), a->save()); EXPECT_EQ(retainedB->save(), b->save());
 EXPECT_FALSE(retainedA->hasBonus(Bonus::UntilTakingIndirectDamage)); EXPECT_FALSE(retainedB->hasBonus(Bonus::UntilBeingAttacked));
 EXPECT_TRUE(retainedA->hasBonus(Bonus::UntilAttack));
 EXPECT_EQ(sibling->getForUpdate(a->unitId())->save(), beforeA); EXPECT_EQ(sibling->getForUpdate(b->unitId())->save(), beforeB);
 EXPECT_TRUE(sibling->getForUpdate(a->unitId())->hasBonus(Bonus::UntilTakingIndirectDamage));
}

TEST_F(OwnedStatePacketTest, IndirectZeroDeltaStillExpiresAndEmptyPacketIsNoop)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE));
 unit->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED, 8)); unit->addNewBonus(effect(BonusDuration::UNTIL_OWN_ATTACK, 9));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(unit->unitId());
 StacksInjured packet; packet.battleID = battle()->getBattleID();
 const auto revision = frame->getTreeVersion(); frame->applyOwnedStatePacket(packet); applyNative(packet);
 EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, unit);
 BattleStackAttacked entry; entry.stackAttacked = unit->unitId(); entry.newState.id = unit->unitId(); entry.newState.data = unit->save(); packet.stacks = {entry, entry};
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 EXPECT_FALSE(retained->hasBonus(Bonus::UntilTakingIndirectDamage)); EXPECT_TRUE(retained->hasBonus(Bonus::UntilBeingAttacked)); EXPECT_TRUE(retained->hasBonus(Bonus::UntilOwnAttack));
 const auto afterExpiry = frame->getTreeVersion(); frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
 EXPECT_EQ(frame->getTreeVersion(), afterExpiry); EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
}

TEST_F(OwnedStatePacketTest, InvalidLateIndirectPayloadRollsBackStateAndExpiry)
{
 auto * a = add(); auto * b = add(7); a->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE)); b->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE));
 a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED, 8));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(a->unitId()); const auto before = a->save(); const auto revision = frame->getTreeVersion();
 auto state = a->acquireState(); int64_t loss = 1; state->damage(loss);
 StacksInjured packet; packet.battleID = battle()->getBattleID();
 BattleStackAttacked first; first.stackAttacked = a->unitId(); first.newState.id = a->unitId(); first.newState.healthDelta = -loss; first.newState.data = state->save();
 BattleStackAttacked late; late.stackAttacked = b->unitId(); late.newState.id = b->unitId(); late.newState.data = b->save(); packet.stacks = {first, late};
 for(int invalid = 0; invalid < 8; ++invalid)
 {
  packet.stacks.back() = late;
  auto & entry = packet.stacks.back();
  switch(invalid)
  {
   case 0: entry.newState.data["state"]["unexpected"] = JsonNode(true); break;
   case 1: entry.newState.id = a->unitId(); break;
   case 2: entry.flags = 4; break;
   case 3: entry.newState.data["state"]["position"] = JsonNode(int64_t(GameConstants::BFIELD_SIZE)); break;
   case 4: entry.newState.operation = BattleChanges::EOperation::REMOVE; break;
   case 5: entry.stackAttacked = entry.newState.id = 999999; break;
   case 6: entry.newState.data["state"]["position"] = JsonNode(4.5); break;
   case 7: entry.newState.data["state"]["health"]["firstHPleft"] = JsonNode(int64_t(INT32_MAX) + 1); entry.newState.healthDelta = -1; break;
  }
  EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
  EXPECT_EQ(retained->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, a); same(*frame, b);
 }
 // A failed packet must leave the frame usable for a corrected replay.
 packet.stacks.back() = late; frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, a); same(*frame, b);
 EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retained.get());
}

TEST_F(OwnedStatePacketTest, IndirectPreExpiryMaximumChangesMatchAcrossGraphRecipients)
{
 auto * source = add(); auto * recipient = add(7); recipient->attachTo(*source);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{recipient, source};
 auto health = effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE, 2); health->type = BonusType::STACK_HEALTH; recipient->addNewBonus(health);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); frame->getTreeVersion();
 StacksInjured packet; packet.battleID = battle()->getBattleID(); BattleStackAttacked entry;
 entry.stackAttacked = source->unitId(); entry.newState.id = source->unitId(); entry.newState.data = source->save(); packet.stacks.push_back(entry);
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, source); same(*frame, recipient);
 EXPECT_FALSE(frame->getForUpdate(recipient->unitId())->hasBonus(Bonus::UntilTakingIndirectDamage));
}

TEST_F(OwnedStatePacketTest, IndirectDeathAndCloneTransitionsMatchNativePrepass)
{
 auto * a = add(); auto * b = add(7); b->cloned = true; a->cloneID = b->unitId();
 a->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE)); b->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 StacksInjured packet; packet.battleID = battle()->getBattleID();
 // Both payloads are produced before the visitor runs its all-recipient expiry prepass.
 for(auto * native : {a, b})
 {
  auto state = native->acquireState(); int64_t loss = state->getAvailableHealth(); state->damage(loss);
  BattleStackAttacked entry; entry.stackAttacked = native->unitId(); entry.newState.id = native->unitId();
  entry.newState.data = state->save(); entry.newState.healthDelta = -loss;
  entry.flags = native->cloned ? BattleStackAttacked::CLONE_KILLED : BattleStackAttacked::KILLED;
  packet.stacks.push_back(entry);
 }
 frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, a); same(*frame, b);
 EXPECT_FALSE(frame->getForUpdate(a->unitId())->alive()); EXPECT_FALSE(frame->getForUpdate(b->unitId())->alive());
 EXPECT_FALSE(frame->getForUpdate(b->unitId())->hasBonus(Bonus::UntilTakingIndirectDamage));
 EXPECT_TRUE(sibling->getForUpdate(a->unitId())->alive()); EXPECT_TRUE(sibling->getForUpdate(b->unitId())->alive());
}

TEST_F(OwnedStatePacketTest, ForeignAndDerivedIndirectPacketsRejectWithoutExpiry)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto revision = frame->getTreeVersion();
 StacksInjured packet; packet.battleID = BattleID::NONE; BattleStackAttacked entry; entry.stackAttacked = unit->unitId(); entry.newState.id = unit->unitId(); entry.newState.data = unit->save(); packet.stacks.push_back(entry);
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 struct DerivedInjured : StacksInjured {} derived; derived.battleID = battle()->getBattleID(); derived.stacks = packet.stacks;
 EXPECT_THROW(frame->applyOwnedStatePacket(derived), std::invalid_argument);
 EXPECT_EQ(frame->getTreeVersion(), revision); same(*frame, unit);
 packet.battleID = battle()->getBattleID(); frame->applyOwnedStatePacket(packet); applyNative(packet); same(*frame, unit);
}

class OwnedDeathPacketTest : public OwnedStatePacketTest
{
public:
 UnitChanges dying(CStack * unit)
 {
  auto state = unit->acquireState(); int64_t loss = state->getAvailableHealth(); state->damage(loss);
  UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = state->save(); change.healthDelta = -loss; return change;
 }
 BattleUnitsChanged update(std::vector<UnitChanges> changes)
 { BattleUnitsChanged p; p.battleID = battle()->getBattleID(); p.changedStacks = std::move(changes); return p; }
 void parity(HypotheticBattle & frame)
 {
  for(const auto & native : battle()->stacks)
  {
   same(frame, native.get());
   auto unit = frame.getForUpdate(native->unitId());
   EXPECT_EQ(unit->getMaxHealth(), native->getMaxHealth());
   EXPECT_EQ(unit->getAvailableHealth(), native->getAvailableHealth());
   EXPECT_EQ(unit->getCasterOwner(), native->getCasterOwner());
   EXPECT_EQ(frame.unitHasAmmoCart(unit.get()), native->unitHasAmmoCart(native.get()));
   EXPECT_EQ(unit->shots.isLimited(), native->shots.isLimited());
   EXPECT_EQ(unit->shots.total(), native->shots.total());
   EXPECT_EQ(unit->shots.available(), native->shots.available());
   EXPECT_EQ(unit->casts.total(), native->casts.total());
   EXPECT_EQ(unit->casts.available(), native->casts.available());
  }
 }
 static std::shared_ptr<Bonus> persistent(int value = 13)
 {
  return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
   BonusSource::SPELL_EFFECT, value, SpellID(SpellID::DISRUPTING_RAY));
 }
};

TEST_F(OwnedDeathPacketTest, DeathRemovesRecursiveExportsButKeepsInheritedPersistentAndGraphAttachment)
{
 auto * unit = add(); auto * descendant = add(7); descendant->attachTo(*unit);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{descendant, unit};
 auto inherited = effect(BonusDuration::PERMANENT, 31); attackerSideHero->addNewBonus(inherited);
 unit->addNewBonus(effect(BonusDuration::PERMANENT)); unit->addNewBonus(persistent());
 descendant->addNewBonus(effect(BonusDuration::PERMANENT, 9)); descendant->addNewBonus(persistent(17));
 auto propagated = effect(BonusDuration::PERMANENT, 23);
 propagated->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO); unit->addNewBonus(propagated);
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto frame = parent->forkOwnedFixture(); auto sibling = parent->forkOwnedFixture();
 auto retained = frame->getForUpdate(unit->unitId()); auto detached = retained->acquireState(); const auto before = unit->save();
 auto packet = update({dying(unit)}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_FALSE(retained->alive()); EXPECT_FALSE(retained->ghost); EXPECT_FALSE(retained->ghostPending);
 EXPECT_TRUE(retained->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY))));
 EXPECT_TRUE(retained->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE))));
 EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
 for(auto untouched : {parent, sibling}) EXPECT_EQ(untouched->getForUpdate(unit->unitId())->save(), before);
 EXPECT_EQ(detached->save(), before);
 // A later export on the dead node still reaches its attached descendant.
 auto late = effect(BonusDuration::PERMANENT, 41);
 frame->addUnitBonus(unit->unitId(), frame->makeOwnedBonusEvent({*late})); unit->addNewBonus(late);
 parity(*frame);
 UnitChanges corpse(unit->unitId(), BattleChanges::EOperation::UPDATE); corpse.data = unit->save();
 packet = update({corpse}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_TRUE(retained->hasBonus([](const Bonus * b) { return b->val == 41; }));
}

TEST_F(OwnedDeathPacketTest, LoadedCloneLinkGhostsOnlyOneLevelAndPreservesLinkedSpellsAndReverseLinks)
{
 auto * original = add(); auto * oldLink = add(7); auto * loadedLink = add(9); auto * nextLink = add(11); auto * otherOwner = add(13);
 oldLink->cloned = loadedLink->cloned = nextLink->cloned = true;
 original->cloneID = oldLink->unitId(); loadedLink->cloneID = nextLink->unitId(); otherOwner->cloneID = loadedLink->unitId();
 loadedLink->addNewBonus(effect(BonusDuration::PERMANENT, 29));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(loadedLink->unitId());
 auto death = dying(original); death.data["state"]["cloneID"] = JsonNode(int64_t(loadedLink->unitId()));
 auto packet = update({death}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_TRUE(frame->getForUpdate(oldLink->unitId())->alive()); EXPECT_TRUE(frame->getForUpdate(nextLink->unitId())->alive());
 EXPECT_FALSE(retained->alive()); EXPECT_TRUE(retained->ghostPending); EXPECT_FALSE(retained->ghost);
 EXPECT_EQ(retained->cloneID, nextLink->unitId()); EXPECT_EQ(frame->getForUpdate(otherOwner->unitId())->cloneID, loadedLink->unitId());
 EXPECT_TRUE(retained->hasBonus([](const Bonus * b) { return b->val == 29; })); EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1);
 EXPECT_EQ(frame->getForUpdate(loadedLink->unitId()).get(), retained.get()); EXPECT_TRUE(sibling->getForUpdate(loadedLink->unitId())->alive());
}

TEST_F(OwnedDeathPacketTest, MissingAndAlreadyDeadLoadedCloneLinksClearWithoutFurtherCleanup)
{
 for(bool missing : {false, true})
 {
  auto * original = add(); auto * linked = add(7); linked->cloned = true;
  int64_t loss = 1; linked->damage(loss); linked->addNewBonus(effect(BonusDuration::PERMANENT, 33));
  original->cloneID = missing ? 999999 : linked->unitId();
  auto frame = HypotheticBattle::captureOwnedFixture(*battle());
  auto packet = update({dying(original)}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
  EXPECT_EQ(frame->getForUpdate(original->unitId())->cloneID, -1);
  EXPECT_TRUE(frame->getForUpdate(linked->unitId())->hasBonus([](const Bonus * b) { return b->val == 33; }));
 }
}

TEST_F(OwnedDeathPacketTest, ZeroDeltaCloneDeathAndAlreadyDeadUpdateClearReverseLinksButRetainLateSpells)
{
 auto * owner = add(); auto * clone = add(7); auto * secondOwner = add(9); clone->cloned = true;
 owner->cloneID = secondOwner->cloneID = clone->unitId(); clone->addNewBonus(effect(BonusDuration::PERMANENT)); clone->addNewBonus(persistent());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(clone->unitId());
 auto death = dying(clone); ASSERT_EQ(death.healthDelta, 0);
 auto packet = update({death}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_FALSE(retained->alive()); EXPECT_TRUE(retained->ghostPending); EXPECT_EQ(frame->getForUpdate(owner->unitId())->cloneID, -1);
 EXPECT_EQ(frame->getForUpdate(secondOwner->unitId())->cloneID, -1);
 auto late = effect(BonusDuration::PERMANENT, 37); frame->addUnitBonus(clone->unitId(), frame->makeOwnedBonusEvent({*late})); clone->addNewBonus(late);
 UnitChanges link(owner->unitId(), BattleChanges::EOperation::UPDATE); link.data = owner->save(); link.data["state"]["cloneID"] = JsonNode(int64_t(clone->unitId()));
 UnitChanges corpse(clone->unitId(), BattleChanges::EOperation::UPDATE); corpse.data = clone->save();
 packet = update({link, corpse}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_EQ(frame->getForUpdate(owner->unitId())->cloneID, -1); EXPECT_TRUE(retained->hasBonus([](const Bonus * b) { return b->val == 37; }));
 EXPECT_EQ(frame->getForUpdate(clone->unitId()).get(), retained.get());
}

TEST_F(OwnedDeathPacketTest, LethalAttackCounterOrdersExpiryAndPreservesPersistentSpells)
{
 for(bool counter : {false, true})
 {
  auto * attacker = add(); auto * victim = add(7);
  attacker->addNewBonus(effect(BonusDuration::UNTIL_ATTACK)); attacker->addNewBonus(effect(BonusDuration::UNTIL_OWN_ATTACK, 8));
  victim->addNewBonus(effect(BonusDuration::PERMANENT, 11)); victim->addNewBonus(persistent());
  auto frame = HypotheticBattle::captureOwnedFixture(*battle());
  BattleAttack packet; packet.battleID = battle()->getBattleID(); packet.attackerChanges.battleID = packet.battleID;
  packet.stackAttacking = attacker->unitId(); packet.flags = counter ? BattleAttack::COUNTER : 0;
  BattleStackAttacked entry; entry.stackAttacked = victim->unitId(); entry.newState = dying(victim); entry.flags = BattleStackAttacked::KILLED; packet.bsa = {entry};
  frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
  EXPECT_FALSE(frame->getForUpdate(victim->unitId())->alive());
  EXPECT_FALSE(frame->getForUpdate(attacker->unitId())->hasBonus(Bonus::UntilAttack));
  EXPECT_EQ(frame->getForUpdate(attacker->unitId())->hasBonus(Bonus::UntilOwnAttack), counter);
 }
}

TEST_F(OwnedDeathPacketTest, LateUnknownSpellCleanupRollsBackEarlierDeathCloneAndPublicHandles)
{
 auto * a = add(); auto * clone = add(7); auto * b = add(9); clone->cloned = true; a->cloneID = clone->unitId();
 a->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto unknown = effect(BonusDuration::PERMANENT); unknown->sid = SpellID(999999);
 frame->addUnitBonus(b->unitId(), frame->makeOwnedBonusEvent({*unknown}));
 auto retainedA = frame->getForUpdate(a->unitId()); auto retainedB = frame->getForUpdate(b->unitId()); auto retainedClone = frame->getForUpdate(clone->unitId());
 const auto beforeA = retainedA->save(), beforeB = retainedB->save(), beforeClone = retainedClone->save(); const auto revision = frame->getTreeVersion();
 const auto bonuses = retainedB->getAllBonuses(Selector::all)->size();
 auto packet = update({dying(a), dying(b)});
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(retainedA->save(), beforeA); EXPECT_EQ(retainedB->save(), beforeB); EXPECT_EQ(retainedClone->save(), beforeClone);
 EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(retainedB->getAllBonuses(Selector::all)->size(), bonuses);
 EXPECT_TRUE(retainedA->hasBonus(Bonus::UntilBeingAttacked)); EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retainedA.get());
 EXPECT_EQ(frame->getForUpdate(clone->unitId()).get(), retainedClone.get());
 EXPECT_EQ(sibling->getForUpdate(a->unitId())->save(), beforeA);
 auto removal = frame->ownedEventFromUnitExports(b->unitId(), Selector::source(BonusSource::SPELL_EFFECT, SpellID(999999)));
 frame->removeUnitBonus(b->unitId(), removal);
 frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
}

TEST_F(OwnedDeathPacketTest, DeathReplayAfterNativeDestructionPreservesForksAndDetachedStates)
{
 auto * unit = add(); auto * clone = add(7); clone->cloned = true; unit->cloneID = clone->unitId();
 unit->addNewBonus(effect(BonusDuration::PERMANENT)); unit->addNewBonus(persistent()); clone->addNewBonus(effect(BonusDuration::PERMANENT, 43));
 const auto id = unit->unitId(), cloneId = clone->unitId();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto packet = update({dying(unit)});
 auto retained = frame->getForUpdate(id); auto detached = retained->acquireState(); const auto before = retained->save();
 applyNative(packet); const auto expected = unit->save(), expectedClone = clone->save();
 std::multiset<std::string> expectedBonuses;
 {
  const auto bonusList = unit->getAllBonuses(Selector::all);
  for(const auto & b : *bonusList) expectedBonuses.insert(b->toJsonNode().toString());
 }
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 decltype(frame) sibling;
 {
  // Keep shared suite definitions alive, but deny replay access to the live registry.
  struct UnavailableLibrary
  {
   GameLibrary * saved = LIBRARY;
   UnavailableLibrary() { LIBRARY = nullptr; }
   ~UnavailableLibrary() { LIBRARY = saved; }
  } unavailable;
  sibling = frame->forkOwnedFixture(); frame->applyOwnedStatePacket(packet);
  EXPECT_EQ(retained->save(), expected); EXPECT_EQ(frame->getForUpdate(cloneId)->save(), expectedClone);
  EXPECT_FALSE(retained->alive()); EXPECT_TRUE(frame->getForUpdate(cloneId)->ghostPending);
  EXPECT_TRUE(retained->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY))));
  EXPECT_FALSE(retained->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE))));
  EXPECT_EQ(sibling->getForUpdate(id)->save(), before); EXPECT_EQ(detached->save(), before);
  sibling->applyOwnedStatePacket(packet); EXPECT_EQ(sibling->getForUpdate(id)->save(), expected);
  EXPECT_EQ(frame->getForUpdate(id).get(), retained.get());
 }
 std::multiset<std::string> actual;
 {
  const auto bonusList = retained->getAllBonuses(Selector::all);
  for(const auto & b : *bonusList) actual.insert(b->toJsonNode().toString());
 }
 EXPECT_EQ(actual, expectedBonuses);
 frame.reset(); EXPECT_EQ(retained->save(), expected);
}

TEST_F(OwnedDeathPacketTest, DeathUpdatesAdjacencyAndCountLimitedQueriesOnCandidateGraph)
{
 auto * source = add(5); auto * recipient = add(6);
 auto adjacent = effect(BonusDuration::PERMANENT, 101); auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = source->creatureId(); adjacent->limiter = limiter; recipient->addNewBonus(adjacent);
 auto scaled = persistent(1); scaled->updater = std::make_shared<TimesStackSizeUpdater>(); source->addNewBonus(scaled);
 const auto selected = [](const Bonus * b) { return b->val == 101; };
 ASSERT_TRUE(recipient->hasBonus(selected));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 ASSERT_TRUE(frame->getForUpdate(recipient->unitId())->hasBonus(selected));
 auto packet = update({dying(source)}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_TRUE(recipient->hasBonus(selected)); EXPECT_TRUE(frame->getForUpdate(recipient->unitId())->hasBonus(selected)); EXPECT_TRUE(sibling->getForUpdate(recipient->unitId())->hasBonus(selected));
 auto selector = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY));
 auto values = frame->getForUpdate(source->unitId())->getAllBonuses(selector); ASSERT_EQ(values->size(), 1); EXPECT_EQ(values->front()->val, 0);
 UnitChanges refresh(recipient->unitId(), BattleChanges::EOperation::UPDATE); refresh.data = recipient->save(); refresh.healthDelta = -1;
 auto invalidation = update({refresh}); frame->applyOwnedStatePacket(invalidation); applyNative(invalidation); parity(*frame);
 EXPECT_FALSE(recipient->hasBonus(selected)); EXPECT_FALSE(frame->getForUpdate(recipient->unitId())->hasBonus(selected));
 EXPECT_TRUE(sibling->getForUpdate(recipient->unitId())->hasBonus(selected));
}

TEST_F(OwnedDeathPacketTest, InvalidAddRollsBackBeforeSupportedRevivalAndRemoval)
{
 auto * living = add(); auto * corpse = add(7); auto aliveData = corpse->save(); int64_t loss = corpse->getAvailableHealth(); corpse->damage(loss);
 living->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(living->unitId()); const auto revision = frame->getTreeVersion();
 for(int invalid = 2; invalid < 3; ++invalid)
 {
  UnitChanges late(corpse->unitId(), BattleChanges::EOperation::UPDATE); late.data = corpse->save();
  late.data = aliveData; late.healthDelta = 1;
  if(invalid == 2) late.operation = BattleChanges::EOperation::ADD;
  auto packet = update({dying(living), late}); EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
  parity(*frame); EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(frame->getForUpdate(living->unitId()).get(), retained.get());
 }
 UnitChanges revival(corpse->unitId(), BattleChanges::EOperation::UPDATE); revival.data = aliveData; revival.healthDelta = 1;
 auto supported = update({dying(living), revival}); frame->applyOwnedStatePacket(supported); applyNative(supported); parity(*frame);
 EXPECT_TRUE(frame->getForUpdate(corpse->unitId())->alive());
 auto removed = frame->getForUpdate(corpse->unitId());
 auto removal = update({UnitChanges(corpse->unitId(), BattleChanges::EOperation::REMOVE)});
 frame->applyOwnedStatePacket(removal); applyNative(removal); parity(*frame);
 EXPECT_EQ(frame->getForUpdate(corpse->unitId()).get(), removed.get()); EXPECT_TRUE(removed->ghost);
}

TEST_F(OwnedDeathPacketTest, IndirectInformationalFlagsAndRawHealthMatchNativeWhileIdentityChangesReject)
{
 auto * unit = add(); auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 for(auto flag : {BattleStackAttacked::KILLED, BattleStackAttacked::CLONE_KILLED})
 {
  StacksInjured packet; packet.battleID = battle()->getBattleID(); BattleStackAttacked entry;
  entry.stackAttacked = entry.newState.id = unit->unitId(); entry.newState.data = unit->save(); entry.flags = flag; packet.stacks = {entry};
  frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame); EXPECT_TRUE(frame->getForUpdate(unit->unitId())->alive());
 }
 StacksInjured packet; packet.battleID = battle()->getBattleID(); BattleStackAttacked entry;
 entry.stackAttacked = entry.newState.id = unit->unitId(); entry.newState.data = unit->save(); entry.newState.healthDelta = -1;
 entry.newState.data["state"]["health"]["firstHPleft"] = JsonNode(int64_t(unit->health.getFirstHPleft()) + 1); packet.stacks = {entry};
 frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 packet.stacks[0].newState.data = unit->save(); packet.stacks[0].newState.data["state"]["cloned"] = JsonNode(true);
 const auto before = unit->save(); const auto revision = frame->getTreeVersion();
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(frame->getForUpdate(unit->unitId())->save(), before); EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedDeathPacketTest, ZeroDeltaDamagedProducedStateMatchesNativeWithoutDamageExpiry)
{
 auto * unit = add(); unit->addNewBonus(effect(BonusDuration::UNTIL_BEING_ATTACKED));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto state = unit->acquireState(); int64_t loss = 1; state->damage(loss);
 UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = state->save();
 auto packet = update({change}); frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_TRUE(frame->getForUpdate(unit->unitId())->hasBonus(Bonus::UntilBeingAttacked));
}

TEST_F(OwnedDeathPacketTest, DeathHealthCleanupPrecedesLaterRawUpdateAndRetainedOwnerAmmoQueries)
{
 auto * source = add(5); auto * recipient = add(7); recipient->attachTo(*source);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{recipient, source};
 giveArtifact(defenderSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
 const auto baseMaximum = recipient->getMaxHealth();
 for(auto type : {BonusType::STACK_HEALTH, BonusType::HYPNOTIZED, BonusType::CASTS, BonusType::SHOTS})
 {
  auto value = effect(BonusDuration::PERMANENT, type == BonusType::HYPNOTIZED ? 1 : 9);
  value->type = type; recipient->addNewBonus(value);
 }
 recipient->nodeHasChanged();
 int64_t healing = INT32_MAX; recipient->health.heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
 ASSERT_GT(recipient->health.getFirstHPleft(), baseMaximum);
 ASSERT_FALSE(recipient->shots.isLimited());
 const auto boostedMaximum = recipient->getMaxHealth();
 const auto initialOwner = recipient->getCasterOwner();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(recipient->unitId());
 auto produced = recipient->acquireState(); produced->casts.use(1);
 UnitChanges later(recipient->unitId(), BattleChanges::EOperation::UPDATE); later.data = produced->save();
 const auto rawHP = produced->health.getFirstHPleft();
 auto packet = update({dying(source), later});
 frame->applyOwnedStatePacket(packet); applyNative(packet); parity(*frame);
 EXPECT_EQ(frame->getForUpdate(recipient->unitId()).get(), retained.get());
 EXPECT_TRUE(retained->alive()); EXPECT_EQ(retained->save(), later.data);
 EXPECT_EQ(retained->getMaxHealth(), baseMaximum); EXPECT_EQ(retained->health.getFirstHPleft(), rawHP);
 EXPECT_GT(rawHP, retained->getMaxHealth());
 EXPECT_EQ(retained->getAvailableHealth(), recipient->getAvailableHealth());
 EXPECT_NE(retained->getCasterOwner(), initialOwner); EXPECT_TRUE(retained->shots.isLimited());
 EXPECT_EQ(retained->casts.total(), 0); EXPECT_EQ(retained->casts.available(), -1);
 auto untouched = sibling->getForUpdate(recipient->unitId());
 EXPECT_EQ(untouched->getMaxHealth(), boostedMaximum); EXPECT_EQ(untouched->getCasterOwner(), initialOwner);
 EXPECT_FALSE(untouched->shots.isLimited()); EXPECT_EQ(untouched->casts.available(), 9);
}

class OwnedRoundPacketTest : public OwnedStatePacketTest
{
public:
 void SetUp() override
 {
  OwnedStatePacketTest::SetUp();
  while(battle()->getRound() < 0) { auto packet = next(); applyNative(packet); }
 }
 BattleNextRound next() { BattleNextRound p; p.battleID = battle()->getBattleID(); return p; }
 static std::shared_ptr<Bonus> timed(BonusType type, int turns, int value = 3)
 {
  auto b = effect(BonusDuration::N_TURNS, value); b->type = type; b->turnsRemain = turns; return b;
 }
 void sameBattle(HypotheticBattle & frame)
 {
  EXPECT_EQ(frame.getRound(), battle()->getRound());
  EXPECT_EQ(frame.getActiveStackID(), battle()->getActiveStackID());
  for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
  {
   EXPECT_EQ(frame.getCastSpells(side), battle()->getCastSpells(side));
   EXPECT_EQ(frame.getEnchanterCounter(side), battle()->getEnchanterCounter(side));
   EXPECT_EQ(frame.getUsedSpells(side), battle()->getUsedSpells(side));
  }
  for(auto & native : battle()->stacks) same(frame, native.get());
 }
};

TEST_F(OwnedRoundPacketTest, OpeningEffectsAndSideMetadataFollowRepeatedNativeRounds)
{
 ASSERT_EQ(battle()->getRound(), 0);
 auto * a = add(); a->addNewBonus(timed(BonusType::STACKS_SPEED, 1));
 auto * b = add(7); b->addNewBonus(timed(BonusType::STACKS_SPEED, 3));
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount = 2;
 battle()->getSide(BattleSide::ATTACKER).enchanterCounter = 2;
 battle()->getSide(BattleSide::DEFENDER).castSpellsCount = 4;
 battle()->getSide(BattleSide::DEFENDER).enchanterCounter = 0;
 battle()->getSide(BattleSide::ATTACKER).usedSpellsHistory = {SpellID(SpellID::HASTE), SpellID(SpellID::SLOW)};
 battle()->getSide(BattleSide::DEFENDER).usedSpellsHistory = {SpellID(SpellID::HASTE)};
 battle()->activeStack = a->unitId();
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto frame = parent->forkOwnedFixture();
 auto sibling = parent->forkOwnedFixture(); auto retained = frame->getForUpdate(a->unitId());
 const auto initial = retained->save();
 for(int round = 1; round <= 4; ++round)
 {
  auto packet = next(); frame->applyOwnedStatePacket(packet); applyNative(packet); sameBattle(*frame);
  EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retained.get());
  EXPECT_EQ(frame->getRound(), round);
  if(round == 1)
  {
   EXPECT_TRUE(retained->hasBonus(Bonus::NTurns));
  }
  if(round == 2)
  {
   EXPECT_FALSE(retained->hasBonus(Bonus::NTurns));
  }
 }
 for(auto * untouched : {parent.get(), sibling.get()})
 {
  EXPECT_EQ(untouched->getRound(), 0); EXPECT_EQ(untouched->getCastSpells(BattleSide::ATTACKER), 2);
  EXPECT_EQ(untouched->getEnchanterCounter(BattleSide::ATTACKER), 2);
  EXPECT_EQ(untouched->getForUpdate(a->unitId())->save(), initial);
  EXPECT_TRUE(untouched->getForUpdate(a->unitId())->hasBonus(Bonus::NTurns));
 }
 auto capturedLater = HypotheticBattle::captureOwnedFixture(*battle()); auto laterFork = capturedLater->forkOwnedFixture();
 auto packet = next(); laterFork->applyOwnedStatePacket(packet); applyNative(packet); sameBattle(*laterFork);
 EXPECT_EQ(capturedLater->getRound(), 4);
}

TEST_F(OwnedRoundPacketTest, RoundResetsEveryStatusAndExpiredRetaliationUsesCandidateBonuses)
{
 auto opening = next(); applyNative(opening);
 for(auto status : {battle::UnitTurnStatus::WAITING, battle::UnitTurnStatus::ACTED, battle::UnitTurnStatus::DEFENDED})
 {
  auto * unit = add(); unit->setTurnStatus(battle::UnitTurnStatus::WAITING); unit->setTurnStatus(status);
  unit->hadMorale = true; unit->fear = true; unit->drainedMana = true; unit->setCastSpellThisTurn(true);
  unit->addNewBonus(timed(BonusType::ADDITIONAL_RETALIATION, 1, 4));
  ASSERT_GE(unit->counterAttacks.total(), 5); unit->counterAttacks.use(2);
 }
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto packet = next();
 frame->applyOwnedStatePacket(packet); applyNative(packet); sameBattle(*frame);
 for(auto & native : battle()->stacks)
 {
  auto owned = frame->getForUpdate(native->unitId());
  EXPECT_EQ(owned->getTurnStatus(), battle::UnitTurnStatus::READY);
  EXPECT_FALSE(owned->waitedThisRound()); EXPECT_FALSE(owned->castSpellThisTurn());
  EXPECT_FALSE(owned->hadMorale); EXPECT_FALSE(owned->fear); EXPECT_FALSE(owned->drainedMana);
  EXPECT_EQ(owned->counterAttacks.total(), native->counterAttacks.total());
  EXPECT_EQ(owned->counterAttacks.available(), native->counterAttacks.available());
 }
}

TEST_F(OwnedRoundPacketTest, SharedDurationAliasesAndPropagationMatchEveryNativeRoot)
{
 auto opening = next(); applyNative(opening);
 auto * a = add(); auto * b = add(7); auto * c = add(9);
 auto shared = timed(BonusType::STACKS_SPEED, 3); a->addNewBonus(shared); b->addNewBonus(shared);
 auto propagated = timed(BonusType::ADDITIONAL_RETALIATION, 1, 4);
 propagated->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 c->addNewBonus(propagated); attackerSideHero->nodeHasChanged();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 for(int i = 0; i < 3; ++i)
 {
  auto packet = next(); frame->applyOwnedStatePacket(packet); applyNative(packet); sameBattle(*frame);
  for(auto & native : battle()->stacks)
   EXPECT_EQ(frame->getForUpdate(native->unitId())->counterAttacks.total(), native->counterAttacks.total());
 }
 EXPECT_TRUE(sibling->getForUpdate(a->unitId())->hasBonus(Bonus::NTurns));
 EXPECT_EQ(sibling->getRound(), 1);
}

TEST_F(OwnedRoundPacketTest, LateMaximumHealthFailureRollsBackGraphHeaderHandlesAndAllowsCorrectedReplay)
{
 auto opening = next(); applyNative(opening);
 auto * a = add(); auto * b = add(7);
 a->addNewBonus(timed(BonusType::STACKS_SPEED, 1));
 auto health = timed(BonusType::STACK_HEALTH, 1, 2); b->addNewBonus(health);
 a->setTurnStatus(battle::UnitTurnStatus::DEFENDED);
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount = 3;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(a->unitId());
 const auto revision = frame->getTreeVersion(); auto packet = next();
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(frame->getTreeVersion(), revision); EXPECT_EQ(frame->getForUpdate(a->unitId()).get(), retained.get());
 sameBattle(*frame);
 auto selector = Selector::type()(BonusType::STACK_HEALTH).And(Bonus::NTurns);
 auto removal = frame->ownedEventFromUnitExports(b->unitId(), selector);
 frame->removeUnitBonus(b->unitId(), removal); b->removeBonusesRecursive(selector);
 frame->applyOwnedStatePacket(packet); applyNative(packet); sameBattle(*frame);
}

TEST_F(OwnedRoundPacketTest, UnsupportedIdentityCloneDeathAndObstacleLeaveAllCapturedStateUnchanged)
{
 auto * a = add(); add(7); a->addNewBonus(timed(BonusType::STACKS_SPEED, 2));
 auto check = [&](HypotheticBattle & frame, CPackForClient & packet)
 {
  const auto revision = frame.getTreeVersion(); auto retained = frame.getForUpdate(a->unitId());
  EXPECT_THROW(frame.applyOwnedStatePacket(packet), std::invalid_argument);
  EXPECT_EQ(frame.getTreeVersion(), revision); EXPECT_EQ(frame.getForUpdate(a->unitId()).get(), retained.get()); sameBattle(frame);
 };
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto foreign = next(); foreign.battleID = BattleID::NONE; check(*frame, foreign);
 struct DerivedRound : BattleNextRound {}; DerivedRound derived; derived.battleID = battle()->getBattleID(); check(*frame, derived);
 auto packet = next();
 auto obstacle = std::make_shared<SpellCreatedObstacle>(); obstacle->turnsRemaining = 2; battle()->obstacles.push_back(obstacle);
 auto obstructed = HypotheticBattle::captureOwnedFixture(*battle()); check(*obstructed, packet); EXPECT_EQ(obstacle->turnsRemaining, 2); battle()->obstacles.clear();
 // Dead and clone round parity is covered by OwnedRoundLifecycleTest.
}

TEST_F(OwnedRoundPacketTest, RoundAndForkSurviveNativeWorldDestructionWithRetainedUnit)
{
 auto * unit = add(); const auto id = unit->unitId(); unit->addNewBonus(timed(BonusType::STACKS_SPEED, 2));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto packet = next();
 auto retained = frame->getForUpdate(id); auto detached = retained->acquireState();
 std::weak_ptr<CGameState> lifetime = gameState();
 server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 frame->applyOwnedStatePacket(packet); EXPECT_EQ(frame->getRound(), 1);
 auto fork = frame->forkOwnedFixture(); fork->applyOwnedStatePacket(packet); EXPECT_EQ(fork->getRound(), 2);
 EXPECT_EQ(frame->getRound(), 1); EXPECT_EQ(frame->getForUpdate(id).get(), retained.get());
 EXPECT_TRUE(detached->hasBonus(Bonus::NTurns)); EXPECT_TRUE(retained->hasBonus(Bonus::NTurns));
 fork->applyOwnedStatePacket(packet); EXPECT_FALSE(fork->getForUpdate(id)->hasBonus(Bonus::NTurns));
 EXPECT_TRUE(retained->hasBonus(Bonus::NTurns)); frame.reset(); EXPECT_EQ(retained->getCount(), 10);
 EXPECT_EQ(retained->unitType()->getId(), detached->unitType()->getId());
}

TEST_F(OwnedRoundPacketTest, PropagatorSerializationPreservesIndependentValuesAndGraphAliases)
{
 auto * unit = add();
 for(auto type : {BonusNodeType::PLAYER, BonusNodeType::HERO, BonusNodeType::ARMY})
 {
  auto first = effect(BonusDuration::PERMANENT, 17);
  first->propagator = std::make_shared<CPropagatorNodeType>(type);
  auto second = effect(BonusDuration::PERMANENT, 19); second->propagator = first->propagator;
  unit->addNewBonus(first); unit->addNewBonus(second);
  auto graph = BonusGraphSnapshot::capture({unit});
  std::vector<std::shared_ptr<const Bonus>> copies;
  for(auto id : graph.nodes().at(graph.roots().at(0)).exported)
  {
   auto bonus = graph.bonuses().at(id);
   if(bonus->val == 17 || bonus->val == 19) copies.push_back(bonus);
  }
  ASSERT_EQ(copies.size(), 2);
  EXPECT_EQ(copies[0]->propagator, copies[1]->propagator);
  EXPECT_NE(copies[0]->propagator.get(), first->propagator.get());
  EXPECT_EQ(copies[0]->toJsonNode(), first->toJsonNode());
  const auto json = copies[0]->toJsonNode();
  ASSERT_FALSE(json["propagator"].String().empty());
  auto parsed = JsonUtils::parseBonus(json); ASSERT_TRUE(parsed); ASSERT_TRUE(parsed->propagator);
  EXPECT_EQ(parsed->propagator->getPropagatorType(), type);
  EXPECT_EQ(parsed->toJsonNode(), json);
  EXPECT_EQ(parsed->propagator.get(), bonusPropagatorMap.at(json["propagator"].String()).get());
  EXPECT_NE(parsed->propagator.get(), copies[0]->propagator.get());
  unit->removeBonus(first); unit->removeBonus(second);
 }
 auto alias = effect(BonusDuration::PERMANENT);
 alias->propagator = bonusPropagatorMap.at("PLAYER_PROPAGATOR");
 auto aliasJson = alias->toJsonNode(); EXPECT_EQ(aliasJson["propagator"].String(), "PLAYER");
 auto canonical = JsonUtils::parseBonus(aliasJson); ASSERT_TRUE(canonical); ASSERT_TRUE(canonical->propagator);
 EXPECT_EQ(canonical->propagator->getPropagatorType(), BonusNodeType::PLAYER);
 EXPECT_EQ(canonical->toJsonNode(), aliasJson);
 EXPECT_NE(canonical->propagator.get(), alias->propagator.get());
 struct CustomPropagator : CPropagatorNodeType
 { CustomPropagator() : CPropagatorNodeType(BonusNodeType::PLAYER) {} };
 auto custom = effect(BonusDuration::PERMANENT);
 custom->propagator = std::make_shared<CustomPropagator>();
 EXPECT_TRUE(custom->toJsonNode()["propagator"].String().empty());
 unit->addNewBonus(custom);
 EXPECT_THROW(BonusGraphSnapshot::capture({unit}), std::runtime_error);
}

TEST_F(OwnedRoundPacketTest, ActionSpellTacticsSequenceMatchesNativeAndPreservesForkHeaders)
{
 auto * unit = add(); const auto id = unit->unitId();
 battle()->tacticDistance = 6; battle()->tacticsSide = BattleSide::DEFENDER;
 battle()->getSide(BattleSide::ATTACKER).castSpellsCount = 3;
 battle()->getSide(BattleSide::DEFENDER).usedSpellsHistory = {SpellID(SpellID::SLOW)};
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto frame = parent->forkOwnedFixture();
 auto sibling = parent->forkOwnedFixture(); auto retained = frame->getForUpdate(id); const auto original = retained->save();
 EXPECT_EQ(frame->getTacticDist(), 6); EXPECT_EQ(frame->getTacticsSide(), BattleSide::DEFENDER);
 for(auto status : {battle::UnitTurnStatus::WAITING, battle::UnitTurnStatus::READY, battle::UnitTurnStatus::DEFENDED, battle::UnitTurnStatus::ACTED})
 {
  StartAction action; action.battleID = battle()->getBattleID(); action.ba.stackNumber = id;
  action.turnStatus = status; action.castSpellThisTurn = status == battle::UnitTurnStatus::WAITING;
  frame->applyOwnedStatePacket(action); applyNative(action); sameBattle(*frame);
  EXPECT_TRUE(retained->waitedThisRound()); EXPECT_TRUE(retained->castSpellThisTurn());
 }
 for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
 {
  BattleSpellCast spell; spell.battleID = battle()->getBattleID(); spell.side = side;
  spell.spellID = SpellID(SpellID::HASTE); spell.activeCast = false; spell.manaGained = 9;
  frame->applyOwnedStatePacket(spell); applyNative(spell); sameBattle(*frame);
 }
 auto progressed = frame->forkOwnedFixture(); sameBattle(*progressed);
 EXPECT_EQ(progressed->getTacticDist(), 6); EXPECT_EQ(progressed->getTacticsSide(), BattleSide::DEFENDER);
 BattleTacticsEnded end; end.battleID = battle()->getBattleID();
 frame->applyOwnedStatePacket(end); applyNative(end); sameBattle(*frame);
 EXPECT_EQ(frame->getTacticDist(), battle()->getTacticDist()); EXPECT_EQ(frame->getTacticsSide(), battle()->getTacticsSide());
 EXPECT_EQ(progressed->getTacticDist(), 6);
 auto round = next(); frame->applyOwnedStatePacket(round); applyNative(round); sameBattle(*frame);
 EXPECT_EQ(frame->getForUpdate(id).get(), retained.get());
 for(auto untouched : {parent, sibling})
 {
  EXPECT_EQ(untouched->getForUpdate(id)->save(), original); EXPECT_EQ(untouched->getTacticDist(), 6);
  EXPECT_EQ(untouched->getTacticsSide(), BattleSide::DEFENDER);
  EXPECT_EQ(untouched->getCastSpells(BattleSide::ATTACKER), 3);
  EXPECT_EQ(untouched->getUsedSpells(BattleSide::DEFENDER), std::vector<SpellID>{SpellID(SpellID::SLOW)});
 }
}

TEST_F(OwnedRoundPacketTest, ActionNoopsAndSpellNonheroNoneSidesMatchNative)
{
 auto * unit = add(); auto * dead = add(7); int64_t loss = dead->getAvailableHealth(); dead->damage(loss);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); const auto revision = frame->getTreeVersion();
 for(auto id : {unit->unitId(), dead->unitId(), uint32_t(999999)})
 {
  StartAction action; action.battleID = battle()->getBattleID(); action.ba.stackNumber = id;
  frame->applyOwnedStatePacket(action); applyNative(action); sameBattle(*frame);
 }
 StartAction cast; cast.battleID = battle()->getBattleID(); cast.ba.stackNumber = unit->unitId(); cast.castSpellThisTurn = true;
 frame->applyOwnedStatePacket(cast); applyNative(cast); sameBattle(*frame);
 for(bool hero : {false, true})
  for(auto side : {BattleSide::NONE, BattleSide::ATTACKER, BattleSide::DEFENDER})
  {
   BattleSpellCast spell; spell.battleID = battle()->getBattleID(); spell.castByHero = hero; spell.side = side;
   spell.spellID = SpellID(SpellID::HASTE);
   frame->applyOwnedStatePacket(spell); applyNative(spell); sameBattle(*frame);
  }
 // The native visitor never indexes the side of a nonhero cast, including INVALID.
 BattleSpellCast ignored; ignored.battleID = battle()->getBattleID(); ignored.castByHero = false; ignored.side = BattleSide::INVALID;
 frame->applyOwnedStatePacket(ignored); applyNative(ignored); sameBattle(*frame);
 EXPECT_EQ(frame->getTreeVersion(), revision);
}

TEST_F(OwnedRoundPacketTest, MetadataInvalidTargetsSidesTypesAndBattleIdsAreTransactional)
{
 auto * unit = add(); auto * dead = add(7); int64_t loss = dead->getAvailableHealth(); dead->damage(loss);
 battle()->tacticDistance = 4; battle()->tacticsSide = BattleSide::ATTACKER;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(unit->unitId());
 const auto revision = frame->getTreeVersion();
 auto check = [&](CPackForClient & packet)
 {
  EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument); sameBattle(*frame);
  EXPECT_EQ(frame->getTacticDist(), 4); EXPECT_EQ(frame->getTacticsSide(), BattleSide::ATTACKER);
  EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get()); EXPECT_EQ(frame->getTreeVersion(), revision);
 };
 StartAction action; BattleSpellCast spell; BattleTacticsEnded end;
 for(CPackForClient * p : {static_cast<CPackForClient *>(&action), static_cast<CPackForClient *>(&spell), static_cast<CPackForClient *>(&end)}) check(*p);
 action.battleID = spell.battleID = end.battleID = battle()->getBattleID(); action.castSpellThisTurn = true;
 for(auto id : {dead->unitId(), uint32_t(999999)})
 {
  action.ba.stackNumber = id; check(action); EXPECT_THROW(applyNative(action), std::runtime_error);
 }
 action.ba.stackNumber = unit->unitId(); action.turnStatus = static_cast<battle::UnitTurnStatus>(99); check(action);
 spell.side = BattleSide::INVALID; check(spell);
 struct DerivedAction : StartAction {} da; da.battleID = battle()->getBattleID();
 struct DerivedSpell : BattleSpellCast {} ds; ds.battleID = battle()->getBattleID();
 struct DerivedTactics : BattleTacticsEnded {} dt; dt.battleID = battle()->getBattleID();
 check(da); check(ds); check(dt);
 action.turnStatus = battle::UnitTurnStatus::WAITING; frame->applyOwnedStatePacket(action); applyNative(action); sameBattle(*frame);
 spell.side = BattleSide::DEFENDER; frame->applyOwnedStatePacket(spell); applyNative(spell); sameBattle(*frame);
 frame->applyOwnedStatePacket(end); applyNative(end); EXPECT_EQ(frame->getTacticDist(), battle()->getTacticDist());
}

TEST_F(OwnedRoundPacketTest, MetadataCaptureForkAndReplaySurviveNativeWorldDestruction)
{
 auto * unit = add(); const auto id = unit->unitId(); battle()->tacticDistance = 7; battle()->tacticsSide = BattleSide::DEFENDER;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto retained = frame->getForUpdate(id);
 auto detached = retained->acquireState(); const auto original = detached->save();
 StartAction action; action.battleID = battle()->getBattleID(); action.ba.stackNumber = id; action.turnStatus = battle::UnitTurnStatus::WAITING; action.castSpellThisTurn = true;
 BattleSpellCast spell; spell.battleID = action.battleID; spell.side = BattleSide::ATTACKER; spell.spellID = SpellID(SpellID::HASTE);
 BattleTacticsEnded end; end.battleID = action.battleID;
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 auto fork = frame->forkOwnedFixture(); fork->applyOwnedStatePacket(action); fork->applyOwnedStatePacket(spell); fork->applyOwnedStatePacket(end);
 EXPECT_EQ(frame->getTacticDist(), 7); EXPECT_EQ(fork->getTacticDist(), 0); EXPECT_EQ(fork->getTacticsSide(), BattleSide::DEFENDER);
 EXPECT_EQ(fork->getCastSpells(BattleSide::ATTACKER), frame->getCastSpells(BattleSide::ATTACKER) + 1);
 EXPECT_TRUE(fork->getForUpdate(id)->waitedThisRound()); EXPECT_TRUE(fork->getForUpdate(id)->castSpellThisTurn());
 EXPECT_EQ(retained->save(), original); EXPECT_EQ(detached->save(), original);
 EXPECT_THROW(fork->getSideHero(BattleSide::ATTACKER), std::logic_error);
 EXPECT_THROW(fork->getServerCallback(), std::logic_error);
 frame.reset(); EXPECT_EQ(retained->save(), original); EXPECT_EQ(retained->unitType()->getId(), detached->unitType()->getId());
}

TEST_F(OwnedRoundPacketTest, MetadataUnsignedCountWrapAndIgnoredActionFieldsMatchNative)
{
 auto * unit = add(); battle()->getSide(BattleSide::ATTACKER).castSpellsCount = UINT32_MAX;
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 BattleSpellCast spell; spell.battleID = battle()->getBattleID(); spell.side = BattleSide::ATTACKER;
 spell.spellID = SpellID(SpellID::HASTE); spell.casterStack = -99;
 const auto oldHistory = frame->getUsedSpells(BattleSide::ATTACKER);
 frame->applyOwnedStatePacket(spell); applyNative(spell); sameBattle(*frame);
 EXPECT_EQ(frame->getCastSpells(BattleSide::ATTACKER), 0);
 ASSERT_EQ(frame->getUsedSpells(BattleSide::ATTACKER).size(), oldHistory.size() + 1);
 EXPECT_EQ(frame->getUsedSpells(BattleSide::ATTACKER).back(), spell.spellID);
 StartAction action; action.battleID = battle()->getBattleID(); action.ba.stackNumber = uint32_t(-1);
 action.ba.side = BattleSide::INVALID; action.ba.actionType = static_cast<EActionType>(127);
 frame->applyOwnedStatePacket(action); applyNative(action); sameBattle(*frame);
 // StartAction is already-produced bookkeeping; even an unknown action kind is not executed.
 action.ba.stackNumber = unit->unitId(); action.turnStatus = battle::UnitTurnStatus::DEFENDED;
 frame->applyOwnedStatePacket(action); applyNative(action); sameBattle(*frame);
 action.ba.stackNumber = uint32_t(-1);
 EXPECT_THROW(frame->applyOwnedStatePacket(action), std::invalid_argument);
 EXPECT_THROW(applyNative(action), std::runtime_error); sameBattle(*frame);
}

class OwnedCacheDeathPacketTest : public OwnedDeathPacketTest
{
public:
 static CSelector adjacentValue() { return [](const Bonus * value) { return value->val == 101; }; }
 void addAdjacency(CStack * source, CStack * recipient)
 {
  auto value = effect(BonusDuration::PERMANENT, 101);
  auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = source->creatureId();
  value->limiter = limiter; recipient->addNewBonus(value);
 }
 static UnitChanges rawDeath(CStack * unit)
 {
  UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE);
  change.data = unit->save(); change.healthDelta = -1;
  auto deadHealth = unit->health;
  deadHealth.reset();
  JsonNode serializedHealth;
  JsonSerializer serializer(nullptr, serializedHealth);
  serializer.serializeStruct("health", deadHealth);
  change.data["state"]["health"] = serializedHealth["health"];
  return change;
 }
};

TEST_F(OwnedCacheDeathPacketTest, AlreadyStaleNativeCaptureSurvivesDestructionBeforeFirstOwnedQuery)
{
 auto * source = add(5); auto * recipient = add(6); addAdjacency(source, recipient);
 ASSERT_TRUE(recipient->hasBonus(adjacentValue()));
 const auto recipientEpoch = recipient->getTreeVersion();
 auto packet = update({rawDeath(source)}); applyNative(packet);
 ASSERT_EQ(source->save(), packet.changedStacks.front().data);
 ASSERT_FALSE(source->alive()); ASSERT_EQ(recipient->getTreeVersion(), recipientEpoch);
 ASSERT_TRUE(recipient->hasBonus(adjacentValue()));
 const auto id = recipient->unitId(); const auto sourceId = source->unitId();
 std::vector<std::string> expected;
 {
  const auto nativeValues = recipient->getAllBonuses(Selector::all);
  for(const auto & bonus : *nativeValues) expected.push_back(bonus->toJsonNode().toString());
 }
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 std::weak_ptr<CGameState> lifetime = gameState();
 server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 std::vector<TConstBonusListPtr> retained;
 {
  struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
  auto fork = frame->forkOwnedFixture();
  for(const auto & branch : {frame, fork})
  {
   auto unit = branch->getForUpdate(id);
   EXPECT_TRUE(unit->hasBonus(adjacentValue())); EXPECT_FALSE(branch->getForUpdate(sourceId)->alive());
   retained.push_back(unit->getAllBonuses(Selector::all));
  }
 }
 // Native serialization resolves entity names; replay above has no registry.
 for(const auto & values:retained)
 {
  std::vector<std::string> actual;
  for(const auto & bonus : *values) actual.push_back(bonus->toJsonNode().toString());
  EXPECT_EQ(actual, expected);
 }
}

TEST_F(OwnedCacheDeathPacketTest, InvalidRecipientRemainsColdThroughCaptureAndForkUntilAfterDeath)
{
 auto * source = add(5); auto * recipient = add(6); addAdjacency(source, recipient);
 ASSERT_TRUE(recipient->hasBonus(adjacentValue()));
 recipient->nodeHasChanged();
 const auto nativeEpoch = recipient->getTreeVersion();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto fork = frame->forkOwnedFixture();
 EXPECT_EQ(recipient->getTreeVersion(), nativeEpoch);
 const auto frameEpoch = frame->getForUpdate(recipient->unitId())->getTreeVersion();
 auto packet = update({rawDeath(source)});
 frame->applyOwnedStatePacket(packet); fork->applyOwnedStatePacket(packet); applyNative(packet);
 EXPECT_EQ(frame->getForUpdate(recipient->unitId())->getTreeVersion(), frameEpoch);
 EXPECT_FALSE(recipient->hasBonus(adjacentValue()));
 EXPECT_FALSE(frame->getForUpdate(recipient->unitId())->hasBonus(adjacentValue()));
 EXPECT_FALSE(fork->getForUpdate(recipient->unitId())->hasBonus(adjacentValue()));
 EXPECT_EQ(frame->getForUpdate(source->unitId())->getCount(), source->getCount());
}

TEST_F(OwnedCacheDeathPacketTest, CapturedValidEmptyRequestSurvivesNeighborDeathAndSelectorChange)
{
 auto * source = add(5); auto * recipient = add(6); addAdjacency(source, recipient);
 const CSelector reject = [](const Bonus *) { return false; };
 ASSERT_TRUE(recipient->getAllBonuses(reject, "empty-before-death")->empty());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto packet = update({rawDeath(source)}); frame->applyOwnedStatePacket(packet); applyNative(packet);
 auto owned = frame->getForUpdate(recipient->unitId());
 EXPECT_TRUE(recipient->getAllBonuses(adjacentValue(), "empty-before-death")->empty());
 EXPECT_TRUE(owned->getAllBonuses(adjacentValue(), "empty-before-death")->empty());
 EXPECT_EQ(recipient->getAllBonuses(adjacentValue(), "new-after-death")->size(), 1);
 EXPECT_EQ(owned->getAllBonuses(adjacentValue(), "new-after-death")->size(), 1);
}

TEST_F(OwnedCacheDeathPacketTest, UnrelatedInvalidationKeepsStaleAdjacencyAndOwnNegativeDeltaRefreshesWithoutRemoval)
{
 auto * source = add(5); auto * recipient = add(6); auto * unrelated = add(10); addAdjacency(source, recipient);
 ASSERT_TRUE(recipient->hasBonus(adjacentValue()));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(recipient->unitId());
 const auto nativeEpoch = recipient->getTreeVersion(), ownedEpoch = owned->getTreeVersion();
 auto death = update({rawDeath(source)}); frame->applyOwnedStatePacket(death); applyNative(death);
 UnitChanges change(unrelated->unitId(), BattleChanges::EOperation::UPDATE); change.data = unrelated->save(); change.healthDelta = -1;
 auto unrelatedPacket = update({change}); frame->applyOwnedStatePacket(unrelatedPacket); applyNative(unrelatedPacket);
 EXPECT_EQ(recipient->getTreeVersion(), nativeEpoch); EXPECT_EQ(owned->getTreeVersion(), ownedEpoch);
 EXPECT_TRUE(recipient->hasBonus(adjacentValue())); EXPECT_TRUE(owned->hasBonus(adjacentValue()));
 change.id = recipient->unitId(); change.data = recipient->save();
 auto ownPacket = update({change}); frame->applyOwnedStatePacket(ownPacket); applyNative(ownPacket);
 EXPECT_NE(recipient->getTreeVersion(), nativeEpoch); EXPECT_NE(owned->getTreeVersion(), ownedEpoch);
 EXPECT_FALSE(recipient->hasBonus(adjacentValue())); EXPECT_FALSE(owned->hasBonus(adjacentValue()));
 EXPECT_EQ(owned->save(), recipient->save());
}

TEST_F(OwnedCacheDeathPacketTest, ZeroDeltaCountLoadRetainsUpdaterCacheWhileNegativeDeltaRecomputes)
{
 auto * unit = add(); auto value = persistent(1); value->updater = std::make_shared<TimesStackSizeUpdater>(); unit->addNewBonus(value);
 const auto selector = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY));
 ASSERT_EQ(unit->getAllBonuses(selector)->size(), 1);
 ASSERT_EQ(unit->getAllBonuses(selector)->front()->val, 10);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto owned = frame->getForUpdate(unit->unitId());
 const auto nativeEpoch = unit->getTreeVersion(), ownedEpoch = owned->getTreeVersion();
 UnitChanges change(unit->unitId(), BattleChanges::EOperation::UPDATE); change.data = unit->save();
 change.data["state"]["health"]["fullUnits"] = JsonNode(int64_t(8));
 auto zero = update({change}); frame->applyOwnedStatePacket(zero); applyNative(zero);
 EXPECT_EQ(unit->getCount(), 9); EXPECT_EQ(owned->getCount(), 9);
 EXPECT_EQ(unit->getTreeVersion(), nativeEpoch); EXPECT_EQ(owned->getTreeVersion(), ownedEpoch);
 EXPECT_EQ(unit->getAllBonuses(selector)->front()->val, 10); EXPECT_EQ(owned->getAllBonuses(selector)->front()->val, 10);
 change.healthDelta = -1;
 auto negative = update({change}); frame->applyOwnedStatePacket(negative); applyNative(negative);
 EXPECT_NE(unit->getTreeVersion(), nativeEpoch); EXPECT_NE(owned->getTreeVersion(), ownedEpoch);
 EXPECT_EQ(unit->getAllBonuses(selector)->front()->val, 9); EXPECT_EQ(owned->getAllBonuses(selector)->front()->val, 9);
}

TEST_F(OwnedCacheDeathPacketTest, LinkedMakeGhostChangesFactsWithoutInventingProxyOrNodeInvalidation)
{
 auto * source = add(5); auto * clone = add(7); clone->cloned = true; source->cloneID = clone->unitId();
 auto value = persistent(1); value->updater = std::make_shared<TimesStackSizeUpdater>(); clone->addNewBonus(value);
 const auto selector = Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::DISRUPTING_RAY));
 ASSERT_EQ(clone->getAllBonuses(selector)->size(), 1);
 ASSERT_EQ(clone->getAllBonuses(selector)->front()->val, 10);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto ownedClone = frame->getForUpdate(clone->unitId());
 const auto nativeEpoch = clone->getTreeVersion(), ownedEpoch = ownedClone->getTreeVersion();
 auto packet = update({rawDeath(source)}); frame->applyOwnedStatePacket(packet); applyNative(packet);
 EXPECT_EQ(clone->getTreeVersion(), nativeEpoch); EXPECT_EQ(ownedClone->getTreeVersion(), ownedEpoch);
 EXPECT_EQ(clone->getCount(), 0); EXPECT_EQ(ownedClone->getCount(), 0);
 EXPECT_TRUE(clone->ghostPending); EXPECT_TRUE(ownedClone->ghostPending);
 EXPECT_EQ(clone->getAllBonuses(selector)->front()->val, 10); EXPECT_EQ(ownedClone->getAllBonuses(selector)->front()->val, 10);
 EXPECT_EQ(ownedClone->save(), clone->save());
}

TEST_F(OwnedRoundPacketTest, FailedCandidateWarmupDoesNotWarmParentOrColdSibling)
{
 auto opening = next(); applyNative(opening);
 auto * source = add(5); auto * recipient = add(6); auto * late = add(10);
 auto adjacency = effect(BonusDuration::PERMANENT, 101);
 auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = source->creatureId(); adjacency->limiter = limiter;
 recipient->addNewBonus(adjacency); late->addNewBonus(timed(BonusType::STACK_HEALTH, 1, 2));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(recipient->unitId()); const auto epoch = retained->getTreeVersion();
 auto roundPacket = next(); EXPECT_THROW(frame->applyOwnedStatePacket(roundPacket), std::invalid_argument);
 EXPECT_EQ(retained->getTreeVersion(), epoch); EXPECT_EQ(frame->getForUpdate(recipient->unitId()).get(), retained.get());
 // The rejected round validates every maximum on disposable copies before the late health expiry.
 // Killing the neighbor now distinguishes cold parent caches from leaked validation warmth.
 BattleUnitsChanged death; death.battleID = battle()->getBattleID(); death.changedStacks = {OwnedCacheDeathPacketTest::rawDeath(source)};
 frame->applyOwnedStatePacket(death); sibling->applyOwnedStatePacket(death); applyNative(death);
 const CSelector selected = [](const Bonus * value) { return value->val == 101; };
 EXPECT_FALSE(recipient->hasBonus(selected)); EXPECT_FALSE(retained->hasBonus(selected));
 EXPECT_FALSE(sibling->getForUpdate(recipient->unitId())->hasBonus(selected));
 EXPECT_EQ(frame->getRound(), sibling->getRound()); EXPECT_EQ(frame->getRound(), battle()->getRound());
}

TEST_F(OwnedCacheDeathPacketTest, VisitorMovementInvalidatesWarmAdjacencyOnEnteringLeavingAndSameDestination)
{
 auto * source = add(10); auto * recipient = add(6); addAdjacency(source, recipient);
 ASSERT_FALSE(recipient->hasBonus(adjacentValue()));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto retained = frame->getForUpdate(recipient->unitId());
 ASSERT_FALSE(retained->hasBonus(adjacentValue()));
 for(const auto & [destination, adjacent] : std::vector<std::pair<BattleHex, bool>>{{BattleHex(5, 5), true}, {BattleHex(10, 5), false}, {BattleHex(10, 5), false}})
 {
  const auto nativeEpoch = recipient->getTreeVersion(), ownedEpoch = retained->getTreeVersion();
  BattleStackMoved move; move.battleID = battle()->getBattleID(); move.stack = source->unitId(); move.tilesToMove.insert(destination);
  frame->applyOwnedStatePacket(move); applyNative(move);
  EXPECT_EQ(frame->getForUpdate(source->unitId())->position, source->position);
  EXPECT_EQ(source->position, destination);
  EXPECT_NE(recipient->getTreeVersion(), nativeEpoch); EXPECT_NE(retained->getTreeVersion(), ownedEpoch);
  EXPECT_EQ(recipient->hasBonus(adjacentValue()), adjacent); EXPECT_EQ(retained->hasBonus(adjacentValue()), adjacent);
  parity(*frame);
  EXPECT_EQ(frame->getForUpdate(recipient->unitId()).get(), retained.get());
 }
}

TEST_F(OwnedRoundPacketTest, SuccessfulRoundDoesNotWarmColdRecipientBeforeNeighborDeath)
{
 auto * source = add(5); auto * recipient = add(6);
 auto adjacency = effect(BonusDuration::PERMANENT, 101);
 auto limiter = std::make_shared<UnitAdjacentLimiter>(); limiter->targetUnit = source->creatureId(); adjacency->limiter = limiter;
 recipient->addNewBonus(adjacency);
 recipient->nodeHasChanged();
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(recipient->unitId());
 const auto nativeEpoch = recipient->getTreeVersion(), ownedEpoch = retained->getTreeVersion();
 auto roundPacket = next(); frame->applyOwnedStatePacket(roundPacket); sibling->applyOwnedStatePacket(roundPacket); applyNative(roundPacket);
 EXPECT_EQ(recipient->getTreeVersion(), nativeEpoch); EXPECT_EQ(retained->getTreeVersion(), ownedEpoch);
 EXPECT_EQ(frame->getRound(), battle()->getRound());
 BattleUnitsChanged death; death.battleID = battle()->getBattleID(); death.changedStacks = {OwnedCacheDeathPacketTest::rawDeath(source)};
 frame->applyOwnedStatePacket(death); sibling->applyOwnedStatePacket(death); applyNative(death);
 const CSelector selected = [](const Bonus * value) { return value->val == 101; };
 EXPECT_FALSE(recipient->hasBonus(selected)); EXPECT_FALSE(retained->hasBonus(selected));
 EXPECT_FALSE(sibling->getForUpdate(recipient->unitId())->hasBonus(selected));
 EXPECT_EQ(retained->save(), recipient->save());
 EXPECT_EQ(frame->getForUpdate(source->unitId())->save(), source->save());
}

TEST_F(OwnedCacheDeathPacketTest, CanonicalDeathFixturePreservesColdCachesAndRejectsExplicitZeros)
{
 auto * unit = add();
 unit->nodeHasChanged();
 const auto original = unit->save();
 const auto before = BonusGraphSnapshot::capture({unit});
 const auto node = before.roots().front();
 ASSERT_FALSE(before.nodeCaches().at(node).valid);
 auto change = rawDeath(unit);
 ASSERT_TRUE(change.data["state"]["health"].isStruct());
 EXPECT_TRUE(change.data["state"]["health"].Struct().empty());
 const auto after = BonusGraphSnapshot::capture({unit});
 EXPECT_FALSE(after.nodeCaches().at(after.roots().front()).valid);
 EXPECT_EQ(after.nodeCaches().at(after.roots().front()).epoch, before.nodeCaches().at(node).epoch);
 EXPECT_EQ(unit->save(), original);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto retained = frame->getForUpdate(unit->unitId());
 const auto epoch = retained->getTreeVersion();
 auto explicitZeros = change;
 for(const auto * field : {"firstHPleft", "fullUnits", "resurrected"})
  explicitZeros.data["state"]["health"][field] = JsonNode(int64_t(0));
 auto invalidPacket = update({explicitZeros});
 EXPECT_THROW(frame->applyOwnedStatePacket(invalidPacket), std::invalid_argument);
 EXPECT_EQ(retained->save(), original);
 EXPECT_EQ(retained->getTreeVersion(), epoch);
 EXPECT_EQ(unit->save(), original);
 auto packet = update({change});
 frame->applyOwnedStatePacket(packet); applyNative(packet);
 EXPECT_EQ(unit->save(), change.data);
 EXPECT_EQ(retained->save(), change.data);
 EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
 EXPECT_FALSE(unit->alive()); EXPECT_FALSE(retained->alive());
}
