/* Ordinary single-hex source path publication and predecessor order.
 * sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21, CC0-1.0. */
#pragma once
#include "../HoMM3GroundPath.h"
#include <vector>

namespace HoMM3Forecast
{
struct OrdinaryGroundPath
{
 std::array<bool,187> visited{},validMove{};
 std::array<int,187> cost{},direction{};
 std::vector<int> steps; // Destination to start, excluding start.
};
inline std::optional<OrdinaryGroundPath> ordinaryGroundPath(
 const AIIntegration::Ports::HoMM3::SourceGroundSearchInput & in,int limit,int baseSpeed,int destination=-1)
{
 namespace G=AIIntegration::Ports::HoMM3::GroundPathDetail;
 if(!AIIntegration::Ports::HoMM3::searchSourceGround(in)||limit<0||limit>127||baseSpeed<0||baseSpeed>126
  ||(destination!=-1&&!G::available(destination)))return std::nullopt;
 if(!baseSpeed||in.movementRestricted)limit=0;
 OrdinaryGroundPath out;
 struct Point {int hex,cost,direction;};std::vector<Point> queue;
 const auto push=[&](int hex,int direction,int cost){
  if(cost>limit||(out.visited[hex]&&out.cost[hex]<=cost))return;
  int first=0,last=static_cast<int>(queue.size()),middle;
  for(;;){middle=(first+last)/2;if(last<=first)break;if(cost<queue[middle].cost)first=middle+1;else last=middle;}
  queue.insert(queue.begin()+middle,Point{hex,cost,direction});out.visited[hex]=true;out.cost[hex]=cost;out.direction[hex]=direction;
 };
 push(in.head,4,0);
 bool reached=false;
 while(!queue.empty()){
  const auto current=queue.back();queue.pop_back();
  if(current.hex==destination){reached=true;break;}
  const auto adjacent=G::adjacent(current.hex);
  for(int d=0;d<6;++d){const int hex=adjacent[d];if(hex<0)continue;const auto & cell=in.map[hex];
   if(cell.blocked||(cell.occupant&&cell.occupant!=in.actor)){
    if(cell.occupant&&cell.occupant->side!=in.actor.side&&(!out.validMove[hex]||out.cost[hex]>current.cost)){
     out.validMove[hex]=true;out.cost[hex]=current.cost;
    }
    continue;
   }
   push(hex,d,current.cost+1);
  }
 }
 for(int hex=0;hex<187;++hex)if(out.visited[hex]&&out.cost[hex]<=baseSpeed)out.validMove[hex]=true;
 if(reached){int hex=destination;while(hex!=in.head){
  if(out.steps.size()>=187||!out.visited[hex])return std::nullopt;
  out.steps.push_back(hex);hex=G::adjacent(hex)[(out.direction[hex]+3)%6];if(hex<0)return std::nullopt;
 }}
 return out;
}
}
