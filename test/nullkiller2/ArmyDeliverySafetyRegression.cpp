/*
 * ArmyDeliverySafetyRegression.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../AI/Nullkiller2/Pathfinding/AINodeStorage.h"

VCMI_LIB_USING_NAMESPACE

namespace test
{
TEST(Nullkiller2ArmyDeliverySafetyRegression, ReceiverStrengthCannotMakeCarrierRouteSafe)
{
	CGHeroInstance carrier(nullptr);
	CGHeroInstance receiver(nullptr);
	carrier.pushPrimSkill(PrimarySkill::ATTACK, 0);
	carrier.pushPrimSkill(PrimarySkill::DEFENSE, 0);
	receiver.pushPrimSkill(PrimarySkill::ATTACK, 80);
	receiver.pushPrimSkill(PrimarySkill::DEFENSE, 80);
	CCreatureSet army;
	army.setCreature(SlotID(0), CreatureID(2), 20);
	const auto danger = 2 * army.getArmyStrength();

	NK2AI::AIPath path;
	path.targetHero = &carrier;
	path.heroArmy = &army;
	path.targetObjectDanger = danger;
	// A powerful receiver is not present during the courier's journey.
	ASSERT_TRUE(NK2AI::isSafeToVisit(&receiver, &army, danger, 1.1f));
	EXPECT_FALSE(NK2AI::isSafeToVisit(path, 1.1f));

	path.targetHero = &receiver;
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}

TEST(Nullkiller2ArmyDeliverySafetyRegression, CarrierChecksDangerAlongTheWholeRoute)
{
	CGHeroInstance carrier(nullptr);
	CCreatureSet army;
	army.setCreature(SlotID(0), CreatureID(2), 20);
	NK2AI::AIPath path;
	path.targetHero = &carrier;
	path.heroArmy = &army;
	path.targetObjectDanger = 0;
	NK2AI::AIPathNodeInfo destination{};
	destination.targetHero = &carrier;
	destination.danger = 2 * army.getArmyStrength();
	path.nodes.push_back(destination);
	EXPECT_FALSE(NK2AI::isSafeToVisit(path, 1.1f));

	path.nodes.front().danger = 0;
	EXPECT_TRUE(NK2AI::isSafeToVisit(path, 1.1f));
}
}
