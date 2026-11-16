// GPL-2.0-or-later. Source-order resistance ranking, ordinary native damage only.
#include "../../CI/oracles/fheroes2_damage_resistance.h"
#include "../../lib/bonuses/BonusParameters.h"
struct FHeroes2ResistanceSpellCase
{
 SpellID spell;
 const char * name;
 int power;
 int adjustedRaw;
 int reduction;
};
class FHeroes2NativeResistanceBattle : public AIIntegrationNativeBattle
{
protected:
 enum class Case { OVERKILL, ZERO_WAKE, FAREWELL, ZERO, FULL, MODIFIED, MODIFIED_ZERO, EVENT, EVENT_ZERO };
 void exerciseResistance(Case scenario, const FHeroes2ResistanceSpellCase & config =
  FHeroes2ResistanceSpellCase{SpellID(SpellID::MAGIC_ARROW), "MagicArrow", 8, 90, 0})
 {
  startRecordedGame();
  ASSERT_FALSE(HasFatalFailure());
  setArmy(attackerSideHero, CreatureID(2), 1);
  setArmy(defenderSideHero, CreatureID(0), 4);
  ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(defenderSideHero->id, SlotID(1)), CreatureID(0).toCreature(), 3));
  gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
  ASSERT_EQ(gameState()->currentBattles.size(), 1u);
  battle()->obstacles.clear();
  const auto * active = dynamic_cast<const CStack *>(battle()->battleActiveUnit());
  ASSERT_NE(active, nullptr);
  ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
  CStack * resistant = nullptr;
  CStack * susceptible = nullptr;
  for(const auto & unit : battle()->stacks)
   if(unit->unitSide() == BattleSide::DEFENDER && unit->creatureId() == CreatureID(0))
   {
    if(unit->getCount() == 4) resistant = unit.get();
    if(unit->getCount() == 3) susceptible = unit.get();
   }
  ASSERT_NE(resistant, nullptr);
  ASSERT_NE(susceptible, nullptr);
  const int resistance = (scenario == Case::ZERO || scenario == Case::MODIFIED_ZERO || scenario == Case::EVENT_ZERO) ? 0 : scenario == Case::FULL ? 100
   : scenario == Case::ZERO_WAKE ? 99 : scenario == Case::FAREWELL ? 90 : 50;
  resistant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
   BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, resistance, BonusSourceID()));
  if(scenario == Case::MODIFIED || scenario == Case::EVENT)
   susceptible->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
    BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 50, BonusSourceID()));
  if(scenario == Case::EVENT || scenario == Case::EVENT_ZERO)
  {
   BonusParametersOnCombatEvent::CombatEffectBonus effect;
   effect.bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
    BonusSource::OTHER, 1, BonusSourceID());
   effect.targetEnemy = false;
   BonusParametersOnCombatEvent reaction;
   reaction.effects.emplace_back(effect);
   auto trigger = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ON_COMBAT_EVENT,
    BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype(static_cast<int>(CombatEventType::SPELL_HIT))));
   trigger->parameters = std::make_shared<BonusParameters>(reaction);
   resistant->addNewBonus(trigger);
  }
  if(scenario == Case::ZERO_WAKE)
   resistant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
    BonusType::NOT_ACTIVE, BonusSource::OTHER, 1, BonusSourceID()));
  for(const auto & spell : LIBRARY->spellh->objects)
   attackerSideHero->removeSpellFromSpellbook(spell->id);
  if(config.reduction)
   for(auto * unit : {resistant, susceptible})
    unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
     BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, config.reduction,
     BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
  teachBattleSpell(attackerSideHero, config.spell, config.power);
  setMagicMastery(attackerSideHero, MasteryLevel::BASIC);
  const auto battleID = battle()->getBattleID();
  const auto player = battle()->sideToPlayer(BattleSide::ATTACKER);
  auto callback = std::make_shared<DecisionCallback>(player);
  callback->onBattleStarted(battle());
  const auto view = callback->getBattle(battleID);
  auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
  const auto * spell = config.spell.toSpell();
  spells::BattleCast legality(view.get(), attackerSideHero, spells::Mode::HERO, spell);
  const auto mechanics = spell->battleMechanics(&legality);
  const auto raw = mechanics->adjustEffectValue(resistant);
  ASSERT_EQ(raw, config.adjustedRaw);
  ASSERT_EQ(mechanics->adjustEffectValue(susceptible), config.adjustedRaw);
  ASSERT_GT(raw, 0);
  ASSERT_LT(raw, 100); // r99 must truncate to zero, not fractional damage.
  ASSERT_GE(raw / 2, resistant->getAvailableHealth());
  ASSERT_LT(raw / 10, resistant->getMaxHealth());
  EXPECT_EQ(resistant->magicResistance(), resistance);
  const auto healthR = resistant->getAvailableHealth(), healthS = susceptible->getAvailableHealth();
  const auto ownHealth = active->getAvailableHealth();
  const auto mana = attackerSideHero->mana;
  const auto packets = recorder.spellPackets;
  const bool farewell = scenario == Case::FAREWELL;
  const double army = resistant->estimateCombatValue() + susceptible->estimateCombatValue();
  const auto oracle = [&](const CStack * unit, int resist, bool asleep)
  {
   return FHeroes2PinnedOracle::value(raw, resist, unit->getAvailableHealth(), unit->getCount(),
    unit->getMaxHealth(), unit->estimateCombatValue(), army, false, asleep, farewell);
  };
  const double resistantScore = oracle(resistant, resistance, scenario == Case::ZERO_WAKE);
  const double susceptibleScore = oracle(susceptible, 0, false);
  const bool chooseResistant = scenario == Case::OVERKILL || scenario == Case::ZERO;
  if(scenario != Case::MODIFIED && scenario != Case::MODIFIED_ZERO
   && scenario != Case::EVENT && scenario != Case::EVENT_ZERO)
  {
   if(chooseResistant) ASSERT_GT(resistantScore, susceptibleScore);
   else ASSERT_LT(resistantScore, susceptibleScore);
  }
  // Restore shared spell definition on every exit, including fatal assertions.
  auto & level = const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(mechanics->getEffectLevel()));
  struct Restore { JsonNode & target; JsonNode old; ~Restore() { target = old; } } restore{level.battleEffects, level.battleEffects};
  if(scenario == Case::MODIFIED || scenario == Case::MODIFIED_ZERO)
   level.battleEffects["directDamage"]["killByPercentage"].Bool() = true;
  const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(view, environment.get(), active, farewell);
  EXPECT_EQ(resistant->getAvailableHealth(), healthR);
  EXPECT_EQ(susceptible->getAvailableHealth(), healthS);
  EXPECT_EQ(active->getAvailableHealth(), ownHealth);
  EXPECT_EQ(attackerSideHero->mana, mana);
  EXPECT_EQ(recorder.spellPackets, packets);
  if(scenario == Case::MODIFIED || scenario == Case::EVENT) EXPECT_FALSE(selected);
  else if(scenario == Case::MODIFIED_ZERO || scenario == Case::EVENT_ZERO) EXPECT_TRUE(selected);
  else
  {
   ASSERT_TRUE(selected);
   ASSERT_FALSE(selected->target.empty());
   EXPECT_EQ(view->battleGetUnitByPos(selected->target.front().hexValue), chooseResistant ? resistant : susceptible);
  }
  level.battleEffects = restore.old;
  // Execute only genuine source-order ranking cases. Modified definitions and
  // declarative-event admission cases above are deliberately chooser-only.
  if(scenario == Case::OVERKILL || scenario == Case::ZERO_WAKE
   || scenario == Case::FAREWELL || scenario == Case::ZERO || scenario == Case::FULL)
  {
   ASSERT_TRUE(selected);
   EXPECT_EQ(selected->spell, config.spell);
   const int cost = view->battleGetSpellCost(spell, attackerSideHero);
   ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, *selected));
   EXPECT_EQ(attackerSideHero->mana, mana - cost);
   EXPECT_GT(recorder.spellPackets, packets);
   // Real resistance is a random all-or-nothing outcome. The source heuristic's
   // discounted amount is NOT a prediction of actual post-cast HP or kills.
   for(const auto * unit : {resistant, susceptible})
   {
    EXPECT_GE(unit->getAvailableHealth(), 0);
    EXPECT_LE(unit->getAvailableHealth(), unit == resistant ? healthR : healthS);
    EXPECT_GE(unit->getCount(), 0);
    EXPECT_LE(unit->getCount(), unit == resistant ? 4 : 3);
    EXPECT_EQ(unit->alive(), unit->getAvailableHealth() > 0);
   }
   EXPECT_EQ(active->getAvailableHealth(), ownHealth);
  }
  runToCompletion(std::nullopt, false, "FHeroes2Tactics");
 }
};
TEST_F(FHeroes2NativeResistanceBattle, uncappedDiscountBeforeLethalBonus) { exerciseResistance(Case::OVERKILL); }
TEST_F(FHeroes2NativeResistanceBattle, integerZeroBeforeWakePenalty) { exerciseResistance(Case::ZERO_WAKE); }
TEST_F(FHeroes2NativeResistanceBattle, farewellDiscountBeforeWholeKills) { exerciseResistance(Case::FAREWELL); }
TEST_F(FHeroes2NativeResistanceBattle, zeroResistanceRetainsRanking) { exerciseResistance(Case::ZERO); }
TEST_F(FHeroes2NativeResistanceBattle, fullResistanceIsNotAValuedTarget) { exerciseResistance(Case::FULL); }
TEST_F(FHeroes2NativeResistanceBattle, modifiedDamageEffectIsUnavailable) { exerciseResistance(Case::MODIFIED); }

TEST_F(FHeroes2NativeResistanceBattle, zeroResistanceRetainsModifiedNativePreview) { exerciseResistance(Case::MODIFIED_ZERO); }

TEST_F(FHeroes2NativeResistanceBattle, declarativeCombatReactionIsUnavailableForPartialResistance) { exerciseResistance(Case::EVENT); }
TEST_F(FHeroes2NativeResistanceBattle, zeroResistanceRetainsDeclarativeReactionPreview) { exerciseResistance(Case::EVENT_ZERO); }

// Preserve the original nine Magic Arrow identities above. New IDs extend,
// rather than replace, the regression inventory.
class FHeroes2NativeResistanceBySpell : public FHeroes2NativeResistanceBattle,
 public ::testing::WithParamInterface<FHeroes2ResistanceSpellCase> {};
TEST_P(FHeroes2NativeResistanceBySpell, uncappedDiscountBeforeLethalBonus) { exerciseResistance(Case::OVERKILL, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, integerZeroBeforeWakePenalty) { exerciseResistance(Case::ZERO_WAKE, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, farewellDiscountBeforeWholeKills) { exerciseResistance(Case::FAREWELL, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, zeroResistanceRetainsRanking) { exerciseResistance(Case::ZERO, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, fullResistanceIsNotAValuedTarget) { exerciseResistance(Case::FULL, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, modifiedDamageEffectIsUnavailable) { exerciseResistance(Case::MODIFIED, GetParam()); }

TEST_P(FHeroes2NativeResistanceBySpell, zeroResistanceRetainsModifiedNativePreview) { exerciseResistance(Case::MODIFIED_ZERO, GetParam()); }

TEST_P(FHeroes2NativeResistanceBySpell, declarativeCombatReactionIsUnavailableForPartialResistance) { exerciseResistance(Case::EVENT, GetParam()); }
TEST_P(FHeroes2NativeResistanceBySpell, zeroResistanceRetainsDeclarativeReactionPreview) { exerciseResistance(Case::EVENT_ZERO, GetParam()); }

// All four admitted IDs exercise the same source-order and compatibility cases.
// Implosion's base damage exceeds the integer-zero window even at power 0;
// explicit native damage reduction establishes the same adjusted-raw boundary.
INSTANTIATE_TEST_SUITE_P(OrdinaryDirectSpells, FHeroes2NativeResistanceBySpell,
 ::testing::Values(
  FHeroes2ResistanceSpellCase{SpellID(SpellID::ICE_BOLT), "IceBolt", 4, 90, 0},
  FHeroes2ResistanceSpellCase{SpellID(SpellID::LIGHTNING_BOLT), "LightningBolt", 3, 85, 0},
  FHeroes2ResistanceSpellCase{SpellID(SpellID::IMPLOSION), "Implosion", 1, 87, 50}),
 [](const ::testing::TestParamInfo<FHeroes2ResistanceSpellCase> & info) { return info.param.name; });
