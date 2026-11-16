#include "../AI/Integration/Ports/HoMM3OrdinaryExchange.h"
#include "../test/aiIntegration/homm3Exchange/PinnedExchange.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace AIIntegration::Ports::HoMM3;
namespace Ref = PinnedExchange;
int checks=0;
void require(bool ok,const char *message)
{
	++checks;
	if(!ok) throw std::runtime_error(message);
}
SourceOrdinaryUnit mapUnit(const Ref::army & unit)
{
	SourceOrdinaryUnit result;
	result.count=unit.m_numTroops;
	result.hitPoints=unit.m_monInfo.hitPoints;
	result.topWound=unit.m_topCreatureDamage;
	result.minimumDamage=unit.m_monInfo.damageLowBound;
	result.maximumDamage=unit.m_monInfo.damageHighBound;
	result.blessed=unit.m_spellInfluence[41]!=0;
	result.cursed=unit.m_spellInfluence[42]!=0;
	result.blessAmount=unit.m_blessAmount;
	result.curseAmount=unit.m_curseAmount;
	result.canShoot=unit.canShoot(0);
	result.doubleAttack=unit.is(Ref::creatureTwoAttacks);
	// Independent pinned value chain supplies source loss-value inputs. These
	// values are deliberately not production unitCombatValue or native estimates.
	for(int channel=0;channel<2;++channel)
		result.lossUnitValue[channel]=unit.getUnitCombatValue(0,0,channel!=0,nullptr);
	return result;
}
SourceOrdinaryAttack mapAttack(const Ref::army & attacker,const Ref::army & defender,bool shot)
{
	SourceOrdinaryAttack result;
	result.attack=attacker.getAdjustedAttack(&defender,shot);
	result.defense=defender.getAdjustedDefense(&attacker,true);
	result.lucky=attacker.m_luckStatus>0;
	result.hasController=attacker.controller!=nullptr;
	result.controllerSkillFactor=!attacker.controller ? 0 : shot
		? attacker.controller->getArcheryFactor() : attacker.controller->getOffenseFactor();
	result.wallPenalty=Ref::manager.wallPenalty;
	result.distancePenalty=Ref::manager.distancePenalty;
	result.meleeShooterPenalty=attacker.is(Ref::creatureShootingArmy) && !attacker.is(Ref::creatureNoMeleePenalty);
	result.defenderReduction=defender.computeDefenderDamageReduction(shot);
	return result;
}
SourceOrdinaryExchangeInput mapExchange(const Ref::army & a,const Ref::army & b,const Ref::type_AI_combat_parameters & params,bool ranged)
{
	SourceOrdinaryExchangeInput input;
	input.attacker=mapUnit(a); input.defender=mapUnit(b);
	input.attack={mapAttack(a,b,false),mapAttack(a,b,true)};
	input.retaliation=mapAttack(b,a,false);
	input.ranged=ranged;
	input.canRetaliate=b.retaliates;
	input.retaliationPolicy=Ref::game.m_setup.m_difficulty>0 || Ref::manager.IsHuman[params.m_ourGroup];
	input.killsOnly=params.m_killsOnly;
	input.simulated=params.m_simulated;
	input.attackerExpectedDamage=a.AI_expected_damage;
	input.defenderExpectedDamage=b.AI_expected_damage;
	return input;
}
void compare(Ref::army & a,Ref::army & b,const Ref::type_AI_combat_parameters & params,bool ranged)
{
	const auto input=mapExchange(a,b,params,ranged);
	const auto actual=evaluateSourceOrdinaryExchange(input);
	require(actual.has_value(),"Bounded ordinary source exchange must be available");
	const auto expected=params.getSimpleAttackEffect(a,b,ranged,1);
	if(actual->value!=expected)
	{
		std::cerr << "Value mismatch check=" << checks << " actual=" << actual->value << " oracle=" << expected
			<< " requestedRanged=" << ranged << " canShoot=" << a.shooting << '\n';
		throw std::runtime_error("Exchange differs from exact pinned damage/loss helper chain");
	}
	++checks;
	long ours=a.getTotalHitPoints(0),theirs=b.getTotalHitPoints(0);
	if(params.m_simulated)
	{
		ours=std::max(0L,ours-a.AI_expected_damage);
		theirs=std::max(0L,theirs-b.AI_expected_damage);
	}
	if(ours>0 && theirs>0)
	{
		params.simulateAttack(a,ours,b,theirs,ranged,1);
		require(actual->attackerHealth==ours && actual->defenderHealth==theirs,
			"Survivor HP matches source first-hit/retaliation/second-hit sequence");
	}
}
}
int main()
{
	Ref::akCreatureTypeTraits[0].baseFightValue=37;
	Ref::akCreatureTypeTraits[1].baseFightValue=103;
	Ref::hero own,enemy;
	enemy.m_id=1;
	uint32_t seed=0x7357ab19;
	const auto pick=[&](uint32_t size){ seed=seed*1664525u+1013904223u; return (seed>>8)%size; };
	for(int scenario=0;scenario<24000;++scenario)
	{
		Ref::army a,b;
		b.m_creatureType=1;
		for(auto *unit : {&a,&b})
		{
			unit->m_numTroops=1+pick(35);
			unit->m_monInfo.hitPoints=1+pick(40);
			unit->m_topCreatureDamage=pick(unit->m_monInfo.hitPoints);
			unit->m_monInfo.damageLowBound=1+pick(8);
			unit->m_monInfo.damageHighBound=unit->m_monInfo.damageLowBound+pick(9);
			unit->m_monInfo.attackSkill=pick(95);
			unit->m_monInfo.defenseSkill=pick(95);
			unit->flags=(pick(2)?Ref::creatureShootingArmy:0)|(pick(2)?Ref::creatureTwoAttacks:0)
				|(pick(2)?Ref::creatureNoMeleePenalty:0);
			unit->shooting=unit->is(Ref::creatureShootingArmy) && pick(2);
			unit->m_luckStatus=pick(2);
			unit->retaliates=pick(2);
			const int spell=pick(3);
			unit->m_spellInfluence[41]=spell==1;
			unit->m_spellInfluence[42]=spell==2;
			unit->m_blessAmount=pick(2);
			unit->m_curseAmount=pick(2);
			unit->m_spellInfluence[27]=pick(2);
			unit->m_spellInfluence[28]=pick(2);
			unit->m_shieldFactor=pick(2)?0.7:0.85;
			unit->m_airShieldFactor=pick(2)?0.5:0.75;
			unit->AI_expected_damage=pick(unit->getTotalHitPoints(0)+2);
		}
		for(auto *hero : {&own,&enemy})
		{
			for(int skill=0;skill<3;++skill) hero->m_skillLevel[skill]=pick(4);
			for(auto &&artifact : hero->artifacts) artifact=pick(2);
		}
		a.controller=pick(2)?&own:nullptr;
		b.controller=pick(2)?&enemy:nullptr;
		Ref::manager.wallPenalty=pick(2);
		Ref::manager.distancePenalty=pick(2);
		Ref::manager.IsHuman[0]=pick(2);
		Ref::game.m_setup.m_difficulty=pick(2);
		Ref::type_AI_combat_parameters params;
		params.m_killsOnly=pick(2);
		params.m_simulated=pick(2);
		compare(a,b,params,pick(2));
	}

	// Arithmetic and control-flow boundaries are checked without running a
	// host-long oracle outside the original source signed-32 domain.
	SourceOrdinaryExchangeInput bounded;
	bounded.attacker.hitPoints=bounded.defender.hitPoints=10;
	bounded.attacker.count=bounded.defender.count=10;
	bounded.attacker.minimumDamage=bounded.attacker.maximumDamage=2;
	bounded.defender.minimumDamage=bounded.defender.maximumDamage=2;
	require(evaluateSourceOrdinaryExchange(bounded).has_value(),"Ordinary baseline is available");
	for(int bad : {-1,0})
	{
		auto input=bounded; input.attacker.hitPoints=bad;
		require(!evaluateSourceOrdinaryExchange(input),"Invalid creature HP is unavailable");
	}
	for(double invalid : {-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
	{
		auto input=bounded;input.defender.lossUnitValue[0]=invalid;
		require(!evaluateSourceOrdinaryExchange(input),"Invalid source loss-value facts are unavailable");
	}
	{
		auto input=bounded;input.attacker.count=INT32_MAX;input.attacker.hitPoints=2;
		require(!evaluateSourceOrdinaryExchange(input),"Source live-HP multiplication overflow is unavailable");
	}
	{
		auto input=bounded;input.attacker.count=INT32_MAX;input.attacker.hitPoints=1;
		require(!evaluateSourceOrdinaryExchange(input),"Source ceil first-addition overflow is unavailable");
	}
	{
		auto input=bounded;input.attacker.minimumDamage=1;input.attacker.maximumDamage=INT32_MAX;
		require(!evaluateSourceOrdinaryExchange(input),"Source damage-range addition overflow is unavailable");
	}
	{
		auto input=bounded;input.attacker.minimumDamage=input.attacker.maximumDamage=100000000;
		input.attack[0].attack=60;
		require(!evaluateSourceOrdinaryExchange(input),"Source attacker bonus overflow is unavailable");
	}
	{
		auto input=bounded;input.ranged=true;input.attacker.canShoot=true;input.attacker.doubleAttack=true;
		input.attacker.count=1;input.attacker.minimumDamage=input.attacker.maximumDamage=600000000;
		input.attack[1].lucky=true;
		require(!evaluateSourceOrdinaryExchange(input),"Source doubled shot overflow is checked before live-HP cap");
	}
	{
		auto input=bounded;input.attack[0].defenderReduction=0;
		const auto result=evaluateSourceOrdinaryExchange(input);
		require(result && result->defenderHealth==99,"Source minimum-one damage survives full reduction");
	}
	{
		Ref::army a,b;Ref::hero controller;
		a.controller=&controller; a.m_numTroops=1;a.m_monInfo.hitPoints=1;
		a.m_monInfo.damageLowBound=a.m_monInfo.damageHighBound=5592407;
		a.m_monInfo.attackSkill=60;
		b.m_creatureType=1;b.m_numTroops=100000000;b.m_monInfo.hitPoints=10;
		Ref::manager.wallPenalty=Ref::manager.distancePenalty=false;
		Ref::type_AI_combat_parameters params;
		compare(a,b,params,false);
		const auto withController=evaluateSourceOrdinaryExchange(mapExchange(a,b,params,false));
		a.controller=nullptr;
		const auto without=evaluateSourceOrdinaryExchange(mapExchange(a,b,params,false));
		require(withController && without && withController->defenderHealth!=without->defenderHealth,
			"A zero-skill controller still rounds the prior integer attack bonus through float");
	}
	std::cout << checks << " HoMM3 ordinary exchange differential checks passed\n";
}
