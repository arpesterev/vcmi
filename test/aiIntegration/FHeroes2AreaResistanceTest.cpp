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
class AreaEnvironment final : public Environment {
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<CBattleCallback> battleCb;
public:
 AreaEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override{return LIBRARY;}
 const GameCb * game() const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override{return battleCb->getBattle(id).get();}
};
}
class FHeroes2AreaResistanceBattle : public BattleTestFixture,public ::testing::WithParamInterface<std::tuple<int,int>> {
protected:
 CStack * left=nullptr;CStack * right=nullptr;CStack * alternative=nullptr;
 std::shared_ptr<CBattleCallback> callback;std::shared_ptr<CPlayerBattleCallback> view;std::shared_ptr<AreaEnvironment> environment;
 int64_t raw=0;
 SpellID spellID() const{return SpellID(std::get<0>(GetParam()));}
 int resist() const{return std::get<1>(GetParam());}
 void resistance(CStack * unit,int percent){unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,percent,BonusSourceID()));}
 void prepare(bool threshold=false){
  startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());
  for(const auto & spell:LIBRARY->spellh->objects)attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero,spellID());attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,7,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<AreaEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
  const auto * sample=battle()->stacks.front().get();spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);raw=mechanics->adjustEffectValue(sample);
  ASSERT_GT(raw,20);ASSERT_LT(raw,500);
  left=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(4*17+6),threshold?(raw+9)/10:raw/10*2/3);
  if(!threshold)right=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(4*17+7),raw/10*2/3);
  alternative=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(8*17+13),(raw+9)/10);
  auto * reserve=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(9*17+3),1000);
  ASSERT_NE(left,nullptr);ASSERT_NE(alternative,nullptr);ASSERT_NE(reserve,nullptr);
  if(threshold){injure(left,left->getAvailableHealth()-raw);}injure(alternative,alternative->getAvailableHealth()-raw);
  resistance(left,resist());if(right)resistance(right,resist());resistance(reserve,100);
  for(const auto * u:std::vector<const CStack *>{left,right,alternative})if(u){ASSERT_EQ(mechanics->adjustEffectValue(u),raw);}
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 double sourceValue(const CStack * unit,int resist){
  if(resist>=100) return 0;
  const uint32_t damage=static_cast<uint32_t>(raw)*(100-resist)/100;
  if(!damage) return 0;
  const double strength=unit->estimateCombatValue();
  if(damage>=unit->getAvailableHealth()){double army=0;for(const auto & u:battle()->stacks)if(u->alive()&&u->unitSide()==BattleSide::DEFENDER)army+=u->estimateCombatValue();return strength+army*.035;}
  double fraction=static_cast<double>(damage)/unit->getAvailableHealth();if(unit->hasBonusOfType(BonusType::NOT_ACTIVE))fraction+=fraction-1;return fraction*strength;
 }
 std::optional<BattleAction> choose(bool farewell=false){
  auto mana=attackerSideHero->mana;auto packets=server.casts.size();std::vector<std::tuple<CStack *,int64_t,int,BattleHex>> before;
  for(const auto & u:battle()->stacks)before.emplace_back(u.get(),u->getAvailableHealth(),u->getCount(),u->getPosition());
  auto result=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get(),nullptr,farewell);EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);EXPECT_EQ(battle()->stacks.size(),before.size());
  for(const auto &[u,hp,count,pos]:before){EXPECT_EQ(u->getAvailableHealth(),hp);EXPECT_EQ(u->getCount(),count);EXPECT_EQ(u->getPosition(),pos);}return result;
 }
 bool affects(const BattleAction & action,const CStack * unit){spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);auto affected=mechanics->getAffectedStacks(spells::Target{battle::Destination(action.target.front().hexValue)});return std::find(affected.begin(),affected.end(),unit)!=affected.end();}
};

TEST_P(FHeroes2AreaResistanceBattle,clusterChoiceUsesDiscountBeforeLethalValue){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 const double cluster=sourceValue(left,resist())+sourceValue(right,resist()),single=sourceValue(alternative,0);ASSERT_NE(cluster,single);
 const auto action=choose();ASSERT_TRUE(action);EXPECT_EQ(action->spell,spellID());
 EXPECT_EQ(affects(*action,alternative),single>cluster);if(cluster>single){EXPECT_TRUE(affects(*action,left));EXPECT_TRUE(affects(*action,right));}
}
TEST_P(FHeroes2AreaResistanceBattle,exactLethalBoundaryUsesSourceOrder){
 prepare(true);ASSERT_FALSE(HasFatalFailure());const auto action=choose();ASSERT_TRUE(action);
 if(resist()>0){ASSERT_LT(sourceValue(left,resist()),sourceValue(alternative,0));EXPECT_TRUE(affects(*action,alternative));}
 else EXPECT_TRUE(affects(*action,left)||affects(*action,alternative));
}
TEST_P(FHeroes2AreaResistanceBattle,alteredRangeFailsClosedOnlyForPartialCandidates){
 prepare(true);ASSERT_FALSE(HasFatalFailure());for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER&&u.get()!=left)resistance(u.get(),100);
 auto * spell=spellID().toSpell();spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spell);auto mechanics=spell->battleMechanics(&cast);
 auto & range=const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(mechanics->getRangeLevel())).range;
 struct Restore{std::vector<int>& target;std::vector<int> old;~Restore(){target=old;}} restore{range,range};
 for(const auto & wrong:std::vector<std::vector<int>>{{0,1,2,3},{0},spellID()==SpellID::FROST_RING?std::vector<int>{0,1}:std::vector<int>{1}}){range=wrong;EXPECT_EQ(choose().has_value(),resist()==0);}
}
// Historical test ID retained; expected ranking now follows pinned farewell kills.
TEST_P(FHeroes2AreaResistanceBattle,farewellKeepsNativeProjection){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 const auto sourceKills=[&](const CStack * unit,int resistance){
  const uint32_t damage=static_cast<uint32_t>(raw)*(100-resistance)/100;
  const auto remaining=std::max<int64_t>(0,unit->getAvailableHealth()-damage);
  const auto hp=unit->getMaxHealth();
  const auto survivors=remaining/hp+(remaining%hp!=0);
  return FHeroes2StrengthFixture::basicStrength(*unit)/unit->getCount()*(unit->getCount()-survivors);
 };
 const double cluster=sourceKills(left,resist())+sourceKills(right,resist()),single=sourceKills(alternative,0);
 const auto action=choose(true);ASSERT_TRUE(action);
 if(cluster>single){EXPECT_TRUE(affects(*action,left));EXPECT_TRUE(affects(*action,right));}
 else if(single>cluster){EXPECT_TRUE(affects(*action,alternative));}
 else EXPECT_TRUE(affects(*action,alternative)||(affects(*action,left)&&affects(*action,right)));
}
// Expected occupants below are hand-placed on a single horizontal axial row.
// Neither expected membership nor expected multiplicity comes from the spell query.
TEST_P(FHeroes2AreaResistanceBattle,shapeAndActualCastUseIndependentOccupants){
 startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());
 teachSpell(attackerSideHero,spellID());attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,7,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
 callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<AreaEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 std::vector<CStack *> placed;for(int i=0;i<4;++i){placed.push_back(addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(93+i),200));ASSERT_NE(placed.back(),nullptr);resistance(placed.back(),resist());}
 beginCombat();ASSERT_FALSE(HasFatalFailure());
 spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);const spells::Target aim{battle::Destination(BattleHex(93))};
 const std::vector<int> shape=spellID()==SpellID::FROST_RING?std::vector<int>{1}:spellID()==SpellID::INFERNO?std::vector<int>{0,1,2}:std::vector<int>{0,1};
 EXPECT_EQ(spellID().toSpell()->getLevelInfo(mechanics->getRangeLevel()).range,shape);
 const auto actual=mechanics->getAffectedStacks(aim);std::set<uint32_t> expected;
 for(int i:shape)if(resist()<100)expected.insert(placed[i]->unitId());
 std::set<uint32_t> actualIDs;for(const auto * u:actual)actualIDs.insert(u->unitId());EXPECT_EQ(actualIDs,expected);EXPECT_EQ(actual.size(),expected.size());
 auto preview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast pc(preview.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();
 std::vector<int64_t> hp,damage;for(auto * u:placed){hp.push_back(u->getAvailableHealth());damage.push_back(mechanics->adjustEffectValue(u));}
 pc.castEval(preview->getServerCallback(),aim);
 for(size_t i=0;i<placed.size();++i){EXPECT_EQ(placed[i]->getAvailableHealth(),hp[i]);EXPECT_EQ(preview->battleGetUnitByID(placed[i]->unitId())->getAvailableHealth(),hp[i]-(expected.count(placed[i]->unitId())?damage[i]:0));}
 EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);
 if(resist()==100){EXPECT_FALSE(castAsHero(attackerSideHero,spellID(),battle::Target{battle::Destination(BattleHex(93))}));EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);return;}
 placed[1]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,100,BonusSourceID()));
 ASSERT_TRUE(castAsHero(attackerSideHero,spellID(),battle::Target{battle::Destination(BattleHex(93))}));ASSERT_EQ(server.casts.size(),packets+1);EXPECT_LT(attackerSideHero->mana,mana);EXPECT_TRUE(server.casts.back().announcement.reflectedCres.empty());
 for(size_t i=0;i<placed.size();++i){const auto loss=hp[i]-placed[i]->getAvailableHealth();if(!expected.count(placed[i]->unitId())){EXPECT_EQ(loss,0);}else {EXPECT_TRUE(loss==0||loss==damage[i]);if(resist()==0){EXPECT_EQ(loss,damage[i]);}}}
}
TEST_P(FHeroes2AreaResistanceBattle,edgeShapeClipsWithoutWrapping){
 startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());teachSpell(attackerSideHero,spellID());
 callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
 std::vector<CStack *> placed;for(int x=1;x<=4;++x){placed.push_back(addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(x),200));ASSERT_NE(placed.back(),nullptr);}
 // Opposite edge must not be reached by arithmetic wrap-around.
 auto * far=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(15),200);ASSERT_NE(far,nullptr);beginCombat();ASSERT_FALSE(HasFatalFailure());
 spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);
 const auto actual=mechanics->getAffectedStacks(spells::Target{battle::Destination(BattleHex(1))});std::set<uint32_t> expected{placed[1]->unitId()};if(spellID()!=SpellID::FROST_RING)expected.insert(placed[0]->unitId());if(spellID()==SpellID::INFERNO)expected.insert(placed[2]->unitId());
 std::set<uint32_t> ids;for(const auto * u:actual)ids.insert(u->unitId());EXPECT_EQ(ids,expected);EXPECT_EQ(actual.size(),expected.size());
}
TEST_P(FHeroes2AreaResistanceBattle,wideCenterTailIsIncludedExactlyOnce){
 startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());teachSpell(attackerSideHero,spellID());
 callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<AreaEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 auto * wide=addStack(BattleSide::DEFENDER,CreatureID(10),BattleHex(93),200);ASSERT_NE(wide,nullptr);ASSERT_TRUE(wide->coversPos(BattleHex(93)));ASSERT_TRUE(wide->coversPos(BattleHex(94)));beginCombat();ASSERT_FALSE(HasFatalFailure());
 spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);const spells::Target aim{battle::Destination(BattleHex(93))};const auto actual=mechanics->getAffectedStacks(aim);
 ASSERT_EQ(actual.size(),1u);EXPECT_EQ(actual.front(),wide);const auto hp=wide->getAvailableHealth(),loss=mechanics->adjustEffectValue(wide);
 auto preview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast pc(preview.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());pc.castEval(preview->getServerCallback(),aim);EXPECT_EQ(preview->battleGetUnitByID(wide->unitId())->getAvailableHealth(),hp-loss);EXPECT_EQ(wide->getAvailableHealth(),hp);
 ASSERT_TRUE(castAsHero(attackerSideHero,spellID(),battle::Target{battle::Destination(BattleHex(93))}));EXPECT_EQ(wide->getAvailableHealth(),hp-loss);
}
TEST_P(FHeroes2AreaResistanceBattle,canonicalElementImmunityAndVulnerabilityAreNativeInputs){
 startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());teachSpell(attackerSideHero,spellID());attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,7,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,MasteryLevel::BASIC);
 callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<AreaEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 const int immuneCreature=spellID()==SpellID::FROST_RING?115:spellID()==SpellID::INFERNO?114:121;
 const int vulnerableCreature=spellID()==SpellID::FROST_RING?114:spellID()==SpellID::INFERNO?115:113;
 auto * immune=addStack(BattleSide::DEFENDER,CreatureID(immuneCreature),BattleHex(93),200);auto * vulnerable=addStack(BattleSide::DEFENDER,CreatureID(vulnerableCreature),BattleHex(96),200);ASSERT_NE(immune,nullptr);ASSERT_NE(vulnerable,nullptr);
 // Ring must intersect both: Water Elemental tail94; other immune units head94.
 if(!immune->coversPos(BattleHex(94))){BattleStackMoved move;move.battleID=battle()->getBattleID();move.stack=immune->unitId();move.tilesToMove.insert(BattleHex(94));move.teleporting=true;server.applyPack(move);}ASSERT_TRUE(immune->coversPos(BattleHex(94)));
 beginCombat();ASSERT_FALSE(HasFatalFailure());spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto mechanics=spellID().toSpell()->battleMechanics(&cast);const spells::Target aim{battle::Destination(BattleHex(95))};const auto actual=mechanics->getAffectedStacks(aim);
 ASSERT_EQ(actual.size(),1u);EXPECT_EQ(actual.front(),vulnerable);const auto normal=mechanics->adjustEffectValue(battle()->stacks.front().get()),adjusted=mechanics->adjustEffectValue(vulnerable);ASSERT_GT(normal,0);EXPECT_EQ(adjusted,normal*2);
 const auto immuneHP=immune->getAvailableHealth(),vulnerableHP=vulnerable->getAvailableHealth();ASSERT_TRUE(castAsHero(attackerSideHero,spellID(),battle::Target{battle::Destination(BattleHex(95))}));EXPECT_EQ(immune->getAvailableHealth(),immuneHP);EXPECT_EQ(vulnerable->getAvailableHealth(),vulnerableHP-adjusted);
}
TEST_P(FHeroes2AreaResistanceBattle,fullHitFriendlySurvivalGuardRemainsConservative){
 prepare();ASSERT_FALSE(HasFatalFailure());left->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));right->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));
 CStack * onlyFriend=nullptr;for(const auto & unit:battle()->stacks)if(unit->unitSide()==BattleSide::ATTACKER){ASSERT_EQ(onlyFriend,nullptr);onlyFriend=unit.get();}ASSERT_NE(onlyFriend,nullptr);
 BattleStackMoved moved;moved.battleID=battle()->getBattleID();moved.stack=onlyFriend->unitId();moved.tilesToMove.insert(BattleHex(4*17+5));moved.teleporting=true;server.applyPack(moved);resistance(onlyFriend,resist());
 const auto badTarget=spells::Target{battle::Destination(left->getPosition())};auto badPreview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast badCast(badPreview.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());badCast.castEval(badPreview->getServerCallback(),badTarget);
 if(resist()<100){EXPECT_EQ(badPreview->battleGetUnitByID(onlyFriend->unitId())->getAvailableHealth(),0);}
 const auto action=choose();ASSERT_TRUE(action);EXPECT_FALSE(affects(*action,onlyFriend));
 auto selectedPreview=std::make_shared<HypotheticBattle>(environment.get(),view);spells::BattleCast selectedCast(selectedPreview.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());selectedCast.castEval(selectedPreview->getServerCallback(),spells::Target{battle::Destination(action->target.front().hexValue)});EXPECT_TRUE(selectedPreview->battleGetUnitByID(onlyFriend->unitId())->alive());
}
TEST_P(FHeroes2AreaResistanceBattle,discountedDamageRetainsWakePenalty){
 prepare(true);ASSERT_FALSE(HasFatalFailure());for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER){resistance(u.get(),100);}
 auto * sleeping=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(3*17+8),(raw*3/2+9)/10);ASSERT_NE(sleeping,nullptr);
 teachSpell(attackerSideHero,SpellID::BLIND);ASSERT_TRUE(castOn(attackerSideHero,SpellID::BLIND,sleeping));ASSERT_TRUE(sleeping->hasBonusOfType(BonusType::NOT_ACTIVE));resistance(sleeping,resist());
 BattleNextRound round;round.battleID=battle()->getBattleID();server.applyPack(round);attackerSideHero->removeSpellFromSpellbook(SpellID::BLIND);ASSERT_TRUE(sleeping->hasBonusOfType(BonusType::NOT_ACTIVE));
 spells::BattleCast scalarCast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto scalarMechanics=spellID().toSpell()->battleMechanics(&scalarCast);ASSERT_EQ(scalarMechanics->adjustEffectValue(sleeping),raw);
 ASSERT_LT(raw,sleeping->getAvailableHealth());ASSERT_GT(raw*2,sleeping->getAvailableHealth());ASSERT_LT(sourceValue(sleeping,50),0);
 EXPECT_EQ(choose().has_value(),sourceValue(sleeping,resist())>0);
}
TEST_P(FHeroes2AreaResistanceBattle,friendlyCollateralUsesItsOwnResistance){
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_NE(right,nullptr);
 attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,1,ChangeValueMode::ABSOLUTE);
 spells::BattleCast scalarCast(view.get(),attackerSideHero,spells::Mode::HERO,spellID().toSpell());auto scalarMechanics=spellID().toSpell()->battleMechanics(&scalarCast);raw=scalarMechanics->adjustEffectValue(left);ASSERT_GT(raw,0);ASSERT_LT(raw,100);
 left->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));right->removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));
 auto * otherLeft=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(9*17+13),left->getCount());
 auto * otherRight=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(9*17+14),right->getCount());ASSERT_NE(otherLeft,nullptr);ASSERT_NE(otherRight,nullptr);
 for(const auto & u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER&&u.get()!=left&&u.get()!=right&&u.get()!=otherLeft&&u.get()!=otherRight){resistance(u.get(),100);}
 std::vector<CStack *> friends;
 // Cover every aim that can hit this pair, including Cold Ring's empty center.
 // Two common-neighbor allies alone do not constrain ring/radius-two aiming.
 for(int index=0;index<GameConstants::BFIELD_SIZE;++index){const BattleHex hex(index);
  if(hex.isAvailable()&&!battle()->battleGetUnitByPos(hex)
    &&(BattleHex::getDistance(hex,left->getPosition())<=2||BattleHex::getDistance(hex,right->getPosition())<=2)){
   auto * ally=addStack(BattleSide::ATTACKER,CreatureID(0),hex,5);ASSERT_NE(ally,nullptr);resistance(ally,resist());friends.push_back(ally);
  }
 }
 ASSERT_GT(friends.size(),10u);ASSERT_EQ(left->estimateCombatValue(),otherLeft->estimateCombatValue());ASSERT_EQ(right->estimateCombatValue(),otherRight->estimateCombatValue());
 for(const auto * u:friends){ASSERT_EQ(scalarMechanics->adjustEffectValue(u),raw);}
 const bool zeroCollateral=static_cast<uint32_t>(raw)*(100-resist())/100==0;
 const auto action=choose();ASSERT_TRUE(action);EXPECT_EQ(affects(*action,left)&&affects(*action,right),zeroCollateral);EXPECT_EQ(affects(*action,otherLeft)&&affects(*action,otherRight),!zeroCollateral);
 if(resist()==99){EXPECT_TRUE(std::any_of(friends.begin(),friends.end(),[&](const CStack * ally){return affects(*action,ally);}));}
}
INSTANTIATE_TEST_SUITE_P(SpellsAndResistance,FHeroes2AreaResistanceBattle,::testing::Combine(
 ::testing::Values(int(SpellID::FROST_RING),int(SpellID::METEOR_SHOWER),int(SpellID::INFERNO)),::testing::Values(0,1,50,99,100)));
