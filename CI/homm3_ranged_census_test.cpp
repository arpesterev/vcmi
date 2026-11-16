#include "../AI/Integration/Ports/HoMM3RangedCensus.h"
#include "../test/aiIntegration/homm3Exchange/PinnedExchange.h"
#include <iostream>
#include <stdexcept>
namespace
{
using namespace AIIntegration::Ports::HoMM3;
namespace Ref= PinnedExchange;
int checks=0;
void require(bool ok,const char *message) { ++checks;if(!ok) throw std::runtime_error(message); }
SourceOrdinaryUnit unitFacts(const Ref::army & unit)
{
	SourceOrdinaryUnit out;
	out.count=unit.m_numTroops;out.hitPoints=unit.m_monInfo.hitPoints;out.topWound=unit.m_topCreatureDamage;
	out.minimumDamage=unit.m_monInfo.damageLowBound;out.maximumDamage=unit.m_monInfo.damageHighBound;
	out.canShoot=unit.canShoot(0);out.doubleAttack=unit.is(Ref::creatureTwoAttacks);
	for(int channel=0;channel<2;++channel) out.lossUnitValue[channel]=unit.getUnitCombatValue(0,0,channel!=0,nullptr);
	return out;
}
SourceOrdinaryAttack shotFacts(const Ref::army & a,const Ref::army & b)
{
	SourceOrdinaryAttack out;
	out.attack=a.getAdjustedAttack(&b,true);out.defense=b.getAdjustedDefense(&a,true);
	out.hasController=a.controller!=nullptr;
	out.controllerSkillFactor=a.controller?a.controller->getArcheryFactor():0;
	out.wallPenalty=Ref::manager.wallPenalty;out.distancePenalty=Ref::manager.distancePenalty;
	out.defenderReduction=b.computeDefenderDamageReduction(true);
	return out;
}
}
int main()
{
	Ref::hero hero;
	hero.m_skillLevel[Ref::eSecSkillArchery]=1;
	for(int side : {0,1})
	for(int n=0;n<=7;++n)
	for(int filter=0;filter<12;++filter)
	for(int speed : {0,1,6})
	for(bool simulated : {false,true})
	for(bool killsOnly : {false,true})
	{
		Ref::manager.m_numArmies[side]=1;Ref::manager.m_numArmies[1-side]=n;
		auto &a=Ref::manager.m_armies[side][0];a={};a.index=0;a.group=side;
		a.m_numTroops=17;a.m_monInfo.hitPoints=11;a.m_topCreatureDamage=5;
		a.m_monInfo.damageLowBound=2;a.m_monInfo.damageHighBound=7;
		a.m_monInfo.attackSkill=11;a.speed=speed;a.shooting=true;
		a.flags=Ref::creatureShootingArmy|Ref::creatureTwoAttacks;a.controller=&hero;
		if(filter==1)a.flags|=Ref::creatureImmobilized;
		if(filter==2)a.flags|=Ref::creatureSiegeWeapon;
		if(filter==3)a.m_spellInfluence[70]=1;
		if(filter==4)a.m_spellInfluence[70]=2;
		if(filter==5)a.m_spellInfluence[62]=1;
		if(filter==6)a.m_spellInfluence[62]=2;
		if(filter==7)a.m_spellInfluence[60]=1;
		if(filter==8)a.m_spellInfluence[59]=1;
		if(filter==11)a.AI_expected_damage=a.getTotalHitPoints(0);
		Ref::manager.wallPenalty=(n%2)==0;Ref::manager.distancePenalty=(n%3)==0;
		Ref::type_AI_combat_parameters params;
		params.m_ourGroup=side;params.m_simulated=simulated;params.m_killsOnly=killsOnly;
		SourceRangedCensusInput input;
		input.actor=unitFacts(a);input.speed=speed;input.immobilized=filter==1;input.siegeWeapon=filter==2;
		input.stoneGazeTurns=a.m_spellInfluence[70];input.blindTurns=a.m_spellInfluence[62];
		input.hypnotizeTurns=a.m_spellInfluence[60];input.berserkTurns=a.m_spellInfluence[59];
		input.currentArmyExcluded=filter==9;input.meleeOnly=filter==10;
		input.simulated=simulated;input.killsOnly=killsOnly;input.expectedDamage=a.AI_expected_damage;
		input.enemies=std::vector<SourceRangedCandidate>{};
		for(int j=0;j<n;++j)
		{
			auto &enemy=Ref::manager.m_armies[1-side][j];enemy={};enemy.index=j;enemy.group=1-side;
			enemy.m_creatureType=j==2?Ref::CREATURE_ARROW_TOWER:1;
			enemy.m_numTroops=2+j;enemy.m_monInfo.hitPoints=9;enemy.m_topCreatureDamage=j;
			enemy.m_monInfo.defenseSkill=j*8;
			if(j==4)enemy.AI_expected_damage=enemy.getTotalHitPoints(0);
			SourceRangedCandidate candidate;
			candidate.rosterIndex=j;candidate.arrowTower=j==2;
			candidate.unit=unitFacts(enemy);candidate.attack=shotFacts(a,enemy);candidate.expectedDamage=enemy.AI_expected_damage;
			input.enemies->push_back(candidate);
			if(!candidate.arrowTower && (!simulated || (a.getTotalHitPoints(true)>0 && enemy.getTotalHitPoints(true)>0)))
			{
				SourceOrdinaryExchangeInput exchange;
				exchange.attacker=input.actor;exchange.defender=*candidate.unit;exchange.attack[1]=*candidate.attack;
				exchange.ranged=true;exchange.killsOnly=killsOnly;exchange.simulated=simulated;
				exchange.attackerExpectedDamage=a.AI_expected_damage;exchange.defenderExpectedDamage=enemy.AI_expected_damage;
				const auto actual=evaluateSourceOrdinaryExchange(exchange);
				require(actual && actual->value==params.getSimpleAttackEffect(a,enemy,true,speed==0?0:1),
					"Every ranged candidate score matches full pinned exchange chain");
			}
		}
		Ref::manager.findAITargets(side,filter==9?&a:nullptr,filter==10,&params,nullptr);
		const auto actual=produceSourceRangedCensus(input);
		require(actual.has_value(),"Mapped complete source shot row is available");
		require(actual->possibleTargets==a.AI_possible_targets,"Possible-target mask preserves original skipped slot IDs");
		require(actual->target.has_value()==(a.AI_target!=nullptr),"Source filtering produces known empty vs chosen row");
		if(a.AI_target)
		{
			require(actual->target->rosterIndex==a.AI_target->index,"Full source findAITargets selects same multi-peer target");
			require(actual->target->exchangeValue==a.AI_target_value,"Chosen source exchange value is retained");
			require(actual->target->attackDistance==a.AI_target_distance && actual->target->attackTime==a.getAITargetTime(),
				"Shot census retains source time=1 including zero-speed actor");
		}
	}
	SourceRangedCensusInput base;
	base.actor.canShoot=true;base.speed=5;
	base.enemies=std::vector<SourceRangedCandidate>{};
	SourceRangedCandidate candidate;candidate.unit=SourceOrdinaryUnit{};candidate.attack=SourceOrdinaryAttack{};
	base.enemies->push_back(candidate);
	candidate.rosterIndex=1;base.enemies->push_back(candidate);
	const auto row=produceSourceRangedCensus(base);
	require(row && row->target && row->target->rosterIndex==0,"Equal-valued ordinary peers keep first original slot");
	{
		auto input=base;input.enemies->at(1).attack.reset();
		require(!produceSourceRangedCensus(input),"Unknown second candidate invalidates whole row, not just itself");
		input.enemies->at(1).arrowTower=true;
		require(produceSourceRangedCensus(input).has_value(),"Filtered tower does not require damage facts");
	}
	{
		auto input=base;input.enemies.reset();
		require(!produceSourceRangedCensus(input),"Missing full roster is unknown, never known empty");
		input.immobilized=true;
		const auto empty=produceSourceRangedCensus(input);
		require(empty && !empty->target,"Source-filtered actor is known empty without a peer producer");
	}
	{
		auto input=base;input.actor.canShoot=false;
		require(!produceSourceRangedCensus(input),"Ground path absent is unavailable, not an empty row");
		input.actor.canShoot=true;input.ballista=true;
		require(!produceSourceRangedCensus(input),"Special Ballista exchange is outside ordinary producer");
	}
	for(int bad : {-1,2,31})
	{
		auto input=base;input.enemies->at(1).rosterIndex=bad;
		require(!produceSourceRangedCensus(input),"Original roster cannot omit, reorder or overflow a slot");
	}
	{
		auto input=base;for(auto &peer:*input.enemies)peer.unit->count=0;
		const auto zero=produceSourceRangedCensus(input);
		require(zero && zero->target && zero->target->rosterIndex==0 && zero->target->exchangeValue==0,
			"Known zero-valued peer still becomes the first source target");
		input.simulated=true;
		const auto empty=produceSourceRangedCensus(input);
		require(empty && !empty->target && empty->possibleTargets==0,"Simulated zero-HP peers are filtered without renumbering");
	}
	{
		auto input=base; input.speed=INT32_MAX;
		require(!produceSourceRangedCensus(input), "Source distance+speed first-addition overflow invalidates a shot row");
		input.speed=INT32_MAX-1;
		require(produceSourceRangedCensus(input).has_value(), "Neighboring source-safe distance+speed remains available");
	}
	require(sourceTargetCommitted(0,{std::nullopt,row})==true,"Known positive witness overrides unrelated unknown rows");
	require(!sourceTargetCommitted(1,{std::nullopt,row}).has_value(),"Missing witness plus an unknown row is unknown");
	require(sourceTargetCommitted(1,{row,SourceTargetCensus{}})==false,"Negative commitment requires all rows known");
	require(sourceTargetCommitted(0,{SourceTargetCensus{}})==false,"Known empty roster has no commitment");
	require(!sourceTargetCommitted(31,{row}),"Invalid target identity is unavailable");
	std::cout<<checks<<" HoMM3 complete ranged-row source checks passed\n";
}
