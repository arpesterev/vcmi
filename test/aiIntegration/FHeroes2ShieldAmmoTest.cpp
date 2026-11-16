/* GPL-2.0-or-later. FHeroes2 Shield remaining-ammunition classification. */
#include "StdInc.h"
#include "fheroesStrength/BasicNativeStrengthOracle.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include <vcmi/Environment.h>
namespace {
class ShieldAmmoEnvironment final : public Environment {
 std::shared_ptr<CCallback> gameCb; std::shared_ptr<CBattleCallback> battleCb;
public:
 ShieldAmmoEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<CBattleCallback> b):gameCb(std::move(g)),battleCb(std::move(b)){}
 const Services * services() const override{return LIBRARY;}
 const GameCb * game() const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id) const override{return battleCb->getBattle(id).get();}
};
}
class FHeroes2ShieldAmmoBattle : public BattleTestFixture, public ::testing::WithParamInterface<bool> {
protected:
 CStack * subject=nullptr; CStack * alternative=nullptr; CStack * threat=nullptr;
 std::shared_ptr<CBattleCallback> callback; std::shared_ptr<CPlayerBattleCallback> view; std::shared_ptr<ShieldAmmoEnvironment> environment;
 static auto shieldEffect(){return Selector::source(BonusSource::SPELL_EFFECT,BonusSourceID(SpellID(SpellID::AIR_SHIELD)));}
 void freshRound(){BattleNextRound next;next.battleID=battle()->getBattleID();server.applyPack(next);}
 void initialize(){startGame();ASSERT_FALSE(HasFatalFailure());startBattle();ASSERT_FALSE(HasFatalFailure());}
 void candidateSpell(SpellID id,int power=6,int mastery=MasteryLevel::BASIC){
  for(const auto & spell:LIBRARY->spellh->objects)attackerSideHero->removeSpellFromSpellbook(spell->id);
  teachSpell(attackerSideHero,id);attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,power,ChangeValueMode::ABSOLUTE);setMagicMastery(attackerSideHero,mastery);
  callback=std::make_shared<CBattleCallback>(attackerSideHero->getOwner(),nullptr);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<ShieldAmmoEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
 }
 void exhaustSubject(){ASSERT_NE(subject,nullptr);ASSERT_GT(subject->shots.available(),0);if(GetParam())subject->shots.use(subject->shots.total());ASSERT_TRUE(subject->isShooter());ASSERT_EQ(AIIntegration::fheroes2IsArcher(*subject),!GetParam());}
 void seedEffect(CGHeroInstance * hero,SpellID id,CStack * unit){freshRound();teachSpell(hero,id);hero->setPrimarySkill(PrimarySkill::SPELL_POWER,6,ChangeValueMode::ABSOLUTE);ASSERT_TRUE(castOn(hero,id,unit));}
 std::optional<BattleAction> choose(){
  EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
  const auto mana=attackerSideHero->mana;const auto casts=server.casts.size();
  std::vector<std::tuple<CStack *,int64_t,int,bool>> before;
  for(const auto & unit:battle()->stacks)before.emplace_back(unit.get(),unit->getAvailableHealth(),unit->shots.available(),unit->hasBonus(shieldEffect()));
  const auto result=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());
  EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),casts);
  for(const auto &[unit,hp,shots,shield]:before){EXPECT_EQ(unit->getAvailableHealth(),hp);EXPECT_EQ(unit->shots.available(),shots);EXPECT_EQ(unit->hasBonus(shieldEffect()),shield);}
  return result;
 }
 const CStack * execute(const BattleAction & action,SpellID id){
  EXPECT_EQ(action.spell,id);const auto * selected=dynamic_cast<const CStack *>(view->battleGetUnitByPos(action.target.front().hexValue));
  if(!selected){ADD_FAILURE()<<"No native selected stack";return nullptr;}
  const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();
  EXPECT_TRUE(castOn(attackerSideHero,id,selected));EXPECT_LT(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets+1);return selected;
 }
 void pair(BattleSide side){
  subject=addStack(side,CreatureID(2),BattleHex(4*17+5),22);
  alternative=addStack(side,CreatureID(2),BattleHex(7*17+5),20);
  ASSERT_NE(subject,nullptr);ASSERT_NE(alternative,nullptr);exhaustSubject();
  const double large=FHeroes2StrengthFixture::basicStrength(*subject),small=FHeroes2StrengthFixture::basicStrength(*alternative);ASSERT_GT(large,small);ASSERT_LT(large,small*1.25);
 }
};
TEST_P(FHeroes2ShieldAmmoBattle,directProtectionPremiumUsesRemainingShots){
 initialize();ASSERT_FALSE(HasFatalFailure());pair(BattleSide::ATTACKER);ASSERT_FALSE(HasFatalFailure());
 threat=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(5*17+13),100);ASSERT_NE(threat,nullptr);beginCombat();candidateSpell(SpellID::AIR_SHIELD);
 const auto action=choose();ASSERT_TRUE(action);const auto * expected=GetParam()?alternative:subject;EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),expected);
 const auto * actual=execute(*action,SpellID::AIR_SHIELD);ASSERT_NE(actual,nullptr);EXPECT_TRUE(actual->hasBonus(shieldEffect()));
}
TEST_P(FHeroes2ShieldAmmoBattle,enemyDispelPremiumUsesRemainingShots){
 initialize();ASSERT_FALSE(HasFatalFailure());pair(BattleSide::DEFENDER);ASSERT_FALSE(HasFatalFailure());
 threat=addStack(BattleSide::ATTACKER,CreatureID(2),BattleHex(5*17+13),20);ASSERT_NE(threat,nullptr);beginCombat();
 seedEffect(defenderSideHero,SpellID::AIR_SHIELD,subject);seedEffect(defenderSideHero,SpellID::AIR_SHIELD,alternative);ASSERT_FALSE(HasFatalFailure());
 candidateSpell(SpellID::DISPEL,6,MasteryLevel::ADVANCED);
 const auto action=choose();ASSERT_TRUE(action);const auto * expected=GetParam()?alternative:subject;EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),expected);
 const auto * actual=execute(*action,SpellID::DISPEL);ASSERT_NE(actual,nullptr);EXPECT_FALSE(actual->hasBonus(shieldEffect()));
}
TEST_P(FHeroes2ShieldAmmoBattle,friendlyDispelDoesNotOverchargeExhaustedShield){
 initialize();ASSERT_FALSE(HasFatalFailure());subject=addStack(BattleSide::ATTACKER,CreatureID(2),BattleHex(4*17+5),22);exhaustSubject();
 threat=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(5*17+13),100);ASSERT_NE(threat,nullptr);
 const auto * sample=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(6*17+13),1);ASSERT_NE(sample,nullptr);
 const int meleeCount=static_cast<int>(std::round(FHeroes2StrengthFixture::basicStrength(*threat)*0.55/0.45/FHeroes2StrengthFixture::basicStrength(*sample)));
 ASSERT_NE(addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(7*17+13),meleeCount),nullptr);beginCombat();
 seedEffect(attackerSideHero,SpellID::AIR_SHIELD,subject);seedEffect(defenderSideHero,SpellID::CURSE,subject);ASSERT_FALSE(HasFatalFailure());candidateSpell(SpellID::DISPEL);
 double total=0,ranged=0;for(const auto & unit:battle()->stacks)if(unit->alive()&&unit->unitSide()==BattleSide::DEFENDER){total+=FHeroes2StrengthFixture::basicStrength(*unit);if(AIIntegration::fheroes2IsArcher(*unit))ranged+=FHeroes2StrengthFixture::basicStrength(*unit);}
 const double protection=0.3*ranged/total;ASSERT_LT(protection,0.15);ASSERT_GT(protection*1.25,0.15);
 double ours=0;for(const auto & unit:battle()->stacks)if(unit->alive()&&unit->unitSide()==BattleSide::ATTACKER)ours+=FHeroes2StrengthFixture::basicStrength(*unit);
 double threshold=ours*ours/total*0.04;
 if(ranged/total>0.5)threshold*=0.5;
 if(static_cast<int64_t>(attackerSideHero->mana)*2<attackerSideHero->manaLimit())threshold*=2;
 const int cost=view->battleGetSpellCost(SpellID(SpellID::DISPEL).toSpell(),attackerSideHero);
 const double exhaustedNet=FHeroes2StrengthFixture::basicStrength(*subject)*(0.15-protection)/std::sqrt(std::max(1,cost)/3.0);
 ASSERT_GT(exhaustedNet,threshold)<<"Source exhausted-shooter removal must clear mana conservation";
 ASSERT_LT(FHeroes2StrengthFixture::basicStrength(*subject)*(0.15-protection*1.25),0);

 const auto action=choose();if(!GetParam()){EXPECT_FALSE(action);return;}ASSERT_TRUE(action);EXPECT_EQ(view->battleGetUnitByPos(action->target.front().hexValue),subject);
 const auto * actual=execute(*action,SpellID::DISPEL);ASSERT_EQ(actual,subject);EXPECT_FALSE(actual->hasBonus(shieldEffect()));EXPECT_FALSE(actual->hasBonusOfType(BonusType::ALWAYS_MINIMUM_DAMAGE));
}
TEST_P(FHeroes2ShieldAmmoBattle,oneRoundGuardIgnoresExhaustedUnmovedEnemy){
 initialize();ASSERT_FALSE(HasFatalFailure());
 const auto * protectedUnit=addStack(BattleSide::ATTACKER,CreatureID(2),BattleHex(4*17+5),22);ASSERT_NE(protectedUnit,nullptr);
 subject=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(7*17+13),30);exhaustSubject();
 threat=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(5*17+13),100);ASSERT_NE(threat,nullptr);beginCombat();
 ASSERT_TRUE(act(BattleAction::makeDefend(threat)));ASSERT_TRUE(threat->defended());ASSERT_FALSE(subject->moved());ASSERT_FALSE(subject->defended());
 candidateSpell(SpellID::AIR_SHIELD,1);const auto action=choose();if(GetParam()){EXPECT_FALSE(action);return;}ASSERT_TRUE(action);
 const auto * actual=execute(*action,SpellID::AIR_SHIELD);ASSERT_NE(actual,nullptr);EXPECT_TRUE(actual->hasBonus(shieldEffect()));
}
INSTANTIATE_TEST_SUITE_P(RemainingShots,FHeroes2ShieldAmmoBattle,::testing::Bool(),[](const auto & info){return info.param?"Exhausted":"Armed";});
