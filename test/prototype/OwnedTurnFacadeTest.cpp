#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/networkPacks/PacksForClientBattle.h"
#include "lib/gameState/GameStatePackVisitor.h"
#include "lib/GameLibrary.h"

class OwnedTurnFacadeTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5)
 { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static std::shared_ptr<Bonus> effect()
 { return std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN | BonusDuration::UNTIL_ATTACK, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, SpellID(SpellID::HASTE)); }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 static std::vector<std::string> values(const IBonusBearer & unit)
 {
  std::vector<std::string> result;
  const auto bonuses = unit.getAllBonuses(Selector::all);
  for(const auto & b : *bonuses) result.push_back(b->toJsonNode().toString());
  return result;
 }
};

TEST_F(OwnedTurnFacadeTest, AllReasonsMatchNativeDirectAndPacketWithRetainedReferences)
{
 for(auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE, BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::AUTOMATIC_ACTION})
 {
  auto * unit = add(); unit->addNewBonus(effect());
  unit->setTurnStatus(battle::UnitTurnStatus::ACTED); unit->setCastSpellThisTurn(true);
  auto parent = HypotheticBattle::captureOwnedFixture(*battle());
  auto direct = parent->forkOwnedFixture(), packetFrame = parent->forkOwnedFixture();
  auto retained = direct->getForUpdate(unit->unitId()); auto detached = retained->acquireState();
  const auto before = unit->save(), detachedBefore = detached->save();
  const auto historical = retained->getAllBonuses(selected()); ASSERT_EQ(historical->size(), 1);
  const auto epoch = retained->getTreeVersion(); const auto nativeEpoch = unit->getTreeVersion();
  const auto parentActive = parent->getActiveStackID();
  BattleSetActiveStack packet; packet.battleID = battle()->getBattleID(); packet.stack = unit->unitId(); packet.reason = reason;
  direct->nextTurn(unit->unitId(), reason); packetFrame->applyOwnedStatePacket(packet);
  battle()->nextTurn(unit->unitId(), reason);
  EXPECT_EQ(retained.get(), direct->getForUpdate(unit->unitId()).get());
  EXPECT_EQ(retained->save(), unit->save()); EXPECT_EQ(packetFrame->getForUpdate(unit->unitId())->save(), unit->save());
  EXPECT_EQ(values(*retained), values(*unit)); EXPECT_EQ(values(*packetFrame->getForUpdate(unit->unitId())), values(*unit));
  EXPECT_EQ(direct->getActiveStackID(), battle()->getActiveStackID()); EXPECT_EQ(packetFrame->getActiveStackID(), battle()->getActiveStackID());
  EXPECT_EQ(retained->hadMorale, reason == BattleUnitTurnReason::MORALE);
  EXPECT_EQ(retained->castSpellThisTurn(), reason != BattleUnitTurnReason::MORALE);
  EXPECT_EQ(retained->getTurnStatus(), reason == BattleUnitTurnReason::MORALE ? battle::UnitTurnStatus::READY : battle::UnitTurnStatus::ACTED);
  EXPECT_EQ(retained->hasBonus(selected()), reason == BattleUnitTurnReason::UNIT_SPELLCAST);
  EXPECT_EQ(retained->getTreeVersion() == epoch, unit->getTreeVersion() == nativeEpoch);
  if(reason == BattleUnitTurnReason::UNIT_SPELLCAST)
  {
   EXPECT_EQ(retained->getTreeVersion(), epoch);
  }
  EXPECT_EQ(historical->size(), 1); EXPECT_EQ(historical->front()->duration, BonusDuration::STACK_GETS_TURN | BonusDuration::UNTIL_ATTACK);
  EXPECT_EQ(detached->save(), detachedBefore); EXPECT_EQ(parent->getForUpdate(unit->unitId())->save(), before);
  EXPECT_TRUE(parent->getForUpdate(unit->unitId())->hasBonus(selected())); EXPECT_EQ(parent->getActiveStackID(), parentActive);
 }
}

TEST_F(OwnedTurnFacadeTest, MoralePreservesWaitingDefendedReadyAndRepeatedTurnMatchesNative)
{
 for(auto status : {battle::UnitTurnStatus::READY, battle::UnitTurnStatus::WAITING, battle::UnitTurnStatus::DEFENDED, battle::UnitTurnStatus::ACTED})
 {
  auto * unit = add(); unit->setTurnStatus(status); unit->setCastSpellThisTurn(true);
  auto frame = HypotheticBattle::captureOwnedFixture(*battle());
  for(int repeat = 0; repeat < 2; ++repeat)
  {
   frame->nextTurn(unit->unitId(), BattleUnitTurnReason::MORALE); battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::MORALE);
   auto owned = frame->getForUpdate(unit->unitId());
   EXPECT_EQ(owned->save(), unit->save()); EXPECT_TRUE(owned->hadMorale); EXPECT_FALSE(owned->castSpellThisTurn());
   EXPECT_EQ(owned->getTurnStatus(), status == battle::UnitTurnStatus::ACTED ? battle::UnitTurnStatus::READY : status);
  }
 }
}

TEST_F(OwnedTurnFacadeTest, InvalidReasonMissingDeadAndForeignPacketRejectAtomically)
{
 auto * unit = add(); auto * dead = add(7); int64_t damage = dead->getAvailableHealth(); dead->damage(damage);
 unit->addNewBonus(effect());
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(unit->unitId()); const auto original = retained->save();
 const auto bonuses = values(*retained); const auto epoch = retained->getTreeVersion(); const auto active = frame->getActiveStackID();
 EXPECT_THROW(frame->nextTurn(unit->unitId(), static_cast<BattleUnitTurnReason>(99)), std::invalid_argument);
 for(auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::MORALE})
  for(auto id : {uint32_t(999999), dead->unitId()}) EXPECT_THROW(frame->nextTurn(id, reason), std::invalid_argument);
 BattleSetActiveStack packet; packet.battleID = BattleID::NONE; packet.stack = unit->unitId(); packet.reason = BattleUnitTurnReason::MORALE;
 EXPECT_THROW(frame->applyOwnedStatePacket(packet), std::invalid_argument);
 EXPECT_EQ(retained->save(), original); EXPECT_EQ(values(*retained), bonuses); EXPECT_EQ(retained->getTreeVersion(), epoch);
 EXPECT_EQ(frame->getActiveStackID(), active); EXPECT_EQ(frame->getForUpdate(unit->unitId()).get(), retained.get());
 EXPECT_EQ(sibling->getForUpdate(unit->unitId())->save(), original); EXPECT_EQ(unit->save(), original);
 EXPECT_EQ(frame->getForUpdate(dead->unitId())->save(), dead->save());
}

TEST_F(OwnedTurnFacadeTest, DirectTurnAfterNativeDestructionKeepsForkAndDetachedLifetime)
{
 auto * unit = add(); unit->addNewBonus(effect()); unit->setTurnStatus(battle::UnitTurnStatus::ACTED); unit->setCastSpellThisTurn(true);
 const auto id = unit->unitId(); auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto retained = frame->getForUpdate(id); auto detached = retained->acquireState(); const auto original = detached->save();
 battle()->nextTurn(id, BattleUnitTurnReason::MORALE); const auto expected = unit->save(); const auto expectedValues = values(*unit);
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 {
  struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
  frame->nextTurn(id, BattleUnitTurnReason::MORALE);
  EXPECT_FALSE(retained->hasBonus(selected())); EXPECT_TRUE(sibling->getForUpdate(id)->hasBonus(selected()));
 }
 EXPECT_EQ(retained->save(), expected); EXPECT_EQ(values(*retained), expectedValues);
 EXPECT_EQ(detached->save(), original); EXPECT_EQ(sibling->getForUpdate(id)->save(), original);
 EXPECT_EQ(frame->getForUpdate(id).get(), retained.get()); frame.reset();
 EXPECT_EQ(retained->save(), expected); EXPECT_EQ(detached->save(), original);
}
