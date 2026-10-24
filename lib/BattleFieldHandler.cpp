/*
 * BattleFieldHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include <vcmi/Entity.h>
#include "BattleFieldHandler.h"
#include "GameLibrary.h"
#include "json/JsonBonus.h"
#include "modding/IdentifierStorage.h"
#include "CRandomGenerator.h"
#include "mapObjects/CGHeroInstance.h"
#include "mapObjects/CGCreature.h"
#include "mapObjects/MiscObjects.h"
#include "mapObjectConstructors/AObjectTypeHandler.h"
#include "mapObjectConstructors/CObjectClassesHandler.h"

#include <vstd/RNG.h>

VCMI_LIB_NAMESPACE_BEGIN

std::optional<std::vector<BattleField>> BattleFieldHandler::objectBattlefieldCandidates(
	const CGObjectInstance & object, MapLayerId layer)
{
	// Do not extend CGObjectInstance's vtable or invoke its RNG-consuming
	// virtual getter. Only audited native dynamic types are modeled; a plugin
	// override or other object class is unknown, never silently terrain fallback.
	if(typeid(object) == typeid(CGHeroInstance) || typeid(object) == typeid(CGArtifact))
		return std::vector<BattleField>{};
	if(typeid(object) != typeid(CGCreature) && typeid(object) != typeid(CGTerrainPatch)
		&& typeid(object) != typeid(CGObjectInstance))
		return std::nullopt;
	const auto & fields = LIBRARY->objtypeh->getHandlerFor(object.ID, object.subID)->getBattlefields();
	return compatibleBattlefields(fields, layer);
}

std::shared_ptr<BattleFieldInfo> BattleFieldHandler::loadFromJson(const std::string & scope, const JsonNode & json, const std::string & identifier, size_t index)
{
	assert(identifier.find(':') == std::string::npos);

	auto info = std::make_shared<BattleFieldInfo>(BattleField(index), identifier);

	info->modScope = scope;
	info->graphics = ImagePath::fromJson(json["graphics"]);
	info->icon = json["icon"].String();
	info->name = json["name"].String();
	for(const auto & b : json["bonuses"].Vector())
	{
		auto bonus = JsonUtils::parseBonus(b);

		bonus->source = BonusSource::TERRAIN_OVERLAY;
		bonus->sid = BonusSourceID(info->getId());
		bonus->duration = BonusDuration::PERMANENT;

		info->bonuses.push_back(bonus);
	}

	info->isSpecial = json["isSpecial"].Bool();
	info->limitToLayers.resize(json["limitToLayers"].Vector().size());
	for(int i = 0; i < info->limitToLayers.size(); i++)
	{
		LIBRARY->identifiers()->requestIdentifier("mapLayer", json["limitToLayers"].Vector()[i], [i, info](int32_t idx)
		{
			info->limitToLayers.at(i) = MapLayerId(idx);
		});
	}
	for(auto node : json["impassableHexes"].Vector())
		info->impassableHexes.insert(node.Integer());

	info->openingSoundFilename = AudioPath::fromJson(json["openingSound"]);
	info->musicFilename = AudioPath::fromJson(json["music"]);

	return info;
}

std::vector<JsonNode> BattleFieldHandler::loadLegacyData()
{
	return std::vector<JsonNode>();
}

const std::vector<std::string> & BattleFieldHandler::getTypeNames() const
{
	static const auto types = std::vector<std::string> { "battlefield" };

	return types;
}

int32_t BattleFieldInfo::getIndex() const
{
	return battlefield.getNum();
}

int32_t BattleFieldInfo::getIconIndex() const
{
	return iconIndex;
}

std::string BattleFieldInfo::getJsonKey() const
{
	return modScope + ':' + identifier;
}

std::string BattleFieldInfo::getModScope() const
{
	return modScope;
}

std::string BattleFieldInfo::getNameTextID() const
{
	return name;
}

std::string BattleFieldInfo::getNameTranslated() const
{
	return name; // TODO?
}

void BattleFieldInfo::registerIcons(const IconRegistar & cb) const
{
	//cb(getIconIndex(), "BATTLEFIELD", icon);
}

BattleField BattleFieldInfo::getId() const
{
	return battlefield;
}

std::vector<BattleField> BattleFieldHandler::compatibleBattlefields(const std::vector<BattleField> & battleFields, MapLayerId currentLayer)
{
	std::vector<BattleField> filteredBattleFields;
	for(auto & battleField : battleFields)
		if(battleField.getInfo()->limitToLayers.empty() || vstd::contains(battleField.getInfo()->limitToLayers, currentLayer))
			filteredBattleFields.push_back(battleField);

	if (filteredBattleFields.empty() && !battleFields.empty())
	{
		logGlobal->warn("No battlefield for layer %s found, fallback", MapLayerId::encode(currentLayer));
		filteredBattleFields = battleFields;
	}

	return filteredBattleFields;
}

BattleField BattleFieldHandler::selectRandomBattlefield(const std::vector<BattleField> & battleFields, MapLayerId currentLayer, vstd::RNG & randomGenerator)
{
	const auto filteredBattleFields = compatibleBattlefields(battleFields, currentLayer);
	if(filteredBattleFields.empty())
		return BattleField::NONE;

	return *RandomGeneratorUtil::nextItem(filteredBattleFields, randomGenerator);
}

VCMI_LIB_NAMESPACE_END
