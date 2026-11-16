#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/OwnedStackEffect.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Limiters.h"

class OwnedTypedRemovalTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static Bonus effect()
 {
  Bonus value(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, SpellID(SpellID::HASTE));
  value.turnsRemain = 2;
  return value;
 }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 void addNative(CStack * unit, const std::vector<Bonus> & values)
 {
  SetStackEffect packet; packet.battleID = BattleID(0); packet.toAdd.emplace_back(unit->unitId(), values); gameHandler->sendAndApply(packet);
 }
 void removeNative(CStack * unit, const std::vector<Bonus> & values)
 {
  SetStackEffect packet; packet.battleID = BattleID(0); packet.toRemove.emplace_back(unit->unitId(), values); gameHandler->sendAndApply(packet);
 }
 static void same(const IBonusBearer & owned, const IBonusBearer & native, const CSelector & selector = Selector::all)
 {
  auto a = owned.getAllBonuses(selector), b = native.getAllBonuses(selector);
  ASSERT_EQ(a->size(), b->size());
  for(size_t i = 0; i < a->size(); ++i)
  {
   EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode());
   EXPECT_EQ((*a)[i]->bonusOwner, (*b)[i]->bonusOwner);
  }
 }
};

TEST_F(OwnedTypedRemovalTest, ExactNativeFieldsDistinguishRemovalKeys)
{
 auto * native = add(); const auto base = effect(); addNative(native, {base});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto unit = frame->getForUpdate(native->unitId());
 std::vector<Bonus> differences(8, base);
 differences[0].duration = BonusDuration::PERMANENT;
 differences[1].type = BonusType::FLYING;
 differences[2].subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
 differences[3].source = BonusSource::OTHER;
 differences[4].val = 8;
 differences[5].sid = SpellID(SpellID::SLOW);
 differences[6].valType = BonusValueType::PERCENT_TO_ALL;
 differences[7].effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
 const auto revision = unit->getTreeVersion();
 for(const auto & value : differences)
 {
  frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({value}));
  removeNative(native, {value}); same(*unit, *native); EXPECT_EQ(unit->getAllBonuses(selected())->size(), 1);
 }
 EXPECT_EQ(unit->getTreeVersion(), revision);
 frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({base})); removeNative(native, {base});
 EXPECT_TRUE(unit->getAllBonuses(selected())->empty()); same(*unit, *native);
}

TEST_F(OwnedTypedRemovalTest, RemovalIgnoresDurationRemainderAndOtherNonKeyFields)
{
 auto * native = add(); auto base = effect(); base.updater = std::make_shared<TimesStackSizeUpdater>();
 base.stacking = "remove-owned"; base.hidden = true; base.bonusOwner = PlayerColor(1);
 addNative(native, {base, base});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 auto key = effect(); key.turnsRemain = 99; key.targetSourceType = BonusSource::ARTIFACT; key.targetSourceID = ArtifactID(4);
 frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({key})); removeNative(native, {key});
 EXPECT_TRUE(frame->getForUpdate(native->unitId())->getAllBonuses(selected())->empty()); same(*frame->getForUpdate(native->unitId()), *native);
}

TEST_F(OwnedTypedRemovalTest, RawExportsAreSelectedEvenWhenLimiterHidesThem)
{
 auto * native = add(); auto * eligible = addStack(BattleSide::ATTACKER, creatureByName("core:greenDragon"), BattleHex(10, 5), 10);
 auto base = effect(); auto limiter = std::make_shared<CCreatureTypeLimiter>();
 limiter->setCreature(creatureByName("core:greenDragon")); base.limiter = limiter; addNative(native, {base});
 ASSERT_TRUE(native->getAllBonuses(selected())->empty()); ASSERT_NE(native->getExportedBonusList().getFirst(selected()), nullptr);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto key = effect();
 frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({key})); removeNative(native, {key});
 EXPECT_EQ(native->getExportedBonusList().getFirst(selected()), nullptr);
 auto remaining = frame->ownedEventFromUnitExports(native->unitId(), selected());
 frame->addUnitBonus(eligible->unitId(), remaining);
 EXPECT_TRUE(frame->getForUpdate(eligible->unitId())->getAllBonuses(selected())->empty());
 same(*frame->getForUpdate(eligible->unitId()), *eligible);
 // A removed hidden export must not reappear when a typed copy is added.
 auto fresh = effect(); frame->addUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({fresh})); addNative(native, {fresh});
 EXPECT_EQ(frame->getForUpdate(native->unitId())->getAllBonuses(selected())->size(), 1); same(*frame->getForUpdate(native->unitId()), *native);
}

TEST_F(OwnedTypedRemovalTest, InheritedMatchesRemainAndNoopKeepsRevision)
{
 auto * native = add(); auto value = effect(); attackerSideHero->addNewBonus(std::make_shared<Bonus>(value));
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto unit = frame->getForUpdate(native->unitId()); auto revision = unit->getTreeVersion();
 frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({value})); removeNative(native, {value});
 EXPECT_EQ(unit->getTreeVersion(), revision); EXPECT_EQ(unit->getAllBonuses(selected())->size(), 1); same(*unit, *native);
 auto child = frame->forkOwnedFixture(); auto foreign = child->makeOwnedBonusEvent({value});
 EXPECT_NO_THROW(frame->removeUnitBonus(99999, foreign)); EXPECT_EQ(unit->getTreeVersion(), revision);
 EXPECT_THROW(frame->removeUnitBonus(native->unitId(), foreign), std::invalid_argument); EXPECT_EQ(unit->getTreeVersion(), revision);
 frame->removeUnitBonus(native->unitId(), frame->makeOwnedBonusEvent({})); EXPECT_EQ(unit->getTreeVersion(), revision);
}

TEST_F(OwnedTypedRemovalTest, RecursiveTraversalRemovesDescendantLocalExports)
{
 auto * parent = add(5); auto * child = add(7); child->attachTo(*parent);
 struct Detach { CStack * child; CStack * parent; ~Detach() { child->detachFrom(*parent); } } detach{child, parent};
 auto value = effect(); addNative(parent, {value}); addNative(child, {value});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 frame->removeUnitBonus(parent->unitId(), frame->makeOwnedBonusEvent({value})); removeNative(parent, {value});
 EXPECT_TRUE(frame->getForUpdate(parent->unitId())->getAllBonuses(selected())->empty());
 EXPECT_TRUE(frame->getForUpdate(child->unitId())->getAllBonuses(selected())->empty());
 same(*frame->getForUpdate(parent->unitId()), *parent); same(*frame->getForUpdate(child->unitId()), *child);
}

TEST_F(OwnedTypedRemovalTest, PropagatedRemovalWithdrawsHistoricalMaterialization)
{
 auto * source = add(5); auto * recipient = add(7); auto value = effect(); value.source = BonusSource::OTHER;
 value.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 value.propagationUpdater = std::make_shared<TimesStackSizeUpdater>(); addNative(source, {value});
 const auto select = Selector::source(BonusSource::OTHER, value.sid);
 ASSERT_FALSE(recipient->getAllBonuses(select)->empty()); int64_t damage = source->getMaxHealth(); source->damage(damage);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto child = frame->forkOwnedFixture();
 auto key = effect(); key.source = BonusSource::OTHER;
 child->removeUnitBonus(source->unitId(), frame->makeOwnedBonusEvent({key})); removeNative(source, {key});
 EXPECT_TRUE(child->getForUpdate(recipient->unitId())->getAllBonuses(select)->empty());
 EXPECT_FALSE(frame->getForUpdate(recipient->unitId())->getAllBonuses(select)->empty());
 same(*child->getForUpdate(source->unitId()), *source); same(*child->getForUpdate(recipient->unitId()), *recipient);
}

TEST_F(OwnedTypedRemovalTest, RemovedNestedValueSurvivesPacketAndForkLifetime)
{
 auto * native = add(); auto value = effect(); addNative(native, {value}); auto leaf = native->getExportedBonusList().getFirst(selected());
 Bonus root(BonusDuration::PERMANENT, BonusType::FLYING, BonusSource::OTHER, 1, ArtifactID(ArtifactID::SPELLBOOK));
 BonusParametersOnCombatEvent parameters; parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{leaf, false});
 root.parameters = std::make_shared<BonusParameters>(parameters); addNative(native, {root});
 const auto roots = Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK));
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto child = parent->forkOwnedFixture();
 auto captured = parent->ownedEventFromUnitExports(native->unitId(), roots);
 OwnedStackEffect packet; packet.toRemove.emplace_back(native->unitId(), parent->makeOwnedBonusEvent({value})); packet.toAdd.emplace_back(native->unitId(), captured);
 child->applyOwnedStackEffect(packet);
 SetStackEffect nativePacket; nativePacket.battleID = BattleID(0); nativePacket.toRemove.emplace_back(native->unitId(), std::vector<Bonus>{value}); nativePacket.toAdd.emplace_back(native->unitId(), std::vector<Bonus>{root}); gameHandler->sendAndApply(nativePacket);
 same(*child->getForUpdate(native->unitId()), *native); EXPECT_FALSE(parent->getForUpdate(native->unitId())->getAllBonuses(selected())->empty());
 parent.reset(); auto values = child->getForUpdate(native->unitId())->getAllBonuses(roots); ASSERT_EQ(values->size(), 2);
 auto first = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(values->front()->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;
 auto second = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(values->back()->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;
 EXPECT_EQ(first, second); EXPECT_EQ(first->turnsRemain, 2); EXPECT_NE(first.get(), leaf.get());
}

TEST_F(OwnedTypedRemovalTest, PacketPreservesPhaseRecipientAndItemOrder)
{
 auto * first = add(5); auto * second = add(7); auto value = effect(); addNative(first, {value});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto initial = value; initial.val = 11; initial.turnsRemain = 3;
 auto extension = value; extension.val = 55; extension.turnsRemain = 9; auto forced = value; forced.val = 23;
 OwnedStackEffect owned; owned.toAdd.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({forced}));
 owned.toUpdate.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({initial, extension}));
 owned.toUpdate.emplace_back(second->unitId(), frame->makeOwnedBonusEvent({extension, initial}));
 owned.toRemove.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({value}));
 frame->applyOwnedStackEffect(owned);
 SetStackEffect native; native.battleID = BattleID(0); native.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{forced}); native.toUpdate.emplace_back(first->unitId(), std::vector<Bonus>{initial, extension}); native.toUpdate.emplace_back(second->unitId(), std::vector<Bonus>{extension, initial}); native.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{value}); gameHandler->sendAndApply(native);
 same(*frame->getForUpdate(first->unitId()), *first); same(*frame->getForUpdate(second->unitId()), *second);
 auto a = frame->getForUpdate(first->unitId())->getAllBonuses(selected()); ASSERT_EQ(a->size(), 2);
 EXPECT_EQ(std::count_if(a->begin(), a->end(), [](const auto & bonus) { return bonus->val == 11 && bonus->turnsRemain == 9; }), 1);
 EXPECT_EQ(std::count_if(a->begin(), a->end(), [](const auto & bonus) { return bonus->val == 23 && bonus->turnsRemain == 2; }), 1);
 EXPECT_EQ(frame->getForUpdate(second->unitId())->getAllBonuses(selected())->front()->val, 55);
}

TEST_F(OwnedTypedRemovalTest, PacketFailureRetainsCompletedPhasesAndRecipientPrefix)
{
 auto * first = add(5); auto * second = add(7); auto value = effect(); addNative(first, {value});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto update = value; update.val = 11;
 auto invalid = value; invalid.sid = SpellID(SpellID::SLOW); invalid.turnsRemain = 0;
 OwnedStackEffect owned; owned.toRemove.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({value}));
 owned.toUpdate.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({update}));
 owned.toAdd.emplace_back(first->unitId(), frame->makeOwnedBonusEvent({value, invalid}));
 owned.toAdd.emplace_back(second->unitId(), frame->makeOwnedBonusEvent({value}));
 EXPECT_THROW(frame->applyOwnedStackEffect(owned), std::invalid_argument);
 SetStackEffect native; native.battleID = BattleID(0); native.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{value}); native.toUpdate.emplace_back(first->unitId(), std::vector<Bonus>{update}); native.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{value}); gameHandler->sendAndApply(native);
 same(*frame->getForUpdate(first->unitId()), *first); same(*frame->getForUpdate(second->unitId()), *second);
 EXPECT_TRUE(frame->getForUpdate(second->unitId())->getAllBonuses(selected())->empty());
}

TEST_F(OwnedTypedRemovalTest, SharedDescendantTraversalReadsMembershipAfterFirstVisit)
{
 auto * root = add(3); auto * left = add(5); auto * right = add(7); auto * leaf = add(9);
 left->attachTo(*root); right->attachTo(*root); leaf->attachTo(*left); leaf->attachTo(*right);
 struct Detach
 {
  CStack * root; CStack * left; CStack * right; CStack * leaf;
  ~Detach() { leaf->detachFrom(*right); leaf->detachFrom(*left); right->detachFrom(*root); left->detachFrom(*root); }
 } detach{root, left, right, leaf};
 auto value = effect(); for(auto * unit : {root, left, right, leaf}) addNative(unit, {value});
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 EXPECT_NO_THROW(frame->removeUnitBonus(root->unitId(), frame->makeOwnedBonusEvent({value})));
 removeNative(root, {value});
 for(auto * unit : {root, left, right, leaf})
 {
  EXPECT_TRUE(frame->getForUpdate(unit->unitId())->getAllBonuses(selected())->empty());
  same(*frame->getForUpdate(unit->unitId()), *unit);
 }
}

TEST_F(OwnedTypedRemovalTest, SharedPropagationUpdaterWithdrawalMatchesNativeCollision)
{
 auto * source = add(5); auto * recipient = add(7); auto first = effect(); first.source = BonusSource::OTHER;
 first.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
 first.propagationUpdater = std::make_shared<TimesStackSizeUpdater>();
 auto second = first; second.sid = SpellID(SpellID::SLOW); addNative(source, {first, second});
 const auto select = Selector::sourceType()(BonusSource::OTHER).And(Selector::type()(BonusType::STACKS_SPEED));
 ASSERT_EQ(recipient->getAllBonuses(select)->size(), 2);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto key = effect(); key.source = BonusSource::OTHER;
 frame->removeUnitBonus(source->unitId(), frame->makeOwnedBonusEvent({key})); removeNative(source, {key});
 // Native withdrawal groups materializations by shared updater identity.
 ASSERT_TRUE(recipient->getAllBonuses(select)->empty());
 EXPECT_TRUE(frame->getForUpdate(recipient->unitId())->getAllBonuses(select)->empty());
 same(*frame->getForUpdate(source->unitId()), *source); same(*frame->getForUpdate(recipient->unitId()), *recipient);
 ASSERT_NE(source->getExportedBonusList().getFirst(Selector::source(BonusSource::OTHER, second.sid)), nullptr);
 auto remaining = frame->ownedEventFromUnitExports(source->unitId(), Selector::source(BonusSource::OTHER, second.sid));
 frame->addUnitBonus(source->unitId(), remaining); addNative(source, {second});
 same(*frame->getForUpdate(recipient->unitId()), *recipient);
}

TEST_F(OwnedTypedRemovalTest, SurvivingAliasDurationEditRebindsRemovedNestedValue)
{
 auto * source = add(5); auto * survivor = add(7); auto value = effect(); addNative(source, {value});
 auto alias = source->getExportedBonusList().getFirst(selected()); survivor->addNewBonus(alias);
 Bonus root(BonusDuration::PERMANENT, BonusType::FLYING, BonusSource::OTHER, 1, ArtifactID(ArtifactID::SPELLBOOK));
 BonusParametersOnCombatEvent parameters; parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{alias, false});
 root.parameters = std::make_shared<BonusParameters>(parameters); addNative(source, {root});
 const auto roots = Selector::source(BonusSource::OTHER, ArtifactID(ArtifactID::SPELLBOOK));
 auto parent = HypotheticBattle::captureOwnedFixture(*battle()); auto child = parent->forkOwnedFixture();
 auto event = parent->ownedEventFromUnitExports(source->unitId(), roots);
 child->removeUnitBonus(source->unitId(), parent->makeOwnedBonusEvent({value}));
 auto update = value; update.turnsRemain = 9; child->updateUnitBonus(survivor->unitId(), parent->makeOwnedBonusEvent({update}));
 child->addUnitBonus(source->unitId(), event); child->addUnitBonus(survivor->unitId(), event);
 SetStackEffect native; native.battleID = BattleID(0); native.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{value}); native.toUpdate.emplace_back(survivor->unitId(), std::vector<Bonus>{update}); native.toAdd.emplace_back(source->unitId(), std::vector<Bonus>{root}); native.toAdd.emplace_back(survivor->unitId(), std::vector<Bonus>{root}); gameHandler->sendAndApply(native);
 same(*child->getForUpdate(source->unitId()), *source); same(*child->getForUpdate(survivor->unitId()), *survivor);
 auto values = child->getForUpdate(source->unitId())->getAllBonuses(roots); ASSERT_EQ(values->size(), 2);
 auto nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(values->back()->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;
 EXPECT_EQ(nested->turnsRemain, 9);
 // Each query owns a separate value graph; alias identity is checked within one query.
 const auto survivorValues = child->getForUpdate(survivor->unitId())->getAllBonuses(Selector::all);
 const auto survivorLeaf = survivorValues->getFirst(selected());
 const auto survivorRoot = survivorValues->getFirst(roots);
 ASSERT_NE(survivorLeaf, nullptr); ASSERT_NE(survivorRoot, nullptr);
 const auto survivorNested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(survivorRoot->parameters->toCustom<BonusParametersOnCombatEvent>().effects[0]).bonus;
 EXPECT_EQ(survivorNested, survivorLeaf);
 EXPECT_EQ(nested->toJsonNode(), survivorLeaf->toJsonNode());
 EXPECT_EQ(parent->getForUpdate(survivor->unitId())->getAllBonuses(selected())->front()->turnsRemain, 2);
}
