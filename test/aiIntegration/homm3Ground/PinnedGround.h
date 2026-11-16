// SPDX-License-Identifier: CC0-1.0
#pragma once
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>
namespace PinnedGround
{
constexpr int COMBAT_GRID_CELLS=187, COMBAT_DIRECTION_COUNT=6, COMBAT_GRID_LAST_COLUMN=16;
enum { COMBAT_DIRECTION_0, COMBAT_DIRECTION_1, COMBAT_DIRECTION_2, COMBAT_DIRECTION_3,
 COMBAT_DIRECTION_4, COMBAT_DIRECTION_5, COMBAT_DIRECTION_WIDE_UPPER, COMBAT_DIRECTION_WIDE_LOWER };
enum { creatureDoubleWide=1,creatureSiegeWeapon=2,creatureImmobilized=4,creatureFlyingArmy=8,
 CREATURE_ARROW_TOWER=100,CREATURE_CATAPULT,CREATURE_DEVIL,CREATURE_ARCH_DEVIL,
 DRAWBRIDGE_UP=1,COMBAT_HEX_GATE=95,COMBAT_HEX_GATE_MOAT=94 };
inline int g_moatDamage[16]{};
struct army
{
 int group=0,index=0,m_gridIndex=0,m_facing=0,m_creatureType=0,speed=1;
 bool restricted=false;
 bool is(int) const { return false; }
 bool canShoot(int) const { return false; }
 int getSpeed() const { return speed; }
 int getSpellTime(int id) const { return id==72&&restricted; }
 int getOwningSide() const { return group; }
 int getTotalHitPoints(int) const { return 100; }
 int offsetToFront(int) const { throw std::logic_error("wide excluded"); }
 int getSecondGridIndex() const { throw std::logic_error("wide excluded"); }
 int getAdjacentCellIndex(int,int) const;
 int canFit(int,int,int*) const;
};
struct hexcell
{
 enum { blocked=1 };
 int m_armySide=-1,m_armySlot=-1,m_attributes=0;
 bool m_validMove=false,m_frontMove=false;
 bool hasArmy() const { return m_armySide>=0; }
 army* getArmy() const;
};
struct combatManager
{
 struct Town { int m_type; };
 Town *m_defendingTown=nullptr;
 int m_drawbridgeState=0,m_fortificationLevel=0,m_actingSide=0,m_actingSlot=0;
 std::array<hexcell,187> m_cells;
 army m_armies[2][31]; long m_numArmies[2]{};
 short m_adjacentCells[187][6]{};
 static bool validHex(int h) { return h>=0&&h<187; }
 static int gridX(int h) { return h%17; }
 static int gridY(int h) { return h/17; }
 static bool rowIsOdd(int r) { return r%2!=0; }
 static bool inInvisibleColumn(int h) { return gridX(h)==0||gridX(h)==16; }
 static int getDistance(int,int) { throw std::logic_error("destination excluded"); }
 bool isComputerAction(const army*) const { throw std::logic_error("siege excluded"); }
 bool isOutsidePlacementBoundry(long,int) const { throw std::logic_error("placement excluded"); }
 bool doorCanBeLowered() const { throw std::logic_error("siege excluded"); }
 void setupAdjacencyArray(); bool hexIsBlocked(int) const;
};
inline combatManager manager;
inline combatManager *g_combatManager=&manager;
struct pathCell
{
 struct Point { short m_x,m_y; } m_point,m_lastPoint;
 int m_direction;
 bool m_visited;
 unsigned short m_cost;
 int m_flightCost;
};
struct searchArray
{
 std::array<pathCell,187> cells{};
 pathCell *m_cellData=cells.data();
 std::vector<pathCell> m_queue;
 std::vector<pathCell*> m_result;
 void init() { m_cellData=cells.data(); }
 pathCell * getHex(int i) { return &cells.at(i); }
 const pathCell * getHex(int i) const { return &cells.at(i); }
 void setMoat(const army*) {} // domain rejects every moat/stopping effect
 bool isMoat(int) const { return false; }
 bool buildCombatPath(const army*,int,int,int dest) { if(dest!=-1)throw std::logic_error("destination excluded");return false; }
 void seedCombatPosition(const army*,long,long,bool,long);
 void markEnemy(long,long);
 bool checkEnemyArmies(long,long,long,long);
 bool findCombatPath(const army*,long,long,bool,long,long);
 void pushCombatPoint(int,int,int,int,int);
};
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
// Original six-way switch covers the bounded direction loop.
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "bodies.pinned.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
