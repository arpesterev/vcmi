// Native ammunition decision inventory.

TEST_F(AIIntegrationNativeBattle, fheroes2AmmoExhaustedClusterUsesMeleeMaximum)
{ exerciseAmmoPosition(AmmoPositionMode::Exhausted); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoLoadedBlockedClusterRetainsAdditiveValue)
{ exerciseAmmoPosition(AmmoPositionMode::LoadedBlocked); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoForgottenLoadedClusterRetainsAdditiveValue)
{ exerciseAmmoPosition(AmmoPositionMode::Forgotten); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoMixedClusterUsesMeleeMaximumPlusLoaded)
{ exerciseAmmoPosition(AmmoPositionMode::Mixed); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoWideExhaustedNeighborCountsOnce)
{ exerciseAmmoPosition(AmmoPositionMode::WideMixed); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoEquippedLiveCartSuppliesZeroCounterCluster)
{ exerciseAmmoPosition(AmmoPositionMode::Supplied); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoExhaustedGroundPursuerStopsCautiousAdvance)
{ exerciseAmmoPath(AmmoPathMode::Exhausted); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoLoadedUnengagedPursuerRemainsExempt)
{ exerciseAmmoPath(AmmoPathMode::Loaded); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoLoadedEngagedPursuerRetainsMeleeRisk)
{ exerciseAmmoPath(AmmoPathMode::Engaged); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoBlindExhaustedPursuerDoesNotStopAdvance)
{ exerciseAmmoPath(AmmoPathMode::Blind); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoAlreadyMovedExhaustedPursuerStillThreatens)
{ exerciseAmmoPath(AmmoPathMode::AlreadyMoved); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoFlyingExhaustedPursuerRemainsExempt)
{ exerciseAmmoPath(AmmoPathMode::Flying); }
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoRealLastShotChangesCautiousDecision)
{ exerciseAmmoPath(AmmoPathMode::LastShot); }

// Supplementary guard witness; the thirteen battle decisions remain separate.
TEST_F(AIIntegrationNativeBattle, fheroes2AmmoSnapshotDomainRejectsLossyComponents)
{
	struct TenUnits final : BonusUpdaterContext
	{
		BonusNodeType nodeType() const override { return BonusNodeType::STACK_BATTLE; }
		PlayerColor owner() const override { return PlayerColor(0); }
		int heroLevel() const override { return 0; }
		int stackCount() const override { return 10; }
		int stackLevel() const override { return 1; }
		std::vector<BonusUpdaterArmyStack> armyStacks() const override { return {}; }
		int armySize(CreatureID, FactionID, int) const override { return 0; }
	} facts;
	struct UnknownUpdater final : IUpdater
	{
		int multiplier = 3;
		std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & value, const BonusUpdaterContext &) const override
		{
			auto result = std::make_shared<Bonus>(*value);
			result->val *= multiplier;
			return result;
		}
	};
	struct UnknownLimiter final : ILimiter {};
	struct UnknownPropagator final : IPropagator {};

	auto value = std::make_shared<Bonus>();
	auto lossy = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, 1);
	value->updater = lossy;
	const std::vector<const Bonus *> roots{value.get()};
	const auto nativeBytes = [&]()
	{
		CMemorySerializer serializer;
		serializer.oser.version = ESerializationVersion::CURRENT;
		serializer.oser & roots;
		return serializer.extractBuffer();
	};
	const auto before = nativeBytes();
	EXPECT_EQ(lossy->createUpdatedBonus(value, facts)->val, 10);
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);
	*lossy = TimesStackSizeUpdater(0, 100, 1, 2);
	EXPECT_EQ(value->updater.get(), lossy.get());
	EXPECT_EQ(lossy->createUpdatedBonus(value, facts)->val, 20);
	EXPECT_EQ(nativeBytes(), before) << "Native serialization omits the changed semantic field";
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);

	value->updater = nullptr;
	value->propagationUpdater = lossy;
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters.push_back(lossy);
	value->propagationUpdater = composite;
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);
	value->propagationUpdater = nullptr;
	value->updater = composite;
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);
	auto outer = std::make_shared<Bonus>();
	BonusParametersOnCombatEvent event;
	event.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{value, false});
	outer->parameters = std::make_shared<BonusParameters>(event);
	const std::vector<const Bonus *> nestedRoots{outer.get()};
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	composite->updaters = {std::make_shared<UnknownUpdater>()};
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	value->updater = std::make_shared<UnknownUpdater>();
	EXPECT_THROW(ammoBonusValues(roots), std::invalid_argument);
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	value->updater = nullptr;
	value->propagationUpdater = std::make_shared<UnknownUpdater>();
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	value->propagationUpdater = nullptr;
	auto aggregate = std::make_shared<AllOfLimiter>();
	aggregate->add(std::make_shared<UnknownLimiter>());
	value->limiter = aggregate;
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	value->limiter = nullptr;
	value->propagator = std::make_shared<UnknownPropagator>();
	EXPECT_THROW(ammoBonusValues(nestedRoots), std::invalid_argument);
	value->propagator = nullptr;
	EXPECT_NO_THROW(ammoBonusValues(nestedRoots));
}
