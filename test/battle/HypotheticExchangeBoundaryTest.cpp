#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/BattleExchangeVariant.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/json/JsonNode.h"

class HypotheticExchangeBoundaryTest : public BattleTestFixture
{
public:
 testing::StrictMock<EnvironmentMock> environment;
 using Mode = BattleExchangeVariant::AttackMode;
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 std::shared_ptr<HypotheticBattle> project()
 {
  return std::make_shared<HypotheticBattle>(&environment,
   std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER)));
 }
 std::shared_ptr<HypotheticBattle> branch(const std::shared_ptr<HypotheticBattle> & parent)
 { return std::make_shared<HypotheticBattle>(&environment, parent); }
 CStack * add(BattleSide side, int x, int count = 100)
 { return addStack(side, creatureByName("core:woodElf"), BattleHex(x,5), count); }
 static CSelector all() { return [](const Bonus *) { return true; }; }
 static bool hasRetaliationCache(const JsonNode & saved)
 { return saved["state"]["counterAttacks"].Struct().count("totalCache") != 0; }
 static int64_t retaliationCache(const JsonNode & saved)
 { return saved["state"]["counterAttacks"]["totalCache"].Integer(); }

 static std::map<uint32_t, JsonNode> states(const std::shared_ptr<HypotheticBattle> & frame)
 {
  std::map<uint32_t, JsonNode> result;
  for(auto * unit : frame->getUnitsIf([](const battle::Unit *) { return true; }))
   result.emplace(unit->unitId(), unit->acquireState()->save());
  return result;
 }
 static void same(battle::CUnitState & actual, battle::CUnitState & expected)
 {
  EXPECT_EQ(actual.save(), expected.save()) << "actual-json=" << actual.save().toString() << " expected-json=" << expected.save().toString();
  EXPECT_EQ(actual.getMaxHealth(), expected.getMaxHealth());
  EXPECT_EQ(actual.getInitiative(0), expected.getInitiative(0));
  EXPECT_EQ(actual.getAttack(false), expected.getAttack(false));
  EXPECT_EQ(actual.getDefense(false), expected.getDefense(false));
  EXPECT_EQ(actual.shots.total(), expected.shots.total());
  EXPECT_EQ(actual.shots.available(), expected.shots.available());
  EXPECT_EQ(actual.shots.isLimited(), expected.shots.isLimited());
  EXPECT_EQ(actual.shots.canUse(1), expected.shots.canUse(1));
  EXPECT_EQ(actual.counterAttacks.available(), expected.counterAttacks.available());
  EXPECT_EQ(actual.canShoot(), expected.canShoot());
 }
 struct BonusState
 {
  TConstBonusListPtr identities;
  std::vector<Bonus> values;
 };
 static BonusState bonusState(const StackWithBonuses & unit)
 {
  BonusState result; result.identities=unit.getAllBonuses(all());
  for(const auto & bonus : *result.identities) result.values.push_back(*bonus);
  return result;
 }
 static void sameBonuses(const TConstBonusListPtr & actual, const BonusState & expected)
 {
  ASSERT_EQ(actual->size(), expected.values.size());
  for(size_t i=0;i<actual->size();++i)
  {
   const auto & a=*(*actual)[i]; const auto & e=expected.values[i];
   EXPECT_EQ((*actual)[i].get(), (*expected.identities)[i].get());
   EXPECT_EQ(a.type,e.type); EXPECT_EQ(a.subtype,e.subtype); EXPECT_EQ(a.source,e.source); EXPECT_EQ(a.sid,e.sid);
   EXPECT_EQ(a.val,e.val); EXPECT_EQ(a.valType,e.valType); EXPECT_EQ(a.effectRange,e.effectRange);
   EXPECT_EQ(a.duration,e.duration); EXPECT_EQ(a.turnsRemain,e.turnsRemain);
   EXPECT_EQ(a.limiter.get(),e.limiter.get()); EXPECT_EQ(a.updater.get(),e.updater.get());
   EXPECT_EQ(a.propagator.get(),e.propagator.get()); EXPECT_EQ(a.propagationUpdater.get(),e.propagationUpdater.get());
  }
 }
 void scalarDamage(bool clone, bool summoned, bool lethal)
 {
  auto * native = add(BattleSide::ATTACKER, 5, 10);
  native->cloned = clone; native->summoned = summoned;
  native->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_ATTACK,
   BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID()));
  auto parent = project(); auto sibling = branch(parent); auto child = branch(parent);
  const auto parentBefore = states(parent), siblingBefore = states(sibling);
  const auto nativeBefore = native->save(); auto retained = child->getForUpdate(native->unitId());
  const auto version = retained->getTreeVersion(); const auto bonuses = bonusState(*retained);
  retained->getMaxHealth(); retained->getAttack(false); retained->getInitiative(0);
  int64_t actual = lethal ? retained->getAvailableHealth() : retained->getMaxHealth();
  int64_t expected = actual;
  child->damageUnit(native->unitId(), actual);
  EXPECT_EQ(states(parent), parentBefore); EXPECT_EQ(states(sibling), siblingBefore); EXPECT_EQ(native->save(), nativeBefore);
  EXPECT_EQ(child->getForUpdate(native->unitId()).get(), retained.get());
  EXPECT_EQ(retained->getTreeVersion(), version); sameBonuses(retained->getAllBonuses(all()), bonuses);
  native->damage(expected); same(*retained, *native); EXPECT_EQ(actual, expected);
  if(clone) { EXPECT_EQ(actual, 0); EXPECT_FALSE(retained->alive()); EXPECT_TRUE(retained->ghostPending); }
 }
 void strike(Mode mode, bool shooting)
 {
  auto * nativeAttacker = add(BattleSide::ATTACKER, 5);
  auto * nativeDefender = add(BattleSide::DEFENDER, 6);
  auto parent = project(); auto child = branch(parent); auto expected = branch(parent); auto sibling = branch(parent);
  auto a = child->getForUpdate(nativeAttacker->unitId()); auto d = child->getForUpdate(nativeDefender->unitId());
  auto ea = expected->getForUpdate(nativeAttacker->unitId()); auto ed = expected->getForUpdate(nativeDefender->unitId());
  DamageCache cache; cache.buildDamageCache(parent, BattleSide::ATTACKER);
  const auto parentBefore = states(parent), siblingBefore = states(sibling);
  const auto aBefore = nativeAttacker->save(), dBefore = nativeDefender->save();
  const auto av = a->getTreeVersion(), dv = d->getTreeVersion();
  auto ab = bonusState(*a), db = bonusState(*d);
  const std::string cachingStringBlocksRetaliation = "type_BLOCKS_RETALIATION";
  static const auto selectorBlocksRetaliation = Selector::type()(BonusType::BLOCKS_RETALIATION);
  const bool counterAttacksBlocked = ea->hasBonus(selectorBlocksRetaliation, cachingStringBlocksRetaliation);
  ASSERT_FALSE(hasRetaliationCache(ed->save()));
  int64_t damage = cache.getDamage(ea.get(), ed.get(), parent); ASSERT_GT(damage, 0);
  const auto firstValue = AttackPossibility::calculateDamageReduce(ed.get(), damage, cache);
  ed->damage(damage); ea->afterAttack(shooting, false);
  float retaliationValue = 0;
  if(mode == Mode::FIRST_STRIKE && ed->alive() && ed->ableToRetaliate() && !counterAttacksBlocked && !shooting)
  {
   ASSERT_TRUE(ed->alive()); ASSERT_TRUE(ed->ableToRetaliate());
   ASSERT_FALSE(ea->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
   int64_t retaliation = cache.getDamage(ed.get(), ea.get(), parent); ASSERT_GT(retaliation, 0);
   retaliationValue = AttackPossibility::calculateDamageReduce(ea.get(), retaliation, cache);
   ea->damage(retaliation); ed->afterAttack(false, true);
  }
  BattleExchangeVariant exchange;
  const float value = exchange.trackAttack(a,d,shooting,true,cache,child,mode);
  EXPECT_FLOAT_EQ(value, firstValue-retaliationValue);
  // The native condition queries eligibility before rejecting a ranged strike.
  if(mode == Mode::FIRST_STRIKE)
  {
   EXPECT_EQ(retaliationCache(d->save()),1);
   EXPECT_EQ(retaliationCache(ed->save()),1);
  }
  same(*a,*ea); same(*d,*ed);
  EXPECT_EQ(a->getTreeVersion(), av); EXPECT_EQ(d->getTreeVersion(), dv);
  sameBonuses(a->getAllBonuses(all()), ab); sameBonuses(d->getAllBonuses(all()), db);
  EXPECT_EQ(child->getForUpdate(a->unitId()).get(),a.get()); EXPECT_EQ(child->getForUpdate(d->unitId()).get(),d.get());
  EXPECT_EQ(states(parent),parentBefore); EXPECT_EQ(states(sibling),siblingBefore);
  EXPECT_EQ(nativeAttacker->save(),aBefore); EXPECT_EQ(nativeDefender->save(),dBefore);
 }
};

TEST_F(HypotheticExchangeBoundaryTest, NativeNonlethalDamageAndWarmQueriesMatch) { scalarDamage(false,false,false); }
TEST_F(HypotheticExchangeBoundaryTest, NativeLethalNormalDamageMatches) { scalarDamage(false,false,true); }
TEST_F(HypotheticExchangeBoundaryTest, NativeSummonedLethalGhostPendingMatches) { scalarDamage(false,true,true); }
TEST_F(HypotheticExchangeBoundaryTest, NativeCloneDamageRewritesCallerAmount) { scalarDamage(true,false,false); }

TEST_F(HypotheticExchangeBoundaryTest, ZeroCloneDamageDoesNotKillOrChangeCaller)
{
 auto * native=add(BattleSide::ATTACKER,5,10); native->cloned=true;
 auto child=project(); auto unit=child->getForUpdate(native->unitId()); const auto before=unit->save();
 int64_t actual=0, expected=0; child->damageUnit(native->unitId(),actual); native->damage(expected);
 EXPECT_EQ(actual,expected); EXPECT_EQ(unit->save(),before) << "actual-json=" << unit->save().toString() << " expected-json=" << before.toString(); same(*unit,*native); EXPECT_TRUE(unit->alive());
}

TEST_F(HypotheticExchangeBoundaryTest, NativeRangedCounterConsumesBothResources)
{
 auto * native=add(BattleSide::ATTACKER,5); auto parent=project(); auto child=branch(parent); auto sibling=branch(parent);
 auto unit=child->getForUpdate(native->unitId()); const auto parentBefore=states(parent), siblingBefore=states(sibling);
 const auto before=native->save(); const auto shots=unit->shots.available(), counters=unit->counterAttacks.available();
 ASSERT_GT(shots,0); ASSERT_GT(counters,0); const auto version=unit->getTreeVersion(); auto bonuses=bonusState(*unit);
 child->recordUnitAttack(native->unitId(),true,true);
 EXPECT_EQ(unit->shots.available(),shots-1); EXPECT_EQ(unit->counterAttacks.available(),counters-1);
 EXPECT_EQ(states(parent),parentBefore); EXPECT_EQ(states(sibling),siblingBefore); EXPECT_EQ(native->save(),before);
 EXPECT_EQ(child->getForUpdate(native->unitId()).get(),unit.get()); EXPECT_EQ(unit->getTreeVersion(),version);
 sameBonuses(unit->getAllBonuses(all()),bonuses); native->afterAttack(true,true); same(*unit,*native);
}

TEST_F(HypotheticExchangeBoundaryTest, NativeMeleeOwnAttackSpendsNeitherResource)
{
 auto * native=add(BattleSide::ATTACKER,5); auto child=project(); auto unit=child->getForUpdate(native->unitId());
 const auto before=unit->save(); child->recordUnitAttack(native->unitId(),false,false); native->afterAttack(false,false);
 EXPECT_EQ(unit->save(),before) << "actual-json=" << unit->save().toString() << " expected-json=" << before.toString();
 EXPECT_FALSE(hasRetaliationCache(unit->save())); EXPECT_FALSE(hasRetaliationCache(native->save()));
 same(*unit,*native);
 EXPECT_EQ(retaliationCache(unit->save()),1); EXPECT_EQ(retaliationCache(native->save()),1);
 EXPECT_EQ(unit->save(),native->save());
}

TEST_F(HypotheticExchangeBoundaryTest, EstimateWithParentReadContextMutatesNeitherBranch)
{
 auto * nativeAttacker=add(BattleSide::ATTACKER,5); auto * nativeDefender=add(BattleSide::DEFENDER,6);
 auto parent=project(); auto child=branch(parent); auto sibling=branch(parent);
 auto a=child->getForUpdate(nativeAttacker->unitId()), d=child->getForUpdate(nativeDefender->unitId());
 DamageCache cache; cache.buildDamageCache(parent,BattleSide::ATTACKER);
 const auto pb=states(parent), cb=states(child), sb=states(sibling);
 const auto materializedParent=parent->stackStates.size(), materializedChild=child->stackStates.size();
 const auto av=a->getTreeVersion(), dv=d->getTreeVersion(); auto ab=bonusState(*a), db=bonusState(*d);
 const auto damage=cache.getDamage(a.get(),d.get(),parent); ASSERT_GT(damage,0);
 const auto score=AttackPossibility::calculateDamageReduce(d.get(),damage,cache);
 BattleExchangeVariant exchange;
 EXPECT_FLOAT_EQ(exchange.trackAttack(a,d,false,true,cache,parent,Mode::ESTIMATE),score);
 EXPECT_EQ(states(parent),pb); EXPECT_EQ(states(child),cb); EXPECT_EQ(states(sibling),sb);
 EXPECT_EQ(parent->stackStates.size(),materializedParent); EXPECT_EQ(child->stackStates.size(),materializedChild);
 EXPECT_EQ(a->getTreeVersion(),av); EXPECT_EQ(d->getTreeVersion(),dv);
 sameBonuses(a->getAllBonuses(all()),ab); sameBonuses(d->getAllBonuses(all()),db);
}

TEST_F(HypotheticExchangeBoundaryTest, FirstStrikeMeleePreservesRetaliationAndOwner) { strike(Mode::FIRST_STRIKE,false); }
TEST_F(HypotheticExchangeBoundaryTest, FollowupStrikeDoesNotRetaliate) { strike(Mode::FOLLOWUP_STRIKE,false); }
TEST_F(HypotheticExchangeBoundaryTest, FirstStrikeRangedSpendsShotWithoutRetaliation) { strike(Mode::FIRST_STRIKE,true); }

TEST_F(HypotheticExchangeBoundaryTest, AttackPossibilityReplayPreservesOrderedDamageAndResources)
{
 auto * na=add(BattleSide::ATTACKER,5); auto * nd=add(BattleSide::DEFENDER,6); auto * nf=add(BattleSide::ATTACKER,8);
 auto parent=project(); auto child=branch(parent); auto sibling=branch(parent); auto expected=branch(parent);
 auto a=child->getForUpdate(na->unitId()), d=child->getForUpdate(nd->unitId()), f=child->getForUpdate(nf->unitId());
 auto ea=expected->getForUpdate(na->unitId()), ed=expected->getForUpdate(nd->unitId()), ef=expected->getForUpdate(nf->unitId());
 AttackPossibility ap(a->getPosition(),d->getPosition(),BattleAttackInfo(a.get(),d.get(),0,true));
 ap.attackerState=a->acquireState(); auto victim=d->acquireState(); auto friendly=f->acquireState();
 int64_t ad=a->getMaxHealth(), dd=d->getMaxHealth(), fd=f->getMaxHealth();
 ap.attackerState->damage(ad); victim->damage(dd); friendly->damage(fd); victim->afterAttack(false,true);
 ASSERT_FALSE(victim->ableToRetaliate()); ASSERT_TRUE(d->ableToRetaliate());
 ap.affectedUnits={victim,friendly}; ap.attackerDamageReduce=2; ap.defenderDamageReduce=7;
 const auto pb=states(parent), sb=states(sibling); const auto aBefore=na->save(), dBefore=nd->save(), fBefore=nf->save();
 int64_t expectedDefender=ed->getAvailableHealth()-victim->getAvailableHealth(); ed->damage(expectedDefender); ed->afterAttack(true,true);
 int64_t expectedFriendly=ef->getAvailableHealth()-friendly->getAvailableHealth(); ef->damage(expectedFriendly);
 int64_t expectedAttacker=ea->getAvailableHealth()-ap.attackerState->getAvailableHealth(); ea->damage(expectedAttacker); ea->afterAttack(true,false);
 DamageCache cache; BattleExchangeVariant exchange;
 EXPECT_FLOAT_EQ(exchange.trackAttack(ap,child,cache),ap.attackValue());
 same(*a,*ea); same(*d,*ed); same(*f,*ef);
 EXPECT_EQ(states(parent),pb); EXPECT_EQ(states(sibling),sb);
 EXPECT_EQ(na->save(),aBefore); EXPECT_EQ(nd->save(),dBefore); EXPECT_EQ(nf->save(),fBefore);
 EXPECT_EQ(child->getForUpdate(a->unitId()).get(),a.get()); EXPECT_EQ(child->getForUpdate(d->unitId()).get(),d.get());
}

TEST_F(HypotheticExchangeBoundaryTest, AttackPossibilityReplayDoesNotCopyUnrelatedScalarsOrHeal)
{
 auto * na=add(BattleSide::ATTACKER,5); auto * nd=add(BattleSide::DEFENDER,6);
 auto parent=project(); auto child=branch(parent); auto a=child->getForUpdate(na->unitId()), d=child->getForUpdate(nd->unitId());
 AttackPossibility ap(a->getPosition(),d->getPosition(),BattleAttackInfo(a.get(),d.get(),0,false));
 ap.attackerState=a->acquireState(); auto healedVictim=d->acquireState();
 ap.attackerState->setPosition(BattleHex(10,5)); healedVictim->setPosition(BattleHex(11,5));
 int64_t damage=d->getMaxHealth(); d->damage(damage); // Incoming victim has more health than the current branch.
 const auto ab=a->save(); const auto defenderBeforeObservation=d->save();
 ASSERT_FALSE(hasRetaliationCache(defenderBeforeObservation));
 // Replay only the old destination/incoming eligibility reads on independent states.
 auto oracle=branch(parent); auto oracleDefender=oracle->getForUpdate(nd->unitId());
 int64_t oracleDamage=oracleDefender->getMaxHealth(); oracleDefender->damage(oracleDamage);
 auto oracleIncoming=healedVictim->acquireState();
 EXPECT_EQ(oracleDefender->save(),defenderBeforeObservation);
 const bool recordsRetaliation=oracleDefender->ableToRetaliate() && !oracleIncoming->ableToRetaliate();
 ASSERT_FALSE(recordsRetaliation);
 const auto db=oracleDefender->save();
 EXPECT_EQ(retaliationCache(db),1);
 EXPECT_EQ(d->save(),defenderBeforeObservation);
 const auto pb=states(parent); ap.affectedUnits={healedVictim};
 DamageCache cache; BattleExchangeVariant exchange; exchange.trackAttack(ap,child,cache);
 EXPECT_EQ(a->save(),ab); EXPECT_EQ(d->save(),db) << "actual-json=" << d->save().toString() << " expected-json=" << db.toString(); EXPECT_EQ(states(parent),pb);
 EXPECT_EQ(d->save()["state"]["health"],defenderBeforeObservation["state"]["health"]);
 EXPECT_EQ(d->save()["state"]["position"],defenderBeforeObservation["state"]["position"]);
 EXPECT_EQ(retaliationCache(d->save()),1);
}
