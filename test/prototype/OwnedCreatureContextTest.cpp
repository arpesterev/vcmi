#include "StdInc.h"
#include "lib/CCreatureHandler.h"
#include "lib/battle/OwnedCreatureContext.h"
#include "lib/bonuses/BonusCache.h"
#include "lib/bonuses/Limiters.h"
#include <type_traits>

namespace
{
BonusEvaluationSnapshot capture(const CCreature & creature)
{
 return BonusEvaluationSnapshot::capture({&creature}, [](const CBonusSystemNode & node)
 {
  BonusLimiterFacts facts;
  facts.type = node.getNodeType(); facts.originalOwner = node.getOwner();
  return facts;
 });
}
}

static_assert(!std::is_copy_constructible_v<OwnedCreatureContextController>);
static_assert(std::is_nothrow_move_constructible_v<OwnedCreatureContextController>);

TEST(OwnedCreatureContext, DynamicBaseStatsUseRawNativeExportsAndFrozenScalarsStayFrozen)
{
 CCreature source;
 source.addBonus(11, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK));
 source.addBonus(12, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE));
 source.addBonus(3, BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageMin);
 source.addBonus(7, BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageMax);
 source.addBonus(21, BonusType::STACK_HEALTH);
 source.addBonus(4, BonusType::CASTS);
 source.addBonus(6, BonusType::STACKS_SPEED);
 source.addBonus(9, BonusType::SHOTS);
 auto evaluation = capture(source);
 const auto node = evaluation.graph().roots().at(0);
 OwnedCreatureContextController controller(evaluation, 1);
 OwnedCreature::Values values{};
 values.getBaseDamageMin = 81;
 OwnedCreature dynamic(values, controller.view(), node);
 OwnedCreature frozen(values, evaluation, node);
 EXPECT_EQ(frozen.getBaseDamageMin(), 81);
 EXPECT_EQ(dynamic.getBaseAttack(), source.getBaseAttack());
 EXPECT_EQ(dynamic.getBaseDefense(), source.getBaseDefense());
 EXPECT_EQ(dynamic.getBaseDamageMin(), source.getBaseDamageMin());
 EXPECT_EQ(dynamic.getBaseDamageMax(), source.getBaseDamageMax());
 EXPECT_EQ(dynamic.getBaseHitPoints(), source.getBaseHitPoints());
 EXPECT_EQ(dynamic.getBaseSpellPoints(), source.getBaseSpellPoints());
 EXPECT_EQ(dynamic.getBaseSpeed(), source.getBaseSpeed());
 EXPECT_EQ(dynamic.getBaseShots(), source.getBaseShots());
}

TEST(OwnedCreatureContext, RawBaseStatsIgnoreInheritedAndNonCreatureAbilityValues)
{
 CCreature parent, source;
 parent.addBonus(100, BonusType::STACK_HEALTH);
 source.attachTo(parent);
 source.addBonus(21, BonusType::STACK_HEALTH);
 source.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::SPELL_EFFECT, 300, SpellID(SpellID::HASTE)));
 const auto evaluation = capture(source);
 OwnedCreatureContextController controller(evaluation, 1);
 OwnedCreature dynamic({}, controller.view(), evaluation.graph().roots().at(0));
 EXPECT_EQ(dynamic.getBaseHitPoints(), source.getBaseHitPoints());
 EXPECT_EQ(dynamic.getBaseHitPoints(), 21);
 EXPECT_EQ(dynamic.getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 421);
 source.detachFrom(parent);
}

TEST(OwnedCreatureContext, RetainedViewSurvivesNativeAndControllerDestruction)
{
 std::shared_ptr<const OwnedCreature> retained;
 {
  CCreature source;
  source.addBonus(21, BonusType::STACK_HEALTH);
  auto evaluation = capture(source);
  OwnedCreatureContextController controller(evaluation, 1);
  OwnedCreatureContextController moved(std::move(controller));
  EXPECT_THROW(controller.view(), std::logic_error);
  retained = std::make_shared<const OwnedCreature>(OwnedCreature::Values{}, moved.view(), evaluation.graph().roots().at(0));
 }
 EXPECT_EQ(retained->getBaseHitPoints(), 21);
 BonusValueCache cache(retained->getBonusBearer(), Selector::type()(BonusType::STACK_HEALTH));
 EXPECT_EQ(cache.getValue(), 21);
 EXPECT_GT(retained->getBonusBearer()->getTreeVersion(), 0);
 auto query = retained->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::STACK_HEALTH));
 query->front()->val = 900;
 EXPECT_EQ(retained->getBaseHitPoints(), 21);
 EXPECT_EQ(retained->getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 21);
}

TEST(OwnedCreatureContext, RebindRejectsForeignCaptureAndPreservesOwnedValues)
{
 CCreature source;
 source.addBonus(21, BonusType::STACK_HEALTH);
 auto evaluation = capture(source);
 auto foreign = capture(source);
 OwnedCreatureContextController first(evaluation, 1), fork(evaluation, 1), unrelated(foreign, 1);
 OwnedCreature::Values values{};
 values.getNameSingularTranslated = "retained";
 OwnedCreature original(values, first.view(), evaluation.graph().roots().at(0));
 auto rebound = original.rebind(fork.view());
 EXPECT_EQ(rebound->getNameSingularTranslated(), "retained");
 EXPECT_EQ(rebound->getBaseHitPoints(), 21);
 EXPECT_THROW(original.rebind(unrelated.view()), std::invalid_argument);
 EXPECT_THROW(original.rebind({}), std::invalid_argument);
}

TEST(OwnedCreatureContext, RejectsInvalidInitialCaptureAndZeroRevision)
{
 CCreature source;
 const auto evaluation = capture(source);
 EXPECT_THROW((OwnedCreatureContextController(evaluation, 0)), std::invalid_argument);
 EXPECT_THROW((OwnedCreatureContextController(BonusEvaluationSnapshot{}, 1)), std::invalid_argument);
 OwnedCreatureContextController controller(evaluation, 1);
 EXPECT_THROW((OwnedCreature(OwnedCreature::Values{}, controller.view(), 100)), std::out_of_range);
}

TEST(OwnedCreatureContext, RawBaseStatsDoNotApplyLimitersOrStacking)
{
 CCreature source;
 auto rejected = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 21, CreatureID(0));
 rejected->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::FLYING);
 source.addNewBonus(rejected);
 auto one = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 3, CreatureID(0));
 auto two = std::make_shared<Bonus>(*one);
 one->stacking = "context-test"; two->stacking = "context-test";
 source.addNewBonus(one); source.addNewBonus(two);
 const auto evaluation = capture(source);
 OwnedCreatureContextController controller(evaluation, 1);
 OwnedCreature dynamic({}, controller.view(), evaluation.graph().roots().at(0));
 EXPECT_EQ(source.getBaseHitPoints(), 27);
 EXPECT_EQ(dynamic.getBaseHitPoints(), source.getBaseHitPoints());
 EXPECT_EQ(dynamic.getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 3);
 auto copied = dynamic.getBonusBearer()->getAllBonuses(Selector::type()(BonusType::STACK_HEALTH));
 copied->front()->stacking = "changed-query";
 EXPECT_EQ(dynamic.getBaseHitPoints(), 27);
 EXPECT_EQ(dynamic.getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 3);
}

TEST(OwnedCreatureContext, QueryConfigurationIsConstOwnedAndIndependentOfNativeMutation)
{
 static_assert(std::is_const_v<TLimiterPtr::element_type>);
 static_assert(std::is_const_v<TUpdaterPtr::element_type>);
 static_assert(std::is_const_v<TBonusParametersPtr::element_type>);
 CCreature source;
 auto limiter = std::make_shared<AllOfLimiter>();
 auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 21, CreatureID(0));
 bonus->limiter = limiter;
 source.addNewBonus(bonus);
 const auto evaluation = capture(source);
 OwnedCreatureContextController controller(evaluation, 1);
 OwnedCreature dynamic({}, controller.view(), evaluation.graph().roots().at(0));
 auto first = dynamic.getBonusBearer()->getAllBonuses(Selector::type()(BonusType::STACK_HEALTH));
 ASSERT_EQ(first->size(), 1);
 EXPECT_NE(first->front()->limiter.get(), limiter.get());
 limiter->add(std::make_shared<HasAnotherBonusLimiter>(BonusType::FLYING));
 first->front()->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::FLYING);
 first->front()->val = 700;
 const auto second = dynamic.getBonusBearer()->getAllBonuses(Selector::type()(BonusType::STACK_HEALTH));
 ASSERT_EQ(second->size(), 1);
 EXPECT_EQ(second->front()->val, 21);
 EXPECT_NE(first->front().get(), second->front().get());
 EXPECT_EQ(dynamic.getBaseHitPoints(), 21);
}


TEST(OwnedCreatureContext, RetainPreservesDynamicAndFrozenViewsWithoutKeepingOriginalDescriptor)
{
 std::shared_ptr<const OwnedCreature> dynamic, frozen;
 {
  CCreature source;
  source.addBonus(21, BonusType::STACK_HEALTH);
  auto evaluation = capture(source);
  OwnedCreatureContextController controller(evaluation, 1);
  OwnedCreature::Values values{};
  values.getBaseHitPoints = 81;
  values.getNameSingularTranslated = "retained";
  OwnedCreature originalDynamic(values, controller.view(), evaluation.graph().roots().at(0));
  OwnedCreature originalFrozen(values, evaluation, evaluation.graph().roots().at(0));
  dynamic = originalDynamic.retain();
  frozen = originalFrozen.retain();
 }
 EXPECT_EQ(dynamic->getBaseHitPoints(), 21);
 EXPECT_EQ(frozen->getBaseHitPoints(), 81);
 EXPECT_EQ(dynamic->getNameSingularTranslated(), "retained");
 EXPECT_EQ(frozen->getNameSingularTranslated(), "retained");
 EXPECT_EQ(dynamic->getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 21);
 EXPECT_EQ(frozen->getBonusBearer()->valOfBonuses(BonusType::STACK_HEALTH), 21);
}
