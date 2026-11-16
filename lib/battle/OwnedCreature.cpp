/* Owned creature boundary proposal. GPL-2.0-or-later. */
#include "StdInc.h"
#include "OwnedCreature.h"
#include "OwnedCreatureContext.h"
#include "../bonuses/OwnedBonusValues.h"
#include "../bonuses/OwnedBonusGraphSession.h"

OwnedCreatureSnapshot::OwnedCreatureSnapshot(BonusEvaluationSnapshot evaluation, int32_t revision)
 : evaluation(std::move(evaluation)), revision(revision) {}

std::shared_ptr<const OwnedCreatureSnapshot> OwnedCreatureSnapshot::create(BonusEvaluationSnapshot evaluation)
{
 return std::shared_ptr<const OwnedCreatureSnapshot>(new OwnedCreatureSnapshot(std::move(evaluation)));
}

OwnedCreature::Values OwnedCreature::captureValues(const Creature & source, std::set<TerrainId> factionNativeTerrains)
{
 Values result;
 result.getIndex = source.getIndex();
 result.getIconIndex = source.getIconIndex();
 result.getJsonKey = source.getJsonKey();
 result.getModScope = source.getModScope();
 result.getNamePluralTranslated = source.getNamePluralTranslated();
 result.getNameSingularTranslated = source.getNameSingularTranslated();
 result.getNamePluralTextID = source.getNamePluralTextID();
 result.getNameSingularTextID = source.getNameSingularTextID();
 result.getId = source.getId();
 result.getFactionID = source.getFactionID();
 result.getWarMachine = source.getWarMachine();
 result.getAdvMapAmountMin = source.getAdvMapAmountMin();
 result.getAdvMapAmountMax = source.getAdvMapAmountMax();
 result.getAIValue = source.getAIValue();
 result.getFightValue = source.getFightValue();
 result.getLevel = source.getLevel();
 result.getGrowth = source.getGrowth();
 result.getHorde = source.getHorde();
 result.getBaseAttack = source.getBaseAttack();
 result.getBaseDefense = source.getBaseDefense();
 result.getBaseDamageMin = source.getBaseDamageMin();
 result.getBaseDamageMax = source.getBaseDamageMax();
 result.getBaseHitPoints = source.getBaseHitPoints();
 result.getBaseSpellPoints = source.getBaseSpellPoints();
 result.getBaseSpeed = source.getBaseSpeed();
 result.getBaseShots = source.getBaseShots();
 result.hasUpgrades = source.hasUpgrades();
 result.isDoubleWide = source.isDoubleWide();
 result.recruitCost = source.getFullRecruitCost();
 result.factionNativeTerrains = std::move(factionNativeTerrains);
 source.registerIcons([&](int32_t index, int32_t group, const std::string & list, const std::string & image)
 {
  result.icons.push_back({index, group, list, image});
 });
 return result;
}

OwnedCreature::OwnedCreature(Values values, BonusEvaluationSnapshot snapshot, BonusEvaluationSnapshot::NodeId node)
 : OwnedCreature(std::move(values), OwnedCreatureSnapshot::create(std::move(snapshot)), node) {}

OwnedCreature::OwnedCreature(Values values, std::shared_ptr<const OwnedCreatureSnapshot> snapshot, BonusEvaluationSnapshot::NodeId node)
 : values(std::move(values)), snapshot(std::move(snapshot)), node(node)
{
 if(!this->snapshot)
  throw std::invalid_argument("OwnedCreature requires an owned snapshot");
 if(this->snapshot->evaluation.graph().nodes().at(node).type != BonusNodeType::CREATURE)
  throw std::invalid_argument("OwnedCreature requires a captured creature node");
}

int32_t OwnedCreature::getRecruitCost(GameResID resource) const
{
 return values.recruitCost[resource];
}

void OwnedCreature::registerIcons(const IconRegistar & callback) const
{
 for(const auto & icon : values.icons)
  callback(icon.index, icon.group, icon.list, icon.image);
}

bool OwnedCreature::isNativeTerrain(TerrainId terrain) const
{
 return getBonusBearer()->hasBonusOfType(BonusType::TERRAIN_NATIVE)
  || values.factionNativeTerrains.contains(terrain);
}

TConstBonusListPtr OwnedCreature::getAllBonuses(const CSelector & selector, const std::string &) const
{
 return pin()->evaluation.getAllBonuses(node, selector);
}

class OwnedCreature::NativeBonusBearer final : public IBonusBearer
{
 const std::shared_ptr<const OwnedCreature> definition;
public:
 explicit NativeBonusBearer(std::shared_ptr<const OwnedCreature> definition)
  : definition(std::move(definition)) {}
 TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key) const override
 {
  const auto current = definition->pin();
  if(!current->session) throw std::logic_error("Native creature view requires an owned cache session");
  return current->session->query(definition->node, selector, key);
 }
 int32_t getTreeVersion() const override
 {
  const auto current = definition->pin();
  if(!current->session) throw std::logic_error("Native creature view requires an owned cache session");
  const auto version = current->session->nodeVersion(definition->node);
  if(version > INT32_MAX || version < INT32_MIN) throw std::overflow_error("Owned creature node epoch exhausted");
  return static_cast<int32_t>(version);
 }
};

std::shared_ptr<const IBonusBearer> OwnedCreature::retainNativeBonusBearer() const
{
 if(!pin()->session) throw std::logic_error("Native creature view requires an owned cache session");
 return std::make_shared<const NativeBonusBearer>(retain());
}

OwnedCreature::OwnedCreature(Values values, std::shared_ptr<const OwnedCreatureContext> context, BonusEvaluationSnapshot::NodeId node)
 : values(std::move(values)), context(std::move(context)), node(node)
{
 if(!this->context)
  throw std::invalid_argument("OwnedCreature requires an owned context");
 const auto current = pin();
 if(current->evaluation.graph().nodes().at(node).type != BonusNodeType::CREATURE)
  throw std::invalid_argument("OwnedCreature requires a captured creature node");
}

std::shared_ptr<const OwnedCreatureSnapshot> OwnedCreature::pin() const
{
 return context ? context->current : snapshot;
}

std::shared_ptr<const OwnedCreature> OwnedCreature::rebind(std::shared_ptr<const OwnedCreatureContext> next) const
{
 if(!next || !pin()->evaluation.graph().sameOrigin(next->current->evaluation.graph()))
  throw std::invalid_argument("OwnedCreature rebind requires the same capture");
 return std::make_shared<const OwnedCreature>(values, std::move(next), node);
}

int32_t OwnedCreature::getTreeVersion() const
{
 return pin()->revision;
}

int32_t OwnedCreature::rawBaseValue(const CSelector & selector) const
{
 const auto current = pin();
 const auto & graph = current->evaluation.graph();
 const auto copies = OwnedBonusValues::capture(graph).query(graph.nodes().at(node).exported);
 BonusList exported;
 for(const auto & bonus : copies)
  exported.push_back(bonus);
 return exported.valOfBonuses(selector);
}
int32_t OwnedCreature::getBaseAttack() const
{
 if(!context) return values.getBaseAttack;
	static const auto SELECTOR = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseDefense() const
{
 if(!context) return values.getBaseDefense;
	static const auto SELECTOR = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseDamageMin() const
{
 if(!context) return values.getBaseDamageMin;
	static const auto SELECTOR = Selector::typeSubtype(BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageMin).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseDamageMax() const
{
 if(!context) return values.getBaseDamageMax;
	static const auto SELECTOR = Selector::typeSubtype(BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageMax).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseHitPoints() const
{
 if(!context) return values.getBaseHitPoints;
	static const auto SELECTOR = Selector::type()(BonusType::STACK_HEALTH).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseSpellPoints() const
{
 if(!context) return values.getBaseSpellPoints;
	static const auto SELECTOR = Selector::type()(BonusType::CASTS).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseSpeed() const
{
 if(!context) return values.getBaseSpeed;
	static const auto SELECTOR = Selector::type()(BonusType::STACKS_SPEED).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}

int32_t OwnedCreature::getBaseShots() const
{
 if(!context) return values.getBaseShots;
	static const auto SELECTOR = Selector::type()(BonusType::SHOTS).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	return rawBaseValue(SELECTOR);
}


std::shared_ptr<const OwnedCreature> OwnedCreature::retain() const
{
 if(context)
  return std::make_shared<const OwnedCreature>(values, context, node);
 return std::make_shared<const OwnedCreature>(values, snapshot, node);
}
