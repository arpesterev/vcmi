/* GPL-2.0-or-later. Source-order Fireball resistance versus native all-or-none outcomes. */
#include "StdInc.h"
#include "fheroesStrength/BasicNativeStrengthOracle.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include <vcmi/Environment.h>
#include "../../lib/bonuses/BonusParameters.h"
namespace {
class FireballEnvironment final : public Environment {
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<CBattleCallback> battleCb;
public:
 FireballEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override{return LIBRARY;}
 const GameCb * game() const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override{return battleCb->getBattle(id).get();}
};
}
class FHeroes2FireballResistanceBattle : public BattleTestFixture,public ::testing::WithParamInterface<int> {
protected:
 CStack * left=nullptr;CStack * right=nullptr;CStack * alternative=nullptr;
 std::shared_ptr<CBattleCallback> callback;std::shared_ptr<CPlayerBattleCallback> view;std::shared_ptr<FireballEnvironment> environment;
 int64_t raw=0;
 void resistance(CStack * unit,int percent){unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,percent,BonusSourceID()));}
 void prepare(bool threshold=false){
  startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());
  for(const auto & spell:LIBRARY->spellh->objects)attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero,SpellID::FIREBALL);attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,7,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<FireballEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
  const auto * sample=battle()->stacks.front().get();spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::FIREBALL).toSpell());auto mechanics=SpellID(SpellID::FIREBALL).toSpell()->battleMechanics(&cast);raw=mechanics->adjustEffectValue(sample);
  ASSERT_GT(raw,20);ASSERT_LT(raw,100);
  left=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(4*17+6),threshold?(raw+9)/10:raw/10*2/3);
  if(!threshold)right=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(4*17+7),raw/10*2/3);
  alternative=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(8*17+13),(raw+9)/10);
  auto * reserve=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(9*17+3),1000);
  ASSERT_NE(left,nullptr);ASSERT_NE(alternative,nullptr);ASSERT_NE(reserve,nullptr);
  if(threshold){injure(left,left->getAvailableHealth()-raw);}injure(alternative,alternative->getAvailableHealth()-raw);
  resistance(left,GetParam());if(right)resistance(right,GetParam());resistance(reserve,100);
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 double sourceValue(const CStack * unit,int resist){
  if(resist>=100)return 0;const uint32_t damage=static_cast<uint32_t>(raw)*(100-resist)/100;
  if(!damage)return 0;const double strength=unit->estimateCombatValue();
  if(damage>=unit->getAvailableHealth()){double army=0;for(const auto & u:battle()->stacks)if(u->alive()&&u->unitSide()==BattleSide::DEFENDER)army+=u->estimateCombatValue();return strength+army*.035;}
  double fraction=static_cast<double>(damage)/unit->getAvailableHealth();if(unit->hasBonusOfType(BonusType::NOT_ACTIVE))fraction+=fraction-1;return fraction*strength;
 }
 std::optional<BattleAction> choose(bool farewell=false){
  auto mana=attackerSideHero->mana;auto packets=server.casts.size();std::vector<std::tuple<CStack *,int64_t,int,BattleHex>> before;
  for(const auto & u:battle()->stacks)before.emplace_back(u.get(),u->getAvailableHealth(),u->getCount(),u->getPosition());
  auto result=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get(),nullptr,farewell);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);EXPECT_EQ(battle()->stacks.size(),before.size());
  for(const auto &[u,hp,count,pos]:before){EXPECT_EQ(u->getAvailableHealth(),hp);EXPECT_EQ(u->getCount(),count);EXPECT_EQ(u->getPosition(),pos);}return result;
 }
 bool affects(const BattleAction & action,const CStack * unit){spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::FIREBALL).toSpell());auto mechanics=SpellID(SpellID::FIREBALL).toSpell()->battleMechanics(&cast);auto affected=mechanics->getAffectedStacks(spells::Target{battle::Destination(action.target.front().hexValue)});return std::find(affected.begin(),affected.end(),unit)!=affected.end();}
};
TEST_P(FHeroes2FireballResistanceBattle,clusterChoiceUsesDiscountBeforeLethalValue){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 const double cluster=sourceValue(left,GetParam())+sourceValue(right,GetParam()),single=sourceValue(alternative,0);ASSERT_NE(cluster,single);
 const auto action=choose();ASSERT_TRUE(action);EXPECT_EQ(action->spell,SpellID::FIREBALL);
 EXPECT_EQ(affects(*action,alternative),single>cluster);if(cluster>single){EXPECT_TRUE(affects(*action,left));EXPECT_TRUE(affects(*action,right));}
}
TEST_P(FHeroes2FireballResistanceBattle,exactLethalBoundaryIsNotProbabilityTimesKillBonus){
 prepare(true);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(left->getAvailableHealth(),raw);ASSERT_EQ(alternative->getAvailableHealth(),raw);
 const auto action=choose();ASSERT_TRUE(action);if(GetParam()>0){ASSERT_LT(sourceValue(left,GetParam()),sourceValue(alternative,0));EXPECT_TRUE(affects(*action,alternative));}else EXPECT_TRUE(affects(*action,left)||affects(*action,alternative));
}
TEST_P(FHeroes2FireballResistanceBattle,realAreaCastRemainsAllOrNonePerTarget){
 prepare();ASSERT_FALSE(HasFatalFailure());const auto leftHP=left->getAvailableHealth(),rightHP=right->getAvailableHealth(),alternativeHP=alternative->getAvailableHealth();const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();
 left->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,100,BonusSourceID()));
 if(GetParam()==100){EXPECT_FALSE(castOn(attackerSideHero,SpellID::FIREBALL,left));EXPECT_EQ(server.casts.size(),packets);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(left->getAvailableHealth(),leftHP);EXPECT_EQ(right->getAvailableHealth(),rightHP);return;}
 ASSERT_TRUE(castOn(attackerSideHero,SpellID::FIREBALL,left));EXPECT_TRUE(server.casts.back().announcement.reflectedCres.empty());EXPECT_EQ(server.casts.size(),packets+1);EXPECT_LT(attackerSideHero->mana,mana);EXPECT_EQ(alternative->getAvailableHealth(),alternativeHP);
 for(const auto & entry:std::vector<std::pair<const CStack *,int64_t>>{{left,leftHP},{right,rightHP}}){const auto loss=entry.second-entry.first->getAvailableHealth();const auto full=std::min(raw,entry.second);EXPECT_TRUE(loss==0||loss==full);if(GetParam()==0){EXPECT_EQ(loss,full);}if(GetParam()==100){EXPECT_EQ(loss,0);}}
}
TEST_P(FHeroes2FireballResistanceBattle,partialCandidatesRejectNonstandardDamageMetadata){
 prepare(true);ASSERT_FALSE(HasFatalFailure());for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER&&u.get()!=left){resistance(u.get(),100);}
 auto * spell=SpellID(SpellID::FIREBALL).toSpell();spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spell);auto mechanics=spell->battleMechanics(&cast);
 auto & level=const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(mechanics->getEffectLevel()));
 struct Restore {JsonNode & target;JsonNode saved;~Restore(){target=saved;}} restore{level.battleEffects,level.battleEffects};
 level.battleEffects["directDamage"]["killByPercentage"].Bool()=true;
 const auto action=choose();if(GetParam()==0){EXPECT_TRUE(action);}else{EXPECT_FALSE(action);}
}
TEST_P(FHeroes2FireballResistanceBattle,partialCandidatesRejectCombatEventAftermath){
 prepare(true);ASSERT_FALSE(HasFatalFailure());for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER&&u.get()!=left){resistance(u.get(),100);}
 BonusParametersOnCombatEvent::CombatEffectBonus effect;effect.bonus=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::LUCK,BonusSource::OTHER,1,BonusSourceID());effect.targetEnemy=false;
 BonusParametersOnCombatEvent reaction;reaction.effects.emplace_back(effect);
 auto trigger=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ON_COMBAT_EVENT,BonusSource::OTHER,0,BonusSourceID(),BonusSubtypeID(BonusCustomSubtype(static_cast<int>(CombatEventType::SPELL_HIT))));trigger->parameters=std::make_shared<BonusParameters>(reaction);left->addNewBonus(trigger);
 const auto action=choose();if(GetParam()==0){EXPECT_TRUE(action);}else{EXPECT_FALSE(action);}
}
TEST_P(FHeroes2FireballResistanceBattle,discountedDamageRetainsWakePenalty){
 prepare(true);ASSERT_FALSE(HasFatalFailure());for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER){resistance(u.get(),100);}
 auto * sleeping=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(3*17+8),10);ASSERT_NE(sleeping,nullptr);
 teachSpell(attackerSideHero,SpellID::BLIND);ASSERT_TRUE(castOn(attackerSideHero,SpellID::BLIND,sleeping));ASSERT_TRUE(sleeping->hasBonusOfType(BonusType::NOT_ACTIVE));resistance(sleeping,GetParam());
 BattleNextRound round;round.battleID=battle()->getBattleID();server.applyPack(round);attackerSideHero->removeSpellFromSpellbook(SpellID::BLIND);ASSERT_TRUE(sleeping->hasBonusOfType(BonusType::NOT_ACTIVE));
 ASSERT_LT(raw,sleeping->getAvailableHealth());ASSERT_GT(raw*2,sleeping->getAvailableHealth());ASSERT_LT(sourceValue(sleeping,50),0);
 EXPECT_EQ(choose().has_value(),sourceValue(sleeping,GetParam())>0);
}
TEST_P(FHeroes2FireballResistanceBattle,friendlyCollateralUsesItsOwnResistance){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 left->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));right->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));
 auto * otherLeft=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(8*17+7),left->getCount());
 auto * otherRight=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(8*17+8),right->getCount());ASSERT_NE(otherLeft,nullptr);ASSERT_NE(otherRight,nullptr);
 for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER&&u.get()!=left&&u.get()!=right&&u.get()!=otherLeft&&u.get()!=otherRight){resistance(u.get(),100);}
 std::vector<CStack *> friends;
 const auto leftNeighbours=left->getPosition().getNeighbouringTiles();const auto rightNeighbours=right->getPosition().getNeighbouringTiles();
 for(const auto hex:leftNeighbours)if(hex.isAvailable()&&std::find(rightNeighbours.begin(),rightNeighbours.end(),hex)!=rightNeighbours.end()&&!battle()->battleGetUnitByPos(hex)){
  auto * ally=addStack(BattleSide::ATTACKER,CreatureID(0),hex,5);ASSERT_NE(ally,nullptr);resistance(ally,GetParam());friends.push_back(ally);
 }
 ASSERT_EQ(friends.size(),2u);ASSERT_EQ(left->estimateCombatValue(),otherLeft->estimateCombatValue());ASSERT_EQ(right->estimateCombatValue(),otherRight->estimateCombatValue());
 const bool zeroCollateral=static_cast<uint32_t>(raw)*(100-GetParam())/100==0;
 const auto action=choose();ASSERT_TRUE(action);EXPECT_EQ(affects(*action,left)&&affects(*action,right),zeroCollateral);EXPECT_EQ(affects(*action,otherLeft)&&affects(*action,otherRight),!zeroCollateral);
 if(GetParam()==99){EXPECT_TRUE(std::any_of(friends.begin(),friends.end(),[&](const CStack * ally){return affects(*action,ally);}));}
}
// Historical test ID retained; expected ranking now follows pinned farewell kills.
TEST_P(FHeroes2FireballResistanceBattle,farewellRetainsExistingNativeProjection){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 const auto sourceKills=[&](const CStack * unit,int resistance){
  const uint32_t damage=static_cast<uint32_t>(raw)*(100-resistance)/100;
  const auto remaining=std::max<int64_t>(0,unit->getAvailableHealth()-damage);
  const auto hp=unit->getMaxHealth();
  const auto survivors=remaining/hp+(remaining%hp!=0);
  return FHeroes2StrengthFixture::basicStrength(*unit)/unit->getCount()*(unit->getCount()-survivors);
 };
 const double cluster=sourceKills(left,GetParam())+sourceKills(right,GetParam()),single=sourceKills(alternative,0);
 const auto action=choose(true);ASSERT_TRUE(action);
 if(cluster>single){EXPECT_TRUE(affects(*action,left));EXPECT_TRUE(affects(*action,right));}
 else if(single>cluster){EXPECT_TRUE(affects(*action,alternative));}
 else EXPECT_TRUE(affects(*action,alternative)||(affects(*action,left)&&affects(*action,right)));
}
TEST_P(FHeroes2FireballResistanceBattle,fullHitFriendlySurvivalGuardIsNotDiscounted){
 prepare();ASSERT_FALSE(HasFatalFailure());left->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));right->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));
 CStack * onlyFriend=nullptr;for(const auto & unit:battle()->stacks)if(unit->unitSide()==BattleSide::ATTACKER){ASSERT_EQ(onlyFriend,nullptr);onlyFriend=unit.get();}ASSERT_NE(onlyFriend,nullptr);
 BattleStackMoved moved;moved.battleID=battle()->getBattleID();moved.stack=onlyFriend->unitId();moved.tilesToMove.insert(BattleHex(4*17+5));moved.teleporting=true;server.applyPack(moved);resistance(onlyFriend,GetParam());
 const auto badTarget=spells::Target{battle::Destination(left->getPosition())};auto badPreview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast badCast(badPreview.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::FIREBALL).toSpell());badCast.castEval(badPreview->getServerCallback(),badTarget);
 if(GetParam()<100){EXPECT_EQ(badPreview->battleGetUnitByID(onlyFriend->unitId())->getAvailableHealth(),0);}if(GetParam()==99){ASSERT_EQ(static_cast<uint32_t>(raw)*(100-GetParam())/100,0);}
 const auto action=choose();ASSERT_TRUE(action);EXPECT_FALSE(affects(*action,onlyFriend));
 auto selectedPreview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast selectedCast(selectedPreview.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::FIREBALL).toSpell());selectedCast.castEval(selectedPreview->getServerCallback(),spells::Target{battle::Destination(action->target.front().hexValue)});EXPECT_TRUE(selectedPreview->battleGetUnitByID(onlyFriend->unitId())->alive());
}
INSTANTIATE_TEST_SUITE_P(Resistance,FHeroes2FireballResistanceBattle,::testing::Values(0,1,50,99,100));
