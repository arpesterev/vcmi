#include "../AI/Integration/Ports/HoMM3PrayerRemoval.h"
#include "../test/aiIntegration/homm3PrayerRemoval/PinnedRemoval.h"
#include <iostream>
#include <random>
using namespace AIIntegration::Ports::HoMM3;
int main(){try{std::mt19937 rng(3626);size_t count=0;
 for(int i=0;i<20000;++i){PrayerRemovalFacts f;f.singlePrayerRemoved=f.cleanedBaselineKnown=f.ownerContextKnown=true;f.friendly=i%2;f.ownerWinLikely=i%7==0;f.completed=i%3==0;f.mastery=i%4;f.duration=1+rng()%10;f.rounds=1+rng()%7;f.total=1+rng()%10000;f.hitPoints=10+rng()%100;f.topDamage=rng()%f.hitPoints;f.health=f.hitPoints*(1+rng()%20)-f.topDamage;f.expectedDamage=rng()%(f.health+1);f.cachedIncoming=rng()%200;f.hasWorstEnemy=i%4!=0;f.hasTarget=i%5!=0;f.defenseAverageDamage=10+rng()%100;f.defenseEstimatedBefore=20+rng()%100;f.defenseEstimatedBoosted=f.defenseEstimatedBefore*(0.15+double(rng()%850)/1000);f.targetHealth=10+rng()%500;f.attackAverageDamage=10+rng()%100;f.attackEstimatedBefore=20+rng()%100;f.attackEstimatedBoosted=f.attackEstimatedBefore*(1.0+double(rng()%2000)/1000);f.oldSpeed=1+rng()%20;f.newSpeed=1+rng()%20;f.targetSpeed=rng()%25;f.oldTime=1+rng()%8;f.newTime=1+rng()%f.oldTime;f.moveOrderValue=int(rng()%400)-100;
 const auto got=singlePrayerRemovalValue(f);if(!got||*got!=PinnedPrayerRemoval::evaluate(f))throw std::runtime_error("source parity case "+std::to_string(i));if(PinnedPrayerRemoval::evaluate(f,true)!=0)throw std::runtime_error("Cure removed positive Prayer");++count;
 }
 PrayerRemovalFacts zeroReduced;zeroReduced.singlePrayerRemoved=zeroReduced.cleanedBaselineKnown=zeroReduced.ownerContextKnown=true;zeroReduced.duration=zeroReduced.rounds=1;zeroReduced.health=zeroReduced.hitPoints=10;zeroReduced.total=100;zeroReduced.hasWorstEnemy=true;zeroReduced.cachedIncoming=10;zeroReduced.defenseAverageDamage=5;zeroReduced.defenseEstimatedBefore=100;zeroReduced.defenseEstimatedBoosted=1;
 const auto finite=singlePrayerRemovalValue(zeroReduced);if(!finite||*finite!=900||*finite!=PinnedPrayerRemoval::evaluate(zeroReduced))throw std::runtime_error("finite zero reduced damage");
 zeroReduced.defenseAverageDamage=11;if(singlePrayerRemovalValue(zeroReduced))throw std::runtime_error("overkill zero divisor must reject");
 PrayerRemovalFacts bad;bad.singlePrayerRemoved=bad.cleanedBaselineKnown=bad.ownerContextKnown=true;bad.duration=1;bad.rounds=1;bad.health=10;bad.hitPoints=10;
 if(!singlePrayerRemovalValue(bad)||*singlePrayerRemovalValue(bad)!=0)throw std::runtime_error("zero source components");
 for(int k=0;k<8;++k){auto b=bad;switch(k){case 0:b.singlePrayerRemoved=false;break;case 1:b.cleanedBaselineKnown=false;break;case 2:b.ownerContextKnown=false;break;case 3:b.duration=0;break;case 4:b.mastery=4;break;case 5:b.topDamage=10;break;case 6:b.expectedDamage=INT32_MAX;b.topDamage=1;break;case 7:b.cachedIncoming=INT32_MAX;b.rounds=2;break;}if(singlePrayerRemovalValue(b))throw std::runtime_error("invalid input accepted");}
 std::cout<<count<<" Prayer cancellation source cases\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
