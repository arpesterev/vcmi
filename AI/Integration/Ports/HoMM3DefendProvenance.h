// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <utility>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace AIIntegration::Ports::HoMM3
{
/// Pure policy state. Native adapter must establish EVERY clause from public
/// canonical records + uninterrupted authoritative events. Defaults are unknown.
struct DefendOrdinaryDomain
{
 bool publicCanonicalRoster = false;
 // Conjunction of audited host notification semantics, explicit read-only
 // calculator capability, no applicable whole-roster/conditional reaction paths,
 // spell-free observed interval and uninterrupted state continuity. This does
 // NOT require absence of Lua/scripts and does NOT certify public-information access.
 bool noUnmodeledMutationPaths = false;
 bool noAttackMoveTurnDeathProcs = false;
 bool staticObstacleHistory = false;
 bool noConditionalOrDynamicStats = false;
 bool noSpecialActivation = false;
 bool ordinaryOpenField = false;
 bool established() const
 {
  return publicCanonicalRoster && noUnmodeledMutationPaths
   && noAttackMoveTurnDeathProcs && staticObstacleHistory
   && noConditionalOrDynamicStats && noSpecialActivation && ordinaryOpenField;
 }
};

struct DefendUnitFacts
{
 uint32_t id = 0;
 // Adapter-issued identity generation, never a pointer address or count.
 uint64_t identity = 0;
 bool canonical = false, alive = false;
 int64_t underlyingDefense = -1, displayedDefense = -1;
 // Exact ordered normalized public record identity/content, not a hash.
 // Contains all defense dependencies, excluding ONLY the canonical stance.
 // Excludes wounds, count, position, ammo and retaliation usage.
 std::vector<int64_t> defenseProvenance;
 enum class Stance { Absent, CanonicalPercent20ZeroExtra, CanonicalAddOneZeroExtra, Unknown };
 Stance stance = Stance::Unknown;
};

class DefendProvenance
{
public:
 enum class Action { Defend, Wait, NoAction, Walk, Shoot, Melee, Unsupported };
 struct Basis { int32_t originalDefense, fixedBonus; };
private:
 struct Record { uint64_t identity; std::vector<int64_t> provenance; Basis basis; };
 struct Pending { uint64_t sequence; uint32_t actor; Action action; std::optional<Record> defended; };
 std::optional<uint64_t> epoch;
 uint32_t round = 0;
 uint64_t lastSequence = 0;
 bool observedSequence = false;
 std::optional<Pending> pending;
 std::map<uint32_t, Record> records;

 static std::optional<Record> preDefend(const DefendUnitFacts & f)
 {
  if(!f.canonical || !f.alive || f.stance != DefendUnitFacts::Stance::Absent
   || f.underlyingDefense < 0 || f.underlyingDefense > std::numeric_limits<int32_t>::max()/20
   || f.displayedDefense != f.underlyingDefense) return std::nullopt;
  const int64_t bonus = std::max<int64_t>(f.underlyingDefense*20/100, 1);
  return Record{f.identity, f.defenseProvenance,
   {static_cast<int32_t>(f.underlyingDefense), static_cast<int32_t>(bonus)}};
 }
 static bool matches(const Record & r, const DefendUnitFacts & f)
 {
  const auto expected = r.basis.originalDefense < 5
   ? DefendUnitFacts::Stance::CanonicalAddOneZeroExtra
   : DefendUnitFacts::Stance::CanonicalPercent20ZeroExtra;
  return f.canonical && f.alive && f.identity == r.identity
   && f.defenseProvenance == r.provenance && f.stance == expected
   && f.underlyingDefense == r.basis.originalDefense
   && f.displayedDefense == int64_t(r.basis.originalDefense)+r.basis.fixedBonus;
 }
public:
 // Attaching never reconstructs existing Defend; only newly observed transitions qualify.
 void attach(uint64_t newEpoch, uint32_t newRound)
 {
  epoch = newEpoch; round = newRound; lastSequence = 0; observedSequence = false;
  invalidate();
 }
 void detach() { epoch.reset(); invalidate(); }
 void invalidate() { pending.reset(); records.clear(); }
 void nextRound(uint32_t value) { round = value; invalidate(); }

 bool start(uint64_t eventEpoch, uint32_t eventRound, uint64_t sequence,
  Action action, const DefendUnitFacts & actor, const DefendOrdinaryDomain & domain)
 {
  if(!epoch || *epoch != eventEpoch || round != eventRound || pending
   || (observedSequence && (lastSequence == std::numeric_limits<uint64_t>::max() || sequence != lastSequence+1)) || !domain.established()
   || !actor.canonical || !actor.alive || action == Action::Unsupported)
  {
   invalidate(); return false;
  }
  observedSequence = true; lastSequence = sequence;
  Pending p{sequence, actor.id, action, std::nullopt};
  if(action == Action::Defend)
  {
   records.erase(actor.id);
   p.defended = preDefend(actor);
   if(!p.defended) { invalidate(); return false; }
  }
  pending = std::move(p);
  return true;
 }
 // No queries are allowed while an action is in flight. Unexpected effect packets
 // call invalidate() immediately; End cannot resurrect an erased pending record.
 bool finish(uint64_t eventEpoch, uint32_t eventRound, uint64_t sequence,
  const std::vector<DefendUnitFacts> & current, const DefendOrdinaryDomain & domain)
 {
  if(!epoch || *epoch != eventEpoch || round != eventRound || !pending
   || pending->sequence != sequence || !domain.established())
  { invalidate(); return false; }
  std::map<uint32_t, const DefendUnitFacts *> byId;
  for(const auto & f : current)
   if(!byId.emplace(f.id, &f).second) { invalidate(); return false; }
  for(auto i = records.begin(); i != records.end();)
  {
   const auto f = byId.find(i->first);
   if(f == byId.end() || !matches(i->second, *f->second)) i = records.erase(i);
   else ++i;
  }
  const auto p = std::move(*pending); pending.reset();
  if(p.defended)
  {
   const auto f = byId.find(p.actor);
   if(f == byId.end() || !matches(*p.defended, *f->second)) return false;
   records.insert_or_assign(p.actor, *p.defended);
  }
  return true;
 }
 std::optional<Basis> query(uint64_t eventEpoch, uint32_t eventRound,
  const DefendUnitFacts & f, const DefendOrdinaryDomain & domain)
 {
  if(!epoch || *epoch != eventEpoch || round != eventRound || !domain.established())
  { invalidate(); return std::nullopt; }
  if(pending) return std::nullopt;
  const auto i = records.find(f.id);
  if(i == records.end()) return std::nullopt;
  if(!matches(i->second, f)) { records.erase(i); return std::nullopt; }
  return i->second.basis;
 }
};
}
