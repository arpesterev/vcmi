#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include "ai_battle.h"
#include "army.h"
#include "army_troop.h"
#include "battle_arena.h"
#include "battle_army.h"
#include "battle_board.h"
#include "battle_cell.h"
#include "battle_command.h"
#include "battle_troop.h"
#include "ground.h"
#include "heroes.h"
#include "heroes_base.h"
#include "monster.h"
#include "players.h"
#include "rand.h"
#include "settings.h"
#include "spell.h"
#include "world.h"

namespace {
Rand::PCG32 * battleRng = nullptr;
std::ofstream trace;
uint64_t eventIndex = 0;

void writeRng( std::ostream & out, const Rand::PCG32 & rng )
{
    static_assert( std::is_trivially_copyable_v<Rand::PCG32> );
    static_assert( sizeof( Rand::PCG32 ) == 16 );
    Rand::PCG32 copy = rng;
    unsigned char bytes[sizeof( rng )];
    std::memcpy( bytes, &rng, sizeof( rng ) );
    static const char digits[] = "0123456789abcdef";
    out << "{\"object_bytes_native_endian\":\"";
    for ( unsigned char byte : bytes ) out << digits[byte >> 4] << digits[byte & 15];
    out << "\",\"stream\":" << rng.getStream() << ",\"next4\":[";
    for ( int i = 0; i < 4; ++i ) { if ( i ) out << ','; out << copy(); }
    out << "]}";
}

void writeUnit( std::ostream & out, const Battle::Unit & unit )
{
    out << "{\"uid\":" << unit.GetUID() << ",\"monster\":" << unit.GetID()
        << ",\"count\":" << unit.GetCount() << ",\"hp\":" << unit.GetHitPoints()
        << ",\"head\":" << unit.GetHeadIndex() << ",\"tail\":" << unit.GetTailIndex()
        << ",\"army_color\":" << static_cast<int>( unit.GetArmyColor() )
        << ",\"current_color\":" << static_cast<int>( unit.GetCurrentColor() )
        << ",\"current_control\":" << unit.GetCurrentControl()
        << ",\"speed\":" << unit.GetSpeed() << ",\"attack\":" << unit.GetAttack()
        << ",\"defense\":" << unit.GetDefense() << ",\"shots\":" << unit.GetShots()
        << ",\"valid\":" << ( unit.isValid() ? "true" : "false" ) << ",\"mode_bits\":[";
    bool first = true;
    for ( unsigned i = 0; i < 32; ++i ) {
        const uint32_t bit = uint32_t( 1 ) << i;
        if ( unit.Modes( bit ) ) {
            if ( !first ) out << ',';
            out << "{\"bit\":" << bit << ",\"duration\":" << unit.GetAffectedDuration( bit ) << '}';
            first = false;
        }
    }
    out << "]}";
}

void writeUnits( std::ostream & out, const Battle::Arena & arena )
{
    out << '[';
    bool first = true;
    for ( const Battle::Force * force : { &arena.getAttackingForce(), &arena.getDefendingForce() } ) {
        for ( const auto * unit : force->getUnits() ) {
            if ( !first ) out << ',';
            writeUnit( out, *unit ); first = false;
        }
    }
    out << ']';
}

void writeCommanders( std::ostream & out, const Battle::Arena & arena )
{
    out << '[';
    bool first = true;
    for ( const auto * hero : { arena.getAttackingForce().GetCommander(), arena.getDefendingForce().GetCommander() } ) {
        if ( !first ) out << ',';
        first = false;
        if ( !hero ) { out << "null"; continue; }
        out << "{\"color\":" << static_cast<int>( hero->GetColor() ) << ",\"control\":" << hero->GetControl()
            << ",\"attack\":" << hero->GetAttack() << ",\"defense\":" << hero->GetDefense() << ",\"power\":" << hero->GetPower()
            << ",\"knowledge\":" << hero->GetKnowledge() << ",\"mana\":" << hero->GetSpellPoints() << ",\"spells\":[";
        bool firstSpell = true;
        for ( const auto & spell : hero->getAllSpells() ) { if ( !firstSpell ) out << ','; out << spell.GetID(); firstSpell = false; }
        out << "]}";
    }
    out << ']';
}

void writeCommand( std::ostream & out, const Battle::Command & command )
{
    static const char * names[] = { "MOVE", "ATTACK", "SPELLCAST", "MORALE", "CATAPULT", "TOWER", "RETREAT", "SURRENDER", "SKIP", "TOGGLE_AUTO_COMBAT", "QUICK_COMBAT" };
    out << "{\"type\":\"" << names[static_cast<int>( command.GetType() )] << "\",\"constructor_args\":[";
    bool first = true;
    for ( auto it = command.rbegin(); it != command.rend(); ++it ) { if ( !first ) out << ','; out << *it; first = false; }
    out << "]}";
}

void event( const char * phase, const Battle::Arena & arena, const Battle::Unit * active = nullptr, const Battle::Actions * actions = nullptr,
            const Battle::Command * command = nullptr )
{
    if ( !trace.is_open() ) return;
    trace << "{\"event\":" << eventIndex++ << ",\"phase\":\"" << phase << "\",\"round\":" << arena.GetTurnNumber()
          << ",\"rng\":";
    writeRng( trace, *battleRng );
    trace << ",\"ambient_rng\":"; writeRng( trace, Rand::CurrentThreadRandomDevice() );
    trace << ",\"units\":"; writeUnits( trace, arena );
    trace << ",\"commanders\":"; writeCommanders( trace, arena );
    trace << ",\"board_objects\":[";
    bool firstCell = true;
    for ( const auto & cell : *Battle::Arena::GetBoard() ) {
        if ( !firstCell ) trace << ',';
        trace << cell.GetObject(); firstCell = false;
    }
    trace << ']';
    if ( active ) { trace << ",\"active_uid\":" << active->GetUID(); }
    if ( actions ) {
        trace << ",\"actions\":[";
        bool first = true;
        for ( const auto & action : *actions ) { if ( !first ) trace << ','; writeCommand( trace, action ); first = false; }
        trace << ']';
    }
    if ( command ) { trace << ",\"command\":"; writeCommand( trace, *command ); }
    trace << "}\n";
}
}

#ifdef ORACLE_OBSERVE
extern "C" void __real__ZN2AI13BattlePlanner10BattleTurnERN6Battle5ArenaERKNS1_4UnitERNS1_7ActionsE( AI::BattlePlanner *, Battle::Arena &, const Battle::Unit &, Battle::Actions & );
extern "C" void __wrap__ZN2AI13BattlePlanner10BattleTurnERN6Battle5ArenaERKNS1_4UnitERNS1_7ActionsE( AI::BattlePlanner * self, Battle::Arena & arena, const Battle::Unit & unit, Battle::Actions & actions )
{
    event( "planner_before", arena, &unit );
    __real__ZN2AI13BattlePlanner10BattleTurnERN6Battle5ArenaERKNS1_4UnitERNS1_7ActionsE( self, arena, unit, actions );
    event( "planner_after", arena, &unit, &actions );
}
extern "C" void __real__ZN6Battle5Arena11ApplyActionERNS_7CommandE( Battle::Arena *, Battle::Command & );
extern "C" void __wrap__ZN6Battle5Arena11ApplyActionERNS_7CommandE( Battle::Arena * arena, Battle::Command & command )
{
    event( "action_before", *arena, nullptr, nullptr, &command );
    __real__ZN6Battle5Arena11ApplyActionERNS_7CommandE( arena, command );
    event( "action_after", *arena );
}
#endif

int main( int argc, char ** argv )
{
    const std::string scenario = argc > 1 ? argv[1] : "melee";
    const uint64_t seed = argc > 2 ? std::stoull( argv[2] ) : 123456;
    if ( const char * path = std::getenv( "ORACLE_TRACE" ) ) trace.open( path );
    Rand::CurrentThreadRandomDevice() = Rand::PCG32( seed );
    auto & settings = Settings::Get();
    settings.GetPlayers().Init( PlayerColor::BLUE | PlayerColor::RED );
    Players::SetPlayerControl( PlayerColor::BLUE, CONTROL_AI );
    Players::SetPlayerControl( PlayerColor::RED, CONTROL_AI );
    world.generateBattleOnlyMap( Maps::Ground::GRASS );
    Army neutralAttacker;
    Army defender;
    const bool withHero = scenario == "spell_damage" || scenario == "spell_control" || scenario == "human_auto";
    Heroes * hero = withHero ? world.GetHeroes( Heroes::LORDKILBURN ) : nullptr;
    if ( hero ) {
        if ( !hero->Recruit( PlayerColor::BLUE, { 0, 0 } ) ) throw std::runtime_error( "Fixture hero recruitment failed" );
        hero->setAttackBaseValue( 1 );
        hero->setDefenseBaseValue( 1 );
        hero->setPowerBaseValue( 4 );
        hero->setKnowledgeBaseValue( 10 );
        hero->SpellBookDeactivate();
        hero->SpellBookActivate();
        hero->AppendSpellToBook( Spell( scenario == "spell_control" ? Spell::HYPNOTIZE : Spell::LIGHTNINGBOLT ), true );
        hero->SetSpellPoints( 100 );
        hero->GetArmy().Reset();
        // Recruit while AI-controlled; then exercise the engine's no-interface human auto-combat route.
        if ( scenario == "human_auto" ) Players::SetPlayerControl( PlayerColor::BLUE, CONTROL_HUMAN );
    }
    Army & attacker = hero ? hero->GetArmy() : neutralAttacker;
    attacker.SetColor( PlayerColor::BLUE );
    defender.SetColor( PlayerColor::RED );
    if ( scenario == "melee" ) {
        attacker.GetTroop( 0 )->Set( Monster::PEASANT, 40 );
        defender.GetTroop( 0 )->Set( Monster::GOBLIN, 15 );
    }
    else if ( scenario == "archers" ) {
        attacker.GetTroop( 0 )->Set( Monster::ARCHER, 12 );
        defender.GetTroop( 0 )->Set( Monster::GOBLIN, 30 );
    }
    else if ( scenario == "mixed" ) {
        attacker.GetTroop( 0 )->Set( Monster::ARCHER, 12 );
        attacker.GetTroop( 1 )->Set( Monster::PIKEMAN, 10 );
        defender.GetTroop( 0 )->Set( Monster::ORC, 15 );
        defender.GetTroop( 1 )->Set( Monster::GOBLIN, 30 );
    }
    else if ( scenario == "flying" ) {
        attacker.GetTroop( 0 )->Set( Monster::ARCHER, 20 );
        defender.GetTroop( 0 )->Set( Monster::GARGOYLE, 14 );
    }
    else if ( scenario == "wide" ) {
        attacker.GetTroop( 0 )->Set( Monster::CAVALRY, 8 );
        defender.GetTroop( 0 )->Set( Monster::WOLF, 16 );
    }
    else if ( withHero ) {
        attacker.GetTroop( 0 )->Set( Monster::ARCHER, 15 );
        defender.GetTroop( 0 )->Set( Monster::GOBLIN, 30 );
        defender.GetTroop( 1 )->Set( Monster::ORC, 12 );
    }
    else throw std::invalid_argument( "Unknown scenario" );
    Rand::PCG32 rng( seed );
    battleRng = &rng;
    Battle::Arena arena( attacker, defender, 0, false, rng );
    event( "initial", arena );
    for ( int round = 0; round < 1000 && arena.BattleValid(); ++round ) arena.Turns();
    if ( arena.BattleValid() ) throw std::runtime_error( "Battle did not finish in 1000 rounds" );
    const auto & result = arena.GetResult();
    std::cout << "{\"scenario\":\"" << scenario << "\",\"seed\":" << seed << ",\"map_seed\":" << world.GetMapSeed()
              << ",\"rounds\":" << arena.GetTurnNumber() << ",\"attacker_result\":" << result.attacker
              << ",\"defender_result\":" << result.defender << ",\"attacker_xp\":" << result.attackerExperience
              << ",\"defender_xp\":" << result.defenderExperience << ",\"units\":";
    writeUnits( std::cout, arena );
    std::cout << ",\"commanders\":"; writeCommanders( std::cout, arena );
    std::cout << ",\"rng\":"; writeRng( std::cout, rng );
    std::cout << ",\"ambient_rng\":"; writeRng( std::cout, Rand::CurrentThreadRandomDevice() );
    std::cout << "}\n";
    event( "final", arena );
}
