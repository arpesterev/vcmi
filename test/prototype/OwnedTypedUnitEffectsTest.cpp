#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/OwnedBonusLogicalDomain.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/BonusCache.h"
#include "lib/battle/OwnedCreature.h"
#include "lib/GameLibrary.h"
#include "lib/CCreatureHandler.h"
class OwnedTypedUnitEffectsTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add() { return addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(5,5),10); }
 static Bonus effect() { Bonus b(BonusDuration::N_TURNS,BonusType::STACKS_SPEED,BonusSource::SPELL_EFFECT,7,SpellID(SpellID::HASTE));b.turnsRemain=2;return b; }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT,SpellID(SpellID::HASTE)).And(Selector::type()(BonusType::STACKS_SPEED)); }
 void packet(CStack * s,const std::vector<Bonus> & bonuses,bool update) { SetStackEffect p;p.battleID=BattleID(0);(update?p.toUpdate:p.toAdd).emplace_back(s->unitId(),bonuses);gameHandler->sendAndApply(p); }
 static void same(const IBonusBearer & a,const IBonusBearer & b,const CSelector & select) { auto av=a.getAllBonuses(select),bv=b.getAllBonuses(select);ASSERT_EQ(av->size(),bv->size());for(size_t i=0;i<av->size();++i){EXPECT_EQ((*av)[i]->toJsonNode(),(*bv)[i]->toJsonNode());EXPECT_EQ((*av)[i]->bonusOwner,(*bv)[i]->bonusOwner);} }
};
TEST_F(OwnedTypedUnitEffectsTest, LocalUpdateExtendsOnlyDurationAndNoopInvalidates)
{
 auto *s=add();auto b=effect();packet(s,{b},false);auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto unit=frame->getForUpdate(s->unitId());auto change=b;change.turnsRemain=8;change.val=99;change.hidden=true;change.stacking="ignored";
 auto event=frame->makeOwnedBonusEvent({change});frame->updateUnitBonus(s->unitId(),event);packet(s,{change},true);same(*unit,*s,selected());EXPECT_EQ(unit->getAllBonuses(selected())->front()->val,7);
 auto rev=unit->getTreeVersion();frame->updateUnitBonus(s->unitId(),event);packet(s,{change},true);EXPECT_NE(unit->getTreeVersion(),rev);same(*unit,*s,selected());
 change.turnsRemain=1;rev=unit->getTreeVersion();frame->updateUnitBonus(s->unitId(),frame->makeOwnedBonusEvent({change}));packet(s,{change},true);EXPECT_NE(unit->getTreeVersion(),rev);EXPECT_EQ(unit->getAllBonuses(selected())->front()->turnsRemain,8);same(*unit,*s,selected());
 auto empty=frame->makeOwnedBonusEvent({});rev=unit->getTreeVersion();frame->updateUnitBonus(s->unitId(),empty);packet(s,{},true);EXPECT_EQ(unit->getTreeVersion(),rev);
}
TEST_F(OwnedTypedUnitEffectsTest, InheritedMatchDoesNotBecomeLocalAndSourceQuirkMatches)
{
 auto*s=add();auto b=effect();attackerSideHero->addNewBonus(std::make_shared<Bonus>(b));auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto change=b;change.turnsRemain=8;
 frame->updateUnitBonus(s->unitId(),frame->makeOwnedBonusEvent({change}));packet(s,{change},true);same(*frame->getForUpdate(s->unitId()),*s,selected());EXPECT_EQ(frame->getForUpdate(s->unitId())->getAllBonuses(selected())->front()->turnsRemain,2);
 change.source=BonusSource::OTHER;frame->updateUnitBonus(s->unitId(),frame->makeOwnedBonusEvent({change}));packet(s,{change},true);same(*frame->getForUpdate(s->unitId()),*s,Selector::all);
}
TEST_F(OwnedTypedUnitEffectsTest, MissCopiesEveryScalarAndForcedAddRetainsDuplicates)
{
 auto*s=add();auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto b=effect();b.hidden=true;b.bonusOwner=PlayerColor(1);b.stacking="typed-copy";b.targetSourceType=BonusSource::ARTIFACT;b.targetSourceID=ArtifactID(4);
 frame->updateUnitBonus(s->unitId(),frame->makeOwnedBonusEvent({b}));packet(s,{b},true);same(*frame->getForUpdate(s->unitId()),*s,selected());
 b.stacking="";auto event=frame->makeOwnedBonusEvent({b,b});frame->addUnitBonus(s->unitId(),event);packet(s,{b,b},false);same(*frame->getForUpdate(s->unitId()),*s,selected());
}
TEST_F(OwnedTypedUnitEffectsTest, ForkEventsAndCommittedPrefixHaveExplicitBoundaries)
{
 auto*s=add();auto parent=HypotheticBattle::captureOwnedFixture(*battle());auto child=parent->forkOwnedFixture(),sibling=parent->forkOwnedFixture();auto b=effect();auto ancestor=parent->makeOwnedBonusEvent({b});child->addUnitBonus(s->unitId(),ancestor);EXPECT_TRUE(parent->getForUpdate(s->unitId())->getAllBonuses(selected())->empty());
 auto own=child->makeOwnedBonusEvent({b});
 EXPECT_THROW(sibling->addUnitBonus(s->unitId(),own),std::invalid_argument);
 EXPECT_THROW(parent->addUnitBonus(s->unitId(),own),std::invalid_argument);
 auto bad=b;bad.turnsRemain=0;auto prefix=parent->makeOwnedBonusEvent({b,bad});
 EXPECT_THROW(parent->addUnitBonus(s->unitId(),prefix),std::invalid_argument);packet(s,{b},false);same(*parent->getForUpdate(s->unitId()),*s,selected()); // Native invalid timed add asserts; only completed valid prefix has native oracle.
 auto revision=parent->getForUpdate(s->unitId())->getTreeVersion();EXPECT_NO_THROW(parent->addUnitBonus(99999,own));EXPECT_EQ(parent->getForUpdate(s->unitId())->getTreeVersion(),revision);
}
TEST_F(OwnedTypedUnitEffectsTest, DefinitionSharedValueUpdatesSourceViewNotChildAndSurvivesFrame)
{
 auto*s=add();auto creature=LIBRARY->creh->objects.at(s->creatureId().getNum());auto shared=std::make_shared<Bonus>(effect());creature->addNewBonus(shared);struct Cleanup {std::shared_ptr<CCreature> c;std::shared_ptr<Bonus>b;~Cleanup(){c->removeBonus(b);}} cleanup{creature,shared};s->addNewBonus(shared);
 auto parent=HypotheticBattle::captureOwnedFixture(*battle());std::weak_ptr<StackWithBonuses> sourceLifetime=parent->getForUpdate(s->unitId());auto child=parent->forkOwnedFixture();auto detached=parent->getForUpdate(s->unitId())->acquireState();auto descriptor=detached->unitType();auto longDuration=selected().And([](const Bonus*b){return b->turnsRemain>=5;});EXPECT_EQ(descriptor->getBonusBearer()->valOfBonuses(longDuration),0);
 BonusValueCache cache(descriptor->getBonusBearer(),longDuration);BonusValueCache childCache(child->getForUpdate(s->unitId())->unitType()->getBonusBearer(),longDuration);EXPECT_EQ(cache.getValue(),0);EXPECT_EQ(childCache.getValue(),0);
 const auto*ownedDescriptor=dynamic_cast<const OwnedCreature*>(descriptor);ASSERT_NE(ownedDescriptor,nullptr);auto retainedDescriptor=ownedDescriptor->retain();
 auto b=effect();b.turnsRemain=8;auto invalid=b;invalid.sid=SpellID(SpellID::SLOW);invalid.turnsRemain=0;auto beforeUnit=parent->getForUpdate(s->unitId())->getTreeVersion();auto beforeDefinition=descriptor->getBonusBearer()->getTreeVersion();
 EXPECT_THROW(parent->updateUnitBonus(s->unitId(),parent->makeOwnedBonusEvent({invalid})),std::invalid_argument);
 EXPECT_EQ(parent->getForUpdate(s->unitId())->getTreeVersion(),beforeUnit);EXPECT_EQ(descriptor->getBonusBearer()->getTreeVersion(),beforeDefinition);EXPECT_EQ(descriptor->getBonusBearer()->getAllBonuses(selected())->front()->turnsRemain,2);
 EXPECT_THROW(parent->updateUnitBonus(s->unitId(),parent->makeOwnedBonusEvent({b,invalid})),std::invalid_argument);packet(s,{b},true);same(*descriptor->getBonusBearer(),*creature,selected());EXPECT_EQ(descriptor->getBonusBearer()->valOfBonuses(longDuration),7);EXPECT_EQ(cache.getValue(),7);EXPECT_EQ(childCache.getValue(),0);EXPECT_EQ(child->getForUpdate(s->unitId())->unitType()->getBonusBearer()->getAllBonuses(selected())->front()->turnsRemain,2);
 EXPECT_EQ(detached->getAllBonuses(selected())->front()->turnsRemain,2); // Unit acquisition stays frozen; its creature definition follows its source frame.
 parent.reset();EXPECT_FALSE(sourceLifetime.expired());EXPECT_EQ(descriptor->getBonusBearer()->valOfBonuses(longDuration),7);auto grandchild=detached->acquireState();detached.reset();EXPECT_EQ(grandchild->unitType()->getBonusBearer()->valOfBonuses(longDuration),7);
 grandchild.reset();EXPECT_TRUE(sourceLifetime.expired());EXPECT_EQ(retainedDescriptor->getBonusBearer()->valOfBonuses(longDuration),7);
}
TEST_F(OwnedTypedUnitEffectsTest, EqualDistinctDefinitionExportRemainsUnchanged)
{
 auto*s=add();auto creature=LIBRARY->creh->objects.at(s->creatureId().getNum());auto definition=std::make_shared<Bonus>(effect());creature->addNewBonus(definition);struct Cleanup {std::shared_ptr<CCreature> c;std::shared_ptr<Bonus>b;~Cleanup(){c->removeBonus(b);}} cleanup{creature,definition};s->addNewBonus(std::make_shared<Bonus>(*definition));
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto b=effect();b.turnsRemain=8;frame->updateUnitBonus(s->unitId(),frame->makeOwnedBonusEvent({b}));packet(s,{b},true);same(*frame->getForUpdate(s->unitId())->unitType()->getBonusBearer(),*creature,selected());EXPECT_EQ(definition->turnsRemain,2);
}

TEST_F(OwnedTypedUnitEffectsTest, AncestorPayloadResolvesCurrentNestedValueAfterDurationEdit)
{
 auto*s=add();auto b=effect();packet(s,{b},false);auto live=s->getExportedBonusList().getFirst(selected());
 Bonus root(BonusDuration::PERMANENT,BonusType::FLYING,BonusSource::OTHER,1,ArtifactID(721));BonusParametersOnCombatEvent params;params.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{live,false});root.parameters=std::make_shared<BonusParameters>(params);packet(s,{root},false);
 auto parent=HypotheticBattle::captureOwnedFixture(*battle());auto payload=parent->ownedEventFromUnitExports(s->unitId(),Selector::source(BonusSource::OTHER,ArtifactID(721)));auto child=parent->forkOwnedFixture();auto change=b;change.turnsRemain=8;child->updateUnitBonus(s->unitId(),parent->makeOwnedBonusEvent({change}));child->addUnitBonus(s->unitId(),payload);packet(s,{change},true);packet(s,{root},false);
 auto roots=child->getForUpdate(s->unitId())->getAllBonuses(Selector::source(BonusSource::OTHER,ArtifactID(721)));ASSERT_EQ(roots->size(),2);auto first=std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(roots->front()->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;auto second=std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(roots->back()->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;EXPECT_EQ(first,second);EXPECT_EQ(first->turnsRemain,8);EXPECT_EQ(roots->front()->parameters,roots->back()->parameters);EXPECT_EQ(parent->getForUpdate(s->unitId())->getAllBonuses(selected())->front()->turnsRemain,2);same(*child->getForUpdate(s->unitId()),*s,selected());
}

TEST_F(OwnedTypedUnitEffectsTest, TypedAdditionMaterializesCurrentCountWithoutReplayingHistory)
{
 auto*s=add();Bonus b(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,7,ArtifactID(722));b.propagator=std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);b.propagationUpdater=std::make_shared<TimesStackSizeUpdater>(std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),1,3);packet(s,{b},false);auto select=Selector::source(BonusSource::OTHER,ArtifactID(722));ASSERT_EQ(s->getAllBonuses(select)->front()->val,30);int64_t damage=s->getMaxHealth();s->damage(damage);ASSERT_EQ(s->getCount(),9);
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto event=frame->ownedEventFromUnitExports(s->unitId(),select);frame->addUnitBonus(s->unitId(),event);packet(s,{b},false);auto owned=frame->getForUpdate(s->unitId())->getAllBonuses(select),native=s->getAllBonuses(select);ASSERT_EQ(owned->size(),2);ASSERT_EQ(native->size(),2);std::vector<int> ov,nv;for(const auto&v:*owned)ov.push_back(v->val);for(const auto&v:*native)nv.push_back(v->val);EXPECT_EQ(ov,nv);EXPECT_NE(std::find(ov.begin(),ov.end(),30),ov.end());EXPECT_NE(std::find(ov.begin(),ov.end(),27),ov.end());EXPECT_EQ(owned->front()->propagationUpdater,owned->back()->propagationUpdater);EXPECT_NE(owned->front()->propagationUpdater,native->front()->propagationUpdater);
}

TEST_F(OwnedTypedUnitEffectsTest, ExtensionIgnoresReplacementComponentsFromCertifiedCapturedExport)
{
 auto*s=add();auto b=effect();packet(s,{b},false);auto*origin=addStack(BattleSide::ATTACKER,creatureByName("core:woodElf"),BattleHex(7,5),10);auto replacement=b;replacement.turnsRemain=8;replacement.val=99;replacement.updater=std::make_shared<TimesStackSizeUpdater>();auto limiter=std::make_shared<CCreatureTypeLimiter>();limiter->setCreature(creatureByName("core:greenDragon"));replacement.limiter=limiter;origin->addNewBonus(std::make_shared<Bonus>(replacement));
 auto frame=HypotheticBattle::captureOwnedFixture(*battle());auto event=frame->ownedEventFromUnitExports(origin->unitId(),selected());frame->updateUnitBonus(s->unitId(),event);packet(s,{replacement},true);auto result=frame->getForUpdate(s->unitId())->getAllBonuses(selected());ASSERT_EQ(result->size(),1);EXPECT_EQ(result->front()->turnsRemain,8);EXPECT_EQ(result->front()->val,7);EXPECT_EQ(result->front()->updater,nullptr);EXPECT_EQ(result->front()->limiter,nullptr);same(*frame->getForUpdate(s->unitId()),*s,selected());
}

TEST_F(OwnedTypedUnitEffectsTest, DefinitionObserverAndNativeProxyKeepSeparateVersionsAndRequestCaches)
{
 auto *s=add();
 auto creature=LIBRARY->creh->objects.at(s->creatureId().getNum());
 auto shared=std::make_shared<Bonus>(effect());
 creature->addNewBonus(shared);
 struct Cleanup {std::shared_ptr<CCreature> c;std::shared_ptr<Bonus>b;~Cleanup(){c->removeBonus(b);}} cleanup{creature,shared};
 s->addNewBonus(shared);
 auto longDuration=selected().And([](const Bonus*b){return b->turnsRemain>=5;});
 const std::string key="definition-shared-duration";
 BonusValueCache nativeCache(creature.get(),longDuration);
 EXPECT_EQ(nativeCache.getValue(),0);
 EXPECT_TRUE(creature->getAllBonuses(longDuration,key)->empty());
 const auto nativeEpoch=creature->getTreeVersion();

 auto parent=HypotheticBattle::captureOwnedFixture(*battle());
 std::weak_ptr<StackWithBonuses> sourceLifetime=parent->getForUpdate(s->unitId());
 auto child=parent->forkOwnedFixture();
 auto detached=parent->getForUpdate(s->unitId())->acquireState();
 const auto *definition=dynamic_cast<const OwnedCreature*>(detached->unitType());
 ASSERT_NE(definition,nullptr);
 auto retained=definition->retain();
 auto operational=definition->retainNativeBonusBearer();
 const auto *childDefinition=dynamic_cast<const OwnedCreature*>(child->getForUpdate(s->unitId())->unitType());
 ASSERT_NE(childDefinition,nullptr);
 auto childOperational=childDefinition->retainNativeBonusBearer();
 const auto *fresh=retained->getBonusBearer();
 BonusValueCache freshCache(fresh,longDuration),operationalCache(operational.get(),longDuration);
 BonusValueCache childFreshCache(childDefinition->getBonusBearer(),longDuration),childOperationalCache(childOperational.get(),longDuration);
 EXPECT_EQ(freshCache.getValue(),0);EXPECT_EQ(operationalCache.getValue(),0);
 EXPECT_EQ(childFreshCache.getValue(),0);EXPECT_EQ(childOperationalCache.getValue(),0);
 EXPECT_TRUE(fresh->getAllBonuses(longDuration,key)->empty());
 EXPECT_EQ(fresh->getAllBonuses(selected(),key)->totalValue(),7); // Fresh named reads do not cache membership.
 EXPECT_TRUE(operational->getAllBonuses(selected(),key)->empty()); // Native captured key wins over selector.
 EXPECT_EQ(operational->getTreeVersion(),nativeEpoch);
 // A fresh-only key must not appear in the operational request bank.
 EXPECT_TRUE(fresh->getAllBonuses(longDuration,"fresh-only")->empty());
 EXPECT_EQ(operational->getAllBonuses(selected(),"fresh-only")->totalValue(),7);

 const auto freshVersion=fresh->getTreeVersion();
 const auto unitVersion=parent->getForUpdate(s->unitId())->getTreeVersion();
 auto invalid=effect();invalid.sid=SpellID(SpellID::SLOW);invalid.turnsRemain=0;
 EXPECT_THROW(parent->updateUnitBonus(s->unitId(),parent->makeOwnedBonusEvent({invalid})),std::invalid_argument);
 EXPECT_EQ(fresh->getTreeVersion(),freshVersion);
 EXPECT_EQ(operational->getTreeVersion(),nativeEpoch);
 EXPECT_EQ(parent->getForUpdate(s->unitId())->getTreeVersion(),unitVersion);
 EXPECT_EQ(freshCache.getValue(),0);EXPECT_EQ(operationalCache.getValue(),0);
 auto change=effect();change.turnsRemain=8;
 parent->updateUnitBonus(s->unitId(),parent->makeOwnedBonusEvent({change}));packet(s,{change},true);
 EXPECT_NE(fresh->getTreeVersion(),freshVersion);
 EXPECT_NE(parent->getForUpdate(s->unitId())->getTreeVersion(),unitVersion);
 EXPECT_EQ(creature->getTreeVersion(),nativeEpoch);
 EXPECT_EQ(operational->getTreeVersion(),nativeEpoch);
 EXPECT_EQ(freshCache.getValue(),7);EXPECT_EQ(operationalCache.getValue(),0);EXPECT_EQ(nativeCache.getValue(),0);
 EXPECT_EQ(fresh->getAllBonuses(longDuration,key)->totalValue(),7);
 EXPECT_TRUE(operational->getAllBonuses(longDuration,key)->empty());
 EXPECT_TRUE(creature->getAllBonuses(longDuration,key)->empty());
 EXPECT_EQ(operational->valOfBonuses(longDuration),creature->valOfBonuses(longDuration));
 EXPECT_EQ(operational->valOfBonuses(longDuration),7); // Shared raw value changed, native node epoch did not.
 EXPECT_EQ(childFreshCache.getValue(),0);EXPECT_EQ(childOperationalCache.getValue(),0);
 EXPECT_EQ(childOperational->getAllBonuses(selected())->front()->turnsRemain,2);
 EXPECT_EQ(detached->getAllBonuses(selected())->front()->turnsRemain,2);
 parent.reset();EXPECT_FALSE(sourceLifetime.expired());
 EXPECT_EQ(freshCache.getValue(),7);EXPECT_EQ(operationalCache.getValue(),0);
 auto grandchild=detached->acquireState();detached.reset();
 EXPECT_EQ(grandchild->unitType()->getBonusBearer()->valOfBonuses(longDuration),7);
 grandchild.reset();EXPECT_TRUE(sourceLifetime.expired());
 EXPECT_EQ(freshCache.getValue(),7);EXPECT_EQ(operationalCache.getValue(),0);
 EXPECT_EQ(operational->getTreeVersion(),nativeEpoch);
 EXPECT_TRUE(operational->getAllBonuses(longDuration,key)->empty());
 EXPECT_EQ(fresh->getAllBonuses(longDuration,key)->totalValue(),7);
}
