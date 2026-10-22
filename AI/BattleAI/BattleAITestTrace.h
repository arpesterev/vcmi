/*
 * BattleAITestTrace.h, part of VCMI experimental tests.
 * GPL-2.0-or-later; see license.txt.
 */
#pragma once

#ifdef VCMI_BATTLE_AI_TEST_TRACE
class HypotheticBattle;
namespace battle { class Unit; }

namespace BattleAITestTrace
{
enum class Kind { Selection, Estimate, Choice };
struct Event
{
	Kind kind;
	const HypotheticBattle * state;
	const battle::Unit * attacker;
	const battle::Unit * target;
	bool shooting;
};
struct Sink
{
	virtual ~Sink() = default;
	virtual void receive(const Event & event) = 0;
};
// This header is included only in vcmitest's directly compiled BattleAI TU and tests.
// It changes no production class layout. Do not enable in a prebuilt BattleAI library.
inline thread_local Sink * sink = nullptr;
inline void emit(const Event & event)
{
	if(sink)
		sink->receive(event);
}
struct ScopedSink
{
	Sink * previous;
	explicit ScopedSink(Sink & next) : previous(sink) { sink = &next; }
	~ScopedSink() { sink = previous; }
	ScopedSink(const ScopedSink &) = delete;
	ScopedSink & operator=(const ScopedSink &) = delete;
};
}
#endif

