#include "../AI/Integration/Ports/HoMM3MassPrayerRemoval.h"
#include "../AI/Integration/Ports/HoMM3PrayerRemoval.h"
#include "../test/aiIntegration/homm3MassPrayer/PinnedMass.h"
#include <iostream>
#include <random>
using namespace AIIntegration::Ports::HoMM3;
int main(){try{std::mt19937 rng(3701);size_t cases=0;
for(int run=0;run<10000;++run){PinnedMassPrayer::Manager manager;std::vector<int32_t>values[2];
 for(int side=0;side<2;++side)for(unsigned i=0,n=1+rng()%7;i<n;++i){PrayerRemovalFacts f;f.singlePrayerRemoved=f.cleanedBaselineKnown=f.ownerContextKnown=true;f.friendly=side==0;f.mastery=rng()%4;f.duration=1+rng()%10;f.rounds=1+rng()%7;f.health=500;f.hitPoints=10;f.total=100+rng()%10000;f.expectedDamage=rng()%500;f.ownerWinLikely=rng()%2;f.hasWorstEnemy=true;f.cachedIncoming=100;f.defenseAverageDamage=20+rng()%80;f.defenseEstimatedBefore=100;f.defenseEstimatedBoosted=30+rng()%60;
 const auto value=singlePrayerRemovalValue(f);if(!value)throw std::logic_error("valid scalar rejected");values[side].push_back(*value);manager.m_armies[side].push_back({f,true,true});}
 const auto actual=sumPrayerRemovalValues(values[0],values[1]);if(!actual||*actual!=PinnedMassPrayer::evaluate(manager))throw std::logic_error("mass exact caller mismatch");++cases;}
 const std::vector<int32_t>empty,negative={-1},positive={1},overflowPlus={INT32_MAX,1},overflowMinus={INT32_MIN,-1};
 if(sumPrayerRemovalValues(positive,empty)||sumPrayerRemovalValues(empty,negative)||sumPrayerRemovalValues(empty,overflowPlus)||sumPrayerRemovalValues(overflowMinus,empty))throw std::logic_error("invalid signs/overflow admitted");
 if(sumPrayerRemovalValues(negative,positive)!=std::optional<int32_t>(0))throw std::logic_error("friendly cancellation lost");
 std::cout<<cases<<" exact mass caller/scalar cases; independent target copies; signed side totals\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
