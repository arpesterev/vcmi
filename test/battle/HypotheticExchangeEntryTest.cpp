#include "StdInc.h"
#include <cmath>
#include "server/battles/BattleTestFixture.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/BattleExchangeVariant.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/json/JsonNode.h"

class HypotheticExchangeEntryTest : public BattleTestFixture
{
public:
 testing::StrictMock<EnvironmentMock> environment;
 static bool hasRetaliationCache(const JsonNode & saved)
 { return saved["state"]["counterAttacks"].Struct().count("totalCache") != 0; }
 static int64_t retaliationCache(const JsonNode & saved)
 { return saved["state"]["counterAttacks"]["totalCache"].Integer(); }

 void SetUp() override
 {
  BattleTestFixture::SetUp(); startGame(); startBattle();
  for(const auto * unit : battle()->getUnitsIf([](const battle::Unit *) { return true; }))
   battle()->removeUnit(unit->unitId());
 }
 CStack * add(BattleSide side,int x,int count=100)
 { return addStack(side,creatureByName("core:woodElf"),BattleHex(x,5),count); }
 std::shared_ptr<HypotheticBattle> project()
 {
  return std::make_shared<HypotheticBattle>(&environment,
   std::make_shared<CPlayerBattleCallback>(battle(),battle()->sideToPlayer(BattleSide::ATTACKER)));
 }
 static std::map<uint32_t,JsonNode> states(const std::shared_ptr<HypotheticBattle> & frame)
 {
  std::map<uint32_t,JsonNode> result;
  for(const auto * unit : frame->getUnitsIf([](const battle::Unit *) { return true; }))
   result.emplace(unit->unitId(),unit->acquireState()->save());
  return result;
 }
 void evaluatorEntry(bool attackerFirst)
 {
  auto * nativeAttacker=add(BattleSide::ATTACKER,5);
  auto * nativeDefender=add(BattleSide::DEFENDER,6);
  (attackerFirst ? nativeAttacker : nativeDefender)->addNewBonus(std::make_shared<Bonus>(
   BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,50,BonusSourceID()));
  auto parent=project(); auto sibling=std::make_shared<HypotheticBattle>(&environment,parent);
  auto attacker=parent->getForUpdate(nativeAttacker->unitId()); auto defender=parent->getForUpdate(nativeDefender->unitId());
  ASSERT_FALSE(parent->battleCanShoot(attacker.get())); ASSERT_FALSE(parent->battleCanShoot(defender.get()));
  DamageCache cache; cache.buildDamageCache(parent,BattleSide::ATTACKER);
  ASSERT_GT(cache.getDamage(attacker.get(),defender.get(),parent),0);
  ASSERT_GT(cache.getDamage(defender.get(),attacker.get(),parent),0);
  // Real attack production, not a prebuilt exchange result or test-only facade.
  const BattleAttackInfo attack(attacker.get(),defender.get(),0,false);
  auto ap=AttackPossibility::evaluate(attack,attacker->getPosition(),cache,parent);
  ASSERT_NE(ap.attackerState,nullptr); ASSERT_FALSE(ap.affectedUnits.empty());
  ASSERT_GT(ap.defenderDamageReduce,0);
  PotentialTargets targets; targets.possibleAttacks.push_back(ap);
  auto env=std::shared_ptr<Environment>(&environment,[](Environment *) {});
  BattleExchangeEvaluator evaluator(parent,env,1.0f,1);
  evaluator.updateReachabilityMap(parent);
  const auto reachable=evaluator.getExchangeUnits(ap,0,targets,parent);
  ASSERT_FALSE(reachable.units.empty()); ASSERT_GE(reachable.units.at(0).size(),2);
  ASSERT_EQ(reachable.units.at(0).front()->unitId(),attackerFirst ? attacker->unitId() : defender->unitId());
  const auto parentBefore=states(parent), siblingBefore=states(sibling);
  const auto nativeAttackerBefore=nativeAttacker->save(), nativeDefenderBefore=nativeDefender->save();
  const auto materialized=parent->stackStates.size();
  const auto attackerVersion=attacker->getTreeVersion(), defenderVersion=defender->getTreeVersion();
  const float score=evaluator.evaluateExchange(ap,0,targets,cache,parent);
  EXPECT_TRUE(std::isfinite(score)); EXPECT_NE(score,0.0f); EXPECT_GT(score,EvaluationResult::INEFFECTIVE_SCORE);
  EXPECT_EQ(states(parent),parentBefore); EXPECT_EQ(states(sibling),siblingBefore);
  EXPECT_EQ(parent->stackStates.size(),materialized);
  EXPECT_EQ(nativeAttacker->save(),nativeAttackerBefore); EXPECT_EQ(nativeDefender->save(),nativeDefenderBefore);
  EXPECT_EQ(parent->getForUpdate(attacker->unitId()).get(),attacker.get());
  EXPECT_EQ(parent->getForUpdate(defender->unitId()).get(),defender.get());
  EXPECT_EQ(attacker->getTreeVersion(),attackerVersion); EXPECT_EQ(defender->getTreeVersion(),defenderVersion);
  // A second public entry starts a fresh exchange branch and must replay the same result.
  EXPECT_FLOAT_EQ(evaluator.evaluateExchange(ap,0,targets,cache,parent),score);
  EXPECT_EQ(states(parent),parentBefore); EXPECT_EQ(states(sibling),siblingBefore);
 }
};

TEST_F(HypotheticExchangeEntryTest, PublicEvaluatorAttackerFirstKeepsApReplayInChild)
{ evaluatorEntry(true); }
TEST_F(HypotheticExchangeEntryTest, PublicEvaluatorDefenderFirstKeepsDirectStrikeInChild)
{ evaluatorEntry(false); }

TEST_F(HypotheticExchangeEntryTest, LethalApDamagePrecedesRetaliationEligibility)
{
 auto * na=add(BattleSide::ATTACKER,5); auto * nd=add(BattleSide::DEFENDER,6);
 auto parent=project(); auto child=std::make_shared<HypotheticBattle>(&environment,parent);
 auto sibling=std::make_shared<HypotheticBattle>(&environment,parent);
 auto a=child->getForUpdate(na->unitId()), d=child->getForUpdate(nd->unitId());
 ASSERT_FALSE(hasRetaliationCache(d->save())); ASSERT_FALSE(hasRetaliationCache(nd->save()));
 ASSERT_TRUE(d->ableToRetaliate()); ASSERT_GT(d->shots.available(),0);
 ASSERT_TRUE(nd->ableToRetaliate());
 EXPECT_EQ(retaliationCache(d->save()),1); EXPECT_EQ(retaliationCache(nd->save()),1);
 EXPECT_EQ(d->save(),nd->save()) << "actual-json=" << d->save().toString() << " expected-json=" << nd->save().toString();
 AttackPossibility ap(a->getPosition(),d->getPosition(),BattleAttackInfo(a.get(),d.get(),0,true));
 ap.attackerState=a->acquireState(); auto killed=d->acquireState();
 int64_t lethal=killed->getAvailableHealth(); killed->damage(lethal); killed->afterAttack(false,true);
 ASSERT_FALSE(killed->alive()); ASSERT_FALSE(killed->ableToRetaliate());
 ap.affectedUnits={killed}; ap.defenderDamageReduce=7;
 const auto pb=states(parent), sb=states(sibling);
 const auto naBefore=na->save(), ndBefore=nd->save();
 const auto victimShots=d->shots.available(), victimCounters=d->counterAttacks.available();
 const auto attackerShots=a->shots.available();
 DamageCache cache; BattleExchangeVariant exchange; exchange.trackAttack(ap,child,cache);
 EXPECT_FALSE(d->alive());
 EXPECT_EQ(d->shots.available(),victimShots); EXPECT_EQ(d->counterAttacks.available(),victimCounters);
 EXPECT_EQ(a->shots.available(),attackerShots-1);
 EXPECT_EQ(states(parent),pb); EXPECT_EQ(states(sibling),sb);
 EXPECT_EQ(na->save(),naBefore); EXPECT_EQ(nd->save(),ndBefore);
 // Actual native scalar oracle: after lethal damage the retaliation branch is ineligible.
 int64_t nativeDamage=nd->getAvailableHealth(); nd->damage(nativeDamage);
 ASSERT_FALSE(nd->ableToRetaliate()); na->afterAttack(true,false);
 EXPECT_EQ(retaliationCache(d->save()),1); EXPECT_EQ(retaliationCache(nd->save()),1);
 EXPECT_EQ(d->save(),nd->save()) << "actual-json=" << d->save().toString() << " expected-json=" << nd->save().toString(); EXPECT_EQ(a->save(),na->save());
}

TEST_F(HypotheticExchangeEntryTest, LethalFirstStrikeDoesNotGenerateRetaliation)
{
 auto * na=add(BattleSide::ATTACKER,5); auto * nd=add(BattleSide::DEFENDER,6,1);
 auto parent=project(); auto child=std::make_shared<HypotheticBattle>(&environment,parent);
 auto a=child->getForUpdate(na->unitId()), d=child->getForUpdate(nd->unitId());
 DamageCache cache; cache.buildDamageCache(parent,BattleSide::ATTACKER);
 int64_t nativeDamage=cache.getDamage(a.get(),d.get(),parent); ASSERT_GE(nativeDamage,d->getAvailableHealth());
 ASSERT_FALSE(hasRetaliationCache(d->save())); ASSERT_FALSE(hasRetaliationCache(nd->save()));
 ASSERT_TRUE(d->ableToRetaliate());
 ASSERT_TRUE(nd->ableToRetaliate());
 EXPECT_EQ(retaliationCache(d->save()),1); EXPECT_EQ(retaliationCache(nd->save()),1);
 EXPECT_EQ(d->save(),nd->save()) << "actual-json=" << d->save().toString() << " expected-json=" << nd->save().toString();
 const auto attackerBefore=a->save(); const auto parentBefore=states(parent);
 const auto counters=d->counterAttacks.available();
 BattleExchangeVariant exchange; exchange.trackAttack(a,d,false,true,cache,child,BattleExchangeVariant::AttackMode::FIRST_STRIKE);
 EXPECT_FALSE(d->alive()); EXPECT_EQ(a->save(),attackerBefore); EXPECT_EQ(d->counterAttacks.available(),counters);
 EXPECT_EQ(states(parent),parentBefore);
 nd->damage(nativeDamage); ASSERT_FALSE(nd->ableToRetaliate()); na->afterAttack(false,false);
 EXPECT_EQ(retaliationCache(d->save()),1); EXPECT_EQ(retaliationCache(nd->save()),1);
 EXPECT_EQ(d->save(),nd->save()) << "actual-json=" << d->save().toString() << " expected-json=" << nd->save().toString(); EXPECT_EQ(a->save(),na->save());
}

TEST_F(HypotheticExchangeEntryTest, SequentialBoundaryMatchesLegacyAfterEveryOperation)
{
 auto * na=add(BattleSide::ATTACKER,5); auto * nd=add(BattleSide::DEFENDER,6);
 auto parent=project(); auto child=std::make_shared<HypotheticBattle>(&environment,parent);
 auto old=std::make_shared<HypotheticBattle>(&environment,parent); auto sibling=std::make_shared<HypotheticBattle>(&environment,parent);
 auto a=child->getForUpdate(na->unitId()), d=child->getForUpdate(nd->unitId());
 auto oa=old->getForUpdate(na->unitId()), od=old->getForUpdate(nd->unitId());
 const auto pb=states(parent), sb=states(sibling);
 const auto av=a->getTreeVersion(), dv=d->getTreeVersion();
 int64_t first=d->getMaxHealth(), firstOld=first;
 child->damageUnit(d->unitId(),first); od->damage(firstOld);
 EXPECT_EQ(first,firstOld); EXPECT_EQ(states(child),states(old));
 child->recordUnitAttack(a->unitId(),true,false); oa->afterAttack(true,false);
 EXPECT_EQ(states(child),states(old)); EXPECT_EQ(a->shots.available(),oa->shots.available());
 int64_t counter=a->getMaxHealth(), counterOld=counter;
 child->damageUnit(a->unitId(),counter); oa->damage(counterOld);
 EXPECT_EQ(counter,counterOld); EXPECT_EQ(states(child),states(old));
 child->recordUnitAttack(d->unitId(),false,true); od->afterAttack(false,true);
 EXPECT_EQ(states(child),states(old)); EXPECT_EQ(d->counterAttacks.available(),od->counterAttacks.available());
 EXPECT_EQ(a->getTreeVersion(),av); EXPECT_EQ(d->getTreeVersion(),dv);
 EXPECT_EQ(child->getForUpdate(a->unitId()).get(),a.get()); EXPECT_EQ(child->getForUpdate(d->unitId()).get(),d.get());
 EXPECT_EQ(states(parent),pb); EXPECT_EQ(states(sibling),sb);
}
