#include "OwnedUnitLifecycleTest.inc"
class OwnedStateSyncTest : public OwnedUnitLifecycleTest
{
public:
 std::vector<OwnedBattleBonusModel::UnitFacts> observations(const OwnedBattleBonusModel & model)
 {
  auto values=model.units();
  for(auto & value:values)
  {
   const auto *state=dynamic_cast<const battle::CUnitState *>(battle()->battleGetUnitByID(value.id));value=model.observeUnit(value.id,*state);
  }
  return values;
 }
};
TEST_F(OwnedStateSyncTest, DirectCountChangeIsObservedWithoutOperationFlags)
{
 auto *source=add(5);auto b=bonus(BonusType::STACKS_SPEED,1);b->updater=std::make_shared<TimesStackSizeUpdater>();source->addNewBonus(b);auto model=OwnedBattleBonusModel::capture(*battle());auto parent=model;auto initial=model.revision();int64_t damage=source->getMaxHealth();source->damage(damage);ASSERT_EQ(source->getCount(),9);
 ASSERT_TRUE(model.synchronizeUnits(observations(model)));EXPECT_GT(model.revision(),initial);auto selected=model.getAllBonuses(source->unitId(),selector());ASSERT_EQ(selected->size(),1);EXPECT_EQ(selected->front()->val,b->updater->createUpdatedBonus(b,*source)->val);EXPECT_EQ(parent.getAllBonuses(source->unitId(),selector())->front()->val,10);auto version=model.revision();EXPECT_FALSE(model.synchronizeUnits(observations(model)));EXPECT_EQ(model.revision(),version);
}
TEST_F(OwnedStateSyncTest, NativeMoveUpdatesAdjacencyWithoutRememberingMoveFlag)
{
 auto *source=add(5);auto *recipient=add(6);auto b=bonus(BonusType::STACKS_SPEED,1);auto limiter=std::make_shared<UnitAdjacentLimiter>();limiter->targetUnit=source->creatureId();b->limiter=limiter;recipient->addNewBonus(b);auto model=OwnedBattleBonusModel::capture(*battle());ASSERT_EQ(model.getAllBonuses(recipient->unitId(),selector())->size(),1);battle()->moveUnit(source->unitId(),BattleHex(10,5));ASSERT_TRUE(model.synchronizeUnits(observations(model)));EXPECT_TRUE(model.getAllBonuses(recipient->unitId(),selector())->empty());EXPECT_EQ(model.getAllBonuses(recipient->unitId(),selector())->size(),recipient->getAllBonuses(selector())->size());
}
TEST_F(OwnedStateSyncTest, InvalidObservationsCannotPartiallyChangeState)
{
 add(5);auto model=OwnedBattleBonusModel::capture(*battle());auto values=model.units();auto before=values;auto version=model.revision();values[0].count=0;
 EXPECT_THROW(model.synchronizeUnits(values),std::invalid_argument);EXPECT_EQ(model.units(),before);EXPECT_EQ(model.revision(),version);values=before;values[0].node+=1;
 EXPECT_THROW(model.synchronizeUnits(values),std::invalid_argument);EXPECT_EQ(model.units(),before);
}
class QueryForbiddenState final : public battle::CUnitState
{
public:
 int32_t unitBaseAmount() const override {throw std::logic_error("metadata query");}
 uint32_t unitId() const override {throw std::logic_error("metadata query");}
 BattleSide unitSide() const override {throw std::logic_error("metadata query");}
 PlayerColor unitOwner() const override {throw std::logic_error("owner query");}
 SlotID unitSlot() const override {throw std::logic_error("metadata query");}
 const CCreature *unitType() const override {throw std::logic_error("creature query");}
 TConstBonusListPtr getAllBonuses(const CSelector &,const std::string &) const override {throw std::logic_error("recursive bonus query");}
 int32_t getTreeVersion() const override {throw std::logic_error("recursive version query");}
 void spendMana(ServerCallback *,int) const override {throw std::logic_error("unexpected spell action");}
};
TEST_F(OwnedStateSyncTest, ObservationCannotReenterBonusVersionOrOwnerQueries)
{
 auto *source=add(5);auto model=OwnedBattleBonusModel::capture(*battle());QueryForbiddenState state;state.health=source->health;state.position=BattleHex(9,5);state.cloneID=17;state.ghost=true;state.ghostPending=true;
 auto value=model.observeUnit(source->unitId(),state);EXPECT_EQ(value.count,10);EXPECT_TRUE(value.alive);EXPECT_TRUE(value.ghost);EXPECT_TRUE(value.ghostPending);EXPECT_EQ(value.cloneID,17);EXPECT_EQ(value.hexes,battle::Unit::getHexes(state.position,source->doubleWide(),source->unitSide()));auto all=model.units();for(auto &entry:all)if(entry.id==value.id)entry=value;EXPECT_TRUE(model.synchronizeUnits(all));
}
TEST_F(OwnedStateSyncTest, WarmedNativeBonusCacheRefreshesExactPublicStateObservations)
{
 auto *source=add(5);auto value=bonus(BonusType::STACKS_SPEED,1);value->updater=std::make_shared<TimesStackSizeUpdater>();source->addNewBonus(value);auto model=OwnedBattleBonusModel::capture(*battle());
 struct Bearer final : IBonusBearer
 {
  OwnedBattleBonusModel &model;uint32_t id;std::function<void()> sync;mutable int depth=0;
  Bearer(OwnedBattleBonusModel &m,uint32_t i,std::function<void()> s):model(m),id(i),sync(std::move(s)){}
  void refresh() const {if(depth++)throw std::logic_error("refresh reentered");sync();--depth;}
  int32_t getTreeVersion() const override {refresh();return static_cast<int32_t>(model.revision());}
  TConstBonusListPtr getAllBonuses(const CSelector &s,const std::string &) const override {refresh();return model.getAllBonuses(id,s);}
 } bearer(model,source->unitId(),[&]{model.synchronizeUnits(observations(model));});
 BonusValueCache cache(&bearer,selector());EXPECT_EQ(cache.getValue(),10);int64_t damage=source->getMaxHealth();source->health.damage(damage);ASSERT_EQ(source->health.getCount(),9);EXPECT_EQ(cache.getValue(),9);EXPECT_EQ(cache.getValue(),9);EXPECT_EQ(bearer.depth,0);
}
TEST_F(OwnedStateSyncTest, InvalidWideCaptureAndObservationSkipUnsafeSurroundingGeometry)
{
 auto *wide=addStack(BattleSide::DEFENDER,creatureByName("core:unicorn"),BattleHex(8,5),10);ASSERT_TRUE(wide->doubleWide());auto model=OwnedBattleBonusModel::capture(*battle());wide->position=BattleHex::INVALID;auto fact=model.observeUnit(wide->unitId(),*wide);EXPECT_FALSE(fact.validPosition);EXPECT_EQ(fact.hexes,wide->getHexes());EXPECT_TRUE(fact.surrounding.empty());auto initialInvalid=OwnedBattleBonusModel::capture(*battle());for(const auto &unit:initialInvalid.units())if(unit.id==wide->unitId()){EXPECT_FALSE(unit.validPosition);EXPECT_EQ(unit.hexes,wide->getHexes());EXPECT_TRUE(unit.surrounding.empty());}
}
