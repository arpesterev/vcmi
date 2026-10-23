/*
 * HeroRoleRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"

#include "../../AI/Nullkiller2/Engine/Nullkiller.h"
#include "../../lib/mapObjects/CGTownInstance.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
namespace
{
class HeroRoleCallback final : public CCallback
{
public:
	std::vector<const CGHeroInstance *> heroes;
	std::vector<const CGTownInstance *> towns;
	int day = 1;
	int3 mapSize{72, 72, 1};

	HeroRoleCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return heroes; }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return towns; }
	int getDate(Date mode) const override
	{
		EXPECT_EQ(mode, Date::DAY);
		return day;
	}
	int3 getMapSize() const override { return mapSize; }
};

void initializeRoleHero(CGHeroInstance & hero, int id, int attack)
{
	hero.id = ObjectInstanceID(id);
	hero.setHeroType(HeroTypeID(0));
	hero.secSkills.clear();
	hero.pushPrimSkill(PrimarySkill::ATTACK, attack);
	hero.pushPrimSkill(PrimarySkill::DEFENSE, 0);
	hero.pushPrimSkill(PrimarySkill::SPELL_POWER, 1);
	hero.pushPrimSkill(PrimarySkill::KNOWLEDGE, 1);
}
}

class Nullkiller2HeroRoleRegression : public ::testing::Test
{
protected:
	std::shared_ptr<HeroRoleCallback> callback = std::make_shared<HeroRoleCallback>();
	NK2AI::Nullkiller ai;
	CGHeroInstance strong{callback.get()};
	CGHeroInstance weak{callback.get()};
	CGTownInstance town{callback.get()};

	void SetUp() override
	{
		ai.cc = callback;
		initializeRoleHero(strong, 1, 10);
		initializeRoleHero(weak, 2, 0);
		callback->heroes = {&weak, &strong};
		ai.heroManager = std::make_unique<NK2AI::HeroManager>(callback.get(), &ai);
	}
};

TEST_F(Nullkiller2HeroRoleRegression, LastTownLostStillKeepsBestHeroAsMain)
{
	callback->towns = {&town};
	ai.heroManager->update();
	ASSERT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&strong), NK2AI::MAIN);

	callback->towns.clear();
	ai.heroManager->update();
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&strong), NK2AI::MAIN);
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&weak), NK2AI::SCOUT);
}

TEST_F(Nullkiller2HeroRoleRegression, TownlessLargeMapDoesNotPromoteEveryHero)
{
	callback->day = 28;
	callback->mapSize = int3(252, 252, 1);
	ai.heroManager->update();
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&strong), NK2AI::MAIN);
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&weak), NK2AI::SCOUT);
}

TEST_F(Nullkiller2HeroRoleRegression, OneTownEarlyGameRetainsExistingMainLimit)
{
	callback->towns = {&town};
	ai.heroManager->update();
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&strong), NK2AI::MAIN);
	EXPECT_EQ(ai.heroManager->getHeroRoleOrDefaultInefficient(&weak), NK2AI::SCOUT);
}

TEST_F(Nullkiller2HeroRoleRegression, NoHeroesIsValid)
{
	callback->heroes.clear();
	EXPECT_NO_THROW(ai.heroManager->update());
}
}
