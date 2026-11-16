/*
 * HoMM3SpellValue.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 * Source: sushi-shi/homm3-decomp, e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/ai_tactical.cpp (CC0-1.0). Combat values are native VCMI projections.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace AIIntegration::Ports::HoMM3
{
// Stored source attack distance is reused at both speeds, including flyers.
inline int speedArrivalTime(uint32_t distance, int speed, bool shooting)
{
	if(shooting || speed <= 0)
		return 1;
	return std::max<int64_t>(1, (static_cast<int64_t>(distance) + speed - 1) / speed);
}

inline int speedAfterSpell(int baseSpeed, int currentSpeed, int bonus, bool slow)
{
	return slow ? static_cast<int>(std::max<int64_t>(1, static_cast<int64_t>(currentSpeed) * (100 + bonus) / 100))
		: baseSpeed + bonus;
}

// Approach terms only; ordered exchange and winner suppression need a forecast.
inline double speedApproachValue(int64_t total, int oldTime, int newTime,
	int rounds, int duration, bool completed, bool slow, bool shooting)
{
	const int turns = duration - (completed ? 1 : 0);
	if(total <= 0 || rounds <= 0 || turns <= 0 || oldTime < 1 || newTime < 1)
		return 0;
	if(slow)
	{
		if(shooting || oldTime > rounds || newTime <= oldTime)
			return 0;
		newTime = std::min(rounds + 1, oldTime + std::min(newTime - oldTime, turns));
		return (rounds - oldTime + 1) * total / rounds - (rounds - newTime + 1) * total / rounds;
	}
	if(newTime > rounds || newTime >= oldTime)
		return 0;
	oldTime = std::min(oldTime, rounds + 1);
	return (rounds - newTime + 1) * total / rounds - (rounds - oldTime + 1) * total / rounds;
}

// VCMI BattleSpellMechanics::isReflected, restricted to hero casting.
inline bool nativeHeroSpellMayReflect(bool massive, bool directRange, bool negative, int mirrorChance)
{
	return !massive && directRange && negative && mirrorChance > 0;
}

inline bool hostileSpellShouldAttackNow(bool killsOnly, bool lastAction,
	bool activeMeleeTarget, bool enemyCanAttack, bool friendlyActsBeforeEnemy)
{
	if(killsOnly || lastAction || activeMeleeTarget)
		return true;
	return enemyCanAttack && !friendlyActsBeforeEnemy;
}

inline int spellPlanningRounds(double friendlyAwake, double enemyAwake)
{
	const double high = std::max(friendlyAwake, enemyAwake);
	const double low = std::min(friendlyAwake, enemyAwake);
	if(high <= 0 || low * 5 <= high)
		return 1;
	constexpr double odds[] = {2.6, 1.9, 1.5, 1.31, 1.2, 1.13};
	int rounds = 1;
	for(double ratio : odds)
	{
		if(high / low >= ratio)
			break;
		++rounds;
	}
	return rounds;
}

inline double spellDurationValue(int duration, int rounds, bool moved)
{
	return std::max(0.0, std::min(1.0, static_cast<double>(duration) / rounds)
		- (moved ? 1.0 / rounds : 0.0));
}

// Pinned getMirthValue and positive-change valueOfLuckAndMorale branch.
inline double mirthSpellValue(double total, int morale, int bonus, double duration)
{
	if(total <= 0 || total > INT32_MAX || morale < -3 || morale >= 3 || (bonus != 1 && bonus != 2))
		return 0;
	bonus = std::min(bonus, 3 - morale);
	const double effect = morale >= 0 ? bonus * 0.0173
		: (morale + bonus) * 0.0173 + morale * -0.0833;
	return std::floor(total * duration * effect);
}

// Pinned getSorrowValue / negative-change valueOfLuckAndMorale arm.
inline double sorrowSpellValue(double total,int morale,int amount,double duration,int successPercent=100)
{
 if(total<=0 || total>INT32_MAX || morale < -3 || morale>3 || morale==-3
  || (amount!=1 && amount!=2) || successPercent<0 || successPercent>100)return 0;
 const int change=std::max(-amount,-3-morale);
 const double delta=morale<=0 ? -change*-0.0833
  : -change<=morale ? change*0.0173 : -morale*0.0173-(morale+change)*-0.0833;
 // Resistance multiplies the effect BEFORE the final source long conversion.
 const double effect=(-delta)*(successPercent/100.0);
 return std::floor(total*duration*effect);
}

// Pinned getMisfortuneValue: luck constants are source FLOAT storage promoted
// to double, not decimal double literals. Resistance precedes truncation.
inline double misfortuneSpellValue(double total,int luck,int amount,double duration,int successPercent=100)
{
 if(!std::isfinite(total) || total<=0 || total>INT32_MAX || luck < -3 || luck>3
  || luck==-3 || (amount!=1 && amount!=2) || !std::isfinite(duration) || duration<0 || duration>1
  || successPercent<0 || successPercent>100)return 0;
 const double good=static_cast<double>(0.0173f),bad=static_cast<double>(-0.0122f);
 const int change=std::max(-amount,-3-luck);
 const double delta=luck<=0 ? -change*bad
  : -change<=luck ? change*good : -luck*good-(luck+change)*bad;
 return std::floor(total*duration*((-delta)*(successPercent/100.0)));
}

// Source getWeaknessValue deliberately prices a POSITIVE hypothetical attack
// bonus via getAttackSkillValue, although the native spell applies a reduction.
inline std::optional<int32_t> weaknessSpellValue(int32_t total,int32_t oldDamage,
 int32_t targetHealth,int32_t oldEstimate,int32_t newEstimate,double duration)
{
 if(total<0 || oldDamage<1 || targetHealth<1 || oldDamage>targetHealth
  || oldEstimate<1 || newEstimate<1 || !std::isfinite(duration) || duration<0 || duration>1)return std::nullopt;
 double increase=double(newEstimate)/oldEstimate;
 const double projected=oldDamage*increase;
 if(!std::isfinite(projected) || projected>INT32_MAX)return std::nullopt;
 int32_t newDamage=static_cast<int32_t>(projected);
 if(newDamage>targetHealth){newDamage=targetHealth;increase=double(targetHealth)/oldDamage;}
 if(newDamage<=oldDamage)return 0;
 const double value=(std::sqrt(increase)-1.0)*total*duration;
 if(!std::isfinite(value) || value>INT32_MAX)return std::nullopt;
 return static_cast<int32_t>(value);
}

// Pinned getFortuneValue: truncate boost FIRST, then multiply strikes/divide24.
inline std::optional<int32_t> fortuneSpellValue(int32_t total,int luck,int amount,
 int32_t damage,int32_t targetHealth,double duration)
{
 if(total<0 || luck < -3 || luck>3 || (amount!=1 && amount!=2) || damage<0
  || targetHealth<=0 || !std::isfinite(duration) || duration<0 || duration>1)return std::nullopt;
 if(total==0 || luck>=3 || amount+luck<=-3)return 0;
 amount=std::min(amount,3-luck);
 const auto boost=[&](int32_t oldDamage)->std::optional<int32_t>
 {
  if(oldDamage>INT32_MAX/2)return std::nullopt;
  int32_t newDamage=oldDamage*2;double increase=2.0;
  if(newDamage>targetHealth){newDamage=targetHealth;increase=double(targetHealth)/oldDamage;}
  if(newDamage<=oldDamage)return 0;
  const double result=(std::sqrt(increase)-1.0)*total*duration;
  if(!std::isfinite(result) || result<0 || result>INT32_MAX)return std::nullopt;
  return static_cast<int32_t>(result);
 };
 int64_t result=0;
 const auto term=[&](int32_t oldDamage,int strikes)->std::optional<int32_t>
 {
  const auto value=boost(oldDamage);if(!value || int64_t(*value)*strikes>INT32_MAX)return std::nullopt;
  return *value*strikes/24;
 };
 if(luck<0){const auto value=term(damage/2,amount+luck>0?-luck:amount);if(!value)return std::nullopt;result=*value;}
 if(amount+luck>0){const auto value=term(damage,luck<0?amount+luck:amount);if(!value)return std::nullopt;result+=*value;}
 if(result>INT32_MAX)return std::nullopt;
 return static_cast<int32_t>(result);
}

inline double spellDamageValue(double totalValue, int64_t health, int64_t damage, bool inactive)
{
	if(health <= 0 || damage <= 0)
		return 0;
	double value = totalValue * static_cast<double>(std::min(health, damage)) / health;
	if(inactive && value < totalValue)
		value = 2 * value - totalValue;
	return value;
}

// Native beforeCast applies resistance only to negative magical effects.
inline int nativeSpellSuccessPercent(bool negative, bool magical, int resistance)
{
	return negative && magical ? 100 - std::clamp(resistance, 0, 100) : 100;
}

// Native resistance rejects the whole effect. Weight the successful
// castEval outcome, including its wake-up penalty, by landing probability.
// Pinned getDamageValue instead discounts raw damage before its casualty cap.
inline double resistanceWeightedProjectedLoss(double totalValue, int64_t health,
	int64_t projectedLoss, bool inactive, int successPercent)
{
	if(projectedLoss <= 0 || health <= 0)
		return 0;
	const int chance = std::clamp(successPercent, 0, 100);
	const int64_t loss = std::min(projectedLoss, health);
	return spellDamageValue(totalValue, health, loss, inactive) * (chance / 100.0);
}

inline double massSpellDamageValue(double enemyDamage, double friendlyDamage,
	double enemyTotal, double friendlyTotal)
{
	friendlyDamage = std::max(0.0, friendlyDamage);
	if(enemyTotal <= 0 || friendlyTotal <= 0 || enemyDamage <= friendlyDamage
		|| friendlyDamage >= friendlyTotal
		|| enemyDamage / enemyTotal <= friendlyDamage / friendlyTotal)
		return 0;
	return enemyDamage - friendlyDamage;
}

inline double manaWeightedSpellValue(double value, int64_t mana, int cost)
{
	if(value <= 0 || cost < 0 || mana < cost)
		return 0;
	// Zero-cost native spells have no retail counterpart.
	if(cost == 0 || mana / cost >= 7)
		return value * 2.5;
	return value * std::sqrt(static_cast<double>(mana / cost));
}

inline double attackSpellValue(double total, double before, double after,
	int64_t targetHealth, double duration)
{
	if(before <= 0 || after <= before)
		return 0;
	after = std::min(after, static_cast<double>(targetHealth));
	if(after <= before)
		return 0;
	return (std::sqrt(after / before) - 1) * total * duration;
}

// getPrayerValue adds the separately truncated defense, speed and attack terms.
inline std::optional<int32_t> prayerComponentValue(double defense, double speed, double attack)
{
	int64_t result = 0;
	for(const double component : {defense, speed, attack})
	{
		if(!std::isfinite(component) || component < 0 || component > INT32_MAX)
			return std::nullopt;
		result += static_cast<int32_t>(component);
		if(result > INT32_MAX) return std::nullopt;
	}
	return static_cast<int32_t>(result);
}

// getDefenseBoostValue: incoming casualties, overkill and unmoved duration.
// An absent winning expected-damage projection preserves the non-winning branch.
inline double defenseSpellValue(double total, int64_t health, int64_t hitPoints,
	int64_t topDamage, double representativeDamage, double increase,
	double incomingDamage, int rounds, double duration,
	std::optional<int64_t> winningExpectedDamage = std::nullopt)
{
	if(health <= 0 || hitPoints <= 0 || rounds <= 0 || representativeDamage <= 0
		|| increase <= 1 || !std::isfinite(increase))
		return 0;
	const double reduced = std::floor(representativeDamage / increase);
	// Zero damage has no finite source ratio. Keep unsupported mod immunity
	// outcomes out of selection instead of admitting an infinite score.
	if(reduced <= 0)
		return 0;
	if(representativeDamage > health)
	{
		increase = health / reduced;
		representativeDamage = static_cast<double>(health);
	}
	if(reduced >= representativeDamage
		|| (winningExpectedDamage && *winningExpectedDamage < hitPoints - topDamage)
		|| incomingDamage * rounds + topDamage < hitPoints)
		return 0;
	return std::floor(duration * total * (std::sqrt(increase) - 1));
}

// getDisruptiveRayValue truncates before and after resistance. Commitment is
// a source getAITarget identity match, without an arrival-time restriction.
// nullopt denotes unsupported arithmetic inputs, not a zero-valued spell.
inline std::optional<int32_t> directDisruptingRaySpellValue(int64_t sourceTotal,
	int masteryBonus, bool friendlyTargetCommitted, bool killsOnly,
	bool checkResistance, double successChance)
{
	if(killsOnly || !friendlyTargetCommitted)
		return 0;
	if(sourceTotal < 0 || sourceTotal > INT32_MAX || masteryBonus < 0 || masteryBonus > 20
		|| (checkResistance && (!std::isfinite(successChance) || successChance < 0 || successChance > 1)))
		return std::nullopt;
	const double total = static_cast<double>(sourceTotal);
	int32_t value = static_cast<int32_t>(total - total * std::sqrt(1.0 - masteryBonus * 0.05));
	if(checkResistance)
		value = static_cast<int32_t>(successChance * value);
	return value;
}

inline bool directCurseSignedValue(int64_t value)
{
	return value >= INT32_MIN && value <= INT32_MAX;
}

inline bool directCurseDamageRangeSupported(int64_t low, int64_t high)
{
	return low >= 1 && high >= low && high <= INT32_MAX - low;
}

inline bool directCurseHealthSupported(int64_t count, int64_t hitPoints, int64_t availableHealth)
{
	return count > 0 && hitPoints > 0 && hitPoints <= INT32_MAX
		&& count <= INT32_MAX / hitPoints && availableHealth > 0
		&& availableHealth <= count * hitPoints;
}

inline std::optional<int32_t> directCurseModifierDifference(int64_t modifier, int64_t minimum)
{
	if(!directCurseSignedValue(modifier) || !directCurseSignedValue(minimum)
		|| modifier < minimum || modifier - minimum > INT32_MAX)
		return std::nullopt;
	return static_cast<int32_t>(modifier - minimum);
}

inline std::optional<int> directCursePlanningRounds(int64_t friendlyAwake, int64_t enemyAwake)
{
	if(friendlyAwake < 0 || enemyAwake < 0 || friendlyAwake > INT32_MAX || enemyAwake > INT32_MAX
		|| std::min(friendlyAwake, enemyAwake) > INT32_MAX / 5)
		return std::nullopt;
	return spellPlanningRounds(friendlyAwake, enemyAwake);
}

inline bool directCurseContextSupported(int64_t friendly, int64_t enemy)
{
	return friendly > 0 && enemy > 0 && friendly <= INT32_MAX / 2
		&& enemy <= INT32_MAX && friendly * 2 >= enemy;
}

// Direct getCurseValue has three separate signed-long conversions. Removal
// retains its existing native net-effect approximation below.
inline int32_t directCurseSpellValue(double total, double before, double after,
	double duration, int successPercent)
{
	if(!std::isfinite(total) || total < 0 || total > INT32_MAX
		|| !std::isfinite(before) || !std::isfinite(after) || before <= 0 || after < 1
		|| after >= before || !std::isfinite(duration) || duration < 0 || duration > 1
		|| successPercent < 0 || successPercent > 100)
		return 0;
	int32_t value = static_cast<int32_t>(total);
	value = static_cast<int32_t>(value - std::sqrt(after / before) * value);
	value = static_cast<int32_t>(value * duration);
	return static_cast<int32_t>((successPercent / 100.0) * value);
}

inline double curseSpellValue(double total, double before, double after, double duration)
{
	if(before <= 0 || after >= before)
		return 0;
	return total * (1 - std::sqrt(std::max(0.0, after) / before)) * duration;
}

inline double removedAttackSpellValue(double total, double before, double after,
	int64_t targetHealth, double duration)
{
	if(after >= before)
		return curseSpellValue(total, after, before, duration);
	if(after <= 0)
		return -total * duration;
	return -attackSpellValue(total, after, before, targetHealth, duration);
}

inline bool resurrectionFitsCasualties(int64_t healable, int64_t dead, int64_t original, bool winLikely = false)
{
	return healable >= 1 && dead >= 1 && original >= 1
		&& (winLikely || healable <= dead || dead >= original * 3 / 4);
}

inline double blindSpellValue(double total, double awakeEnemy, int retaliationPercent, double duration)
{
	if(total >= awakeEnemy)
		return total * (0.5 - std::sqrt(std::clamp(retaliationPercent, 0, 100) / 400.0));
	return total * duration;
}
}
