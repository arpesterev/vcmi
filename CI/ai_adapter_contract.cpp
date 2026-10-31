/*
 * ai_adapter_contract.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../AI/Integration/BackendRegistry.h"
#include "../AI/Integration/DecisionSession.h"
#include "../AI/Integration/EventChannel.h"

#include <iostream>

using namespace AIIntegration;
namespace
{
void require(bool value) { if(!value) throw std::runtime_error("assertion failed"); }
template<typename Function> void rejects(Function && action)
{
	bool rejected = false;
	try { action(); } catch(const std::exception &) { rejected = true; }
	require(rejected);
}
struct Policy : EventPolicy
{
	std::vector<InputKind> observed;
	std::optional<Decision> decision = Decision{1, "attack:2"};
	bool unsolicited = false;
	bool supports(const std::string & ruleset) const override { return ruleset == "h3-test"; }
	std::optional<Decision> onEvent(const InputEvent & event) override
	{
		observed.push_back(event.kind);
		return event.kind == InputKind::DECIDE || unsolicited ? decision : std::nullopt;
	}
};
Observation observation(std::uint64_t revision = 1)
{
	return {revision, "h3-test", "visible snapshot", {"attack:2", "defend"}};
}
InputEvent input(std::uint64_t sequence, InputKind kind, std::optional<Observation> state = {}, std::uint64_t request = 0)
{
	return {7, sequence, kind, std::move(state), request};
}
struct Fixture
{
	Policy policy;
	std::vector<Command> commands;
	DecisionSession session{7, policy, [this](const Command & command) { commands.push_back(command); }};
	Fixture() { session.post(input(1, InputKind::START, observation())); }
	void decide() { session.post(input(2, InputKind::DECIDE)); }
};
BackendBuild build(BackendId id = BackendId::FHEROES2)
{
	return {id, std::string(40, 'a'), std::string(40, 'b'), {"h3-test"}};
}
}
int main()
{
	const std::vector<std::pair<std::string, std::function<void()>>> cases{
		{"four_stable_provider_ids", [] { require(backendSpecs.size() == 4); std::set<std::string_view> names; for(const auto & spec : backendSpecs) names.insert(spec.name); require(names.size() == 4); require(!findBackend("made-up")); }},
		{"unimplemented_provider_is_not_available", [] { BackendRegistry<int> r; for(const auto & s : backendSpecs) { require(!r.available(s.id, "h3-test")); rejects([&] { r.create(s.id, "h3-test"); }); } }},
		{"requires_pinned_source", [] { BackendRegistry<int> r; auto b = build(); b.sourceRevision = "master"; rejects([&] { r.add(b, [] { return std::make_shared<int>(1); }); }); }},
		{"requires_real_factory", [] { BackendRegistry<int> r; rejects([&] { r.add(build(), {}); }); }},
		{"provider_identity_and_ruleset", [] { BackendRegistry<int> r; r.add(build(), [] { return std::make_shared<int>(42); }); require(r.available(BackendId::FHEROES2, "h3-test")); require(!r.available(BackendId::FHEROES2, "wog")); rejects([&] { r.create(BackendId::FHEROES2, "wog"); }); require(*r.create(BackendId::FHEROES2, "h3-test") == 42); require(r.provenance(BackendId::FHEROES2).sourceRevision == std::string(40, 'a')); }},
		{"provider_cannot_be_replaced_during_game", [] { BackendRegistry<int> r; r.add(build(), [] { return std::make_shared<int>(1); }); r.create(BackendId::FHEROES2, "h3-test"); rejects([&] { r.add(build(BackendId::TUM), [] { return std::make_shared<int>(2); }); }); }},
		{"duplicate_registration_rejected", [] { BackendRegistry<int> r; r.add(build(), [] { return std::make_shared<int>(1); }); rejects([&] { r.add(build(), [] { return std::make_shared<int>(2); }); }); }},
		{"null_backend_does_not_fall_back", [] { BackendRegistry<int> r; r.add(build(), [] { return std::shared_ptr<int>{}; }); rejects([&] { r.create(BackendId::FHEROES2, "h3-test"); }); }},
		{"backend_error_does_not_fall_back", [] { BackendRegistry<int> r; r.add(build(), []() -> std::shared_ptr<int> { throw std::runtime_error("backend failure"); }); rejects([&] { r.create(BackendId::FHEROES2, "h3-test"); }); }},
		{"native_event_is_delivered_once", [] { std::vector<EventRecord> log; EventChannel c("MMAI", [&](const auto & e) { log.push_back(e); }); int n = 0; c.deliver(3, "activeStack", [&] { ++n; }); require(n == 1 && log.size() == 2); require(log[0].phase == EventPhase::BEGIN && log[1].phase == EventPhase::COMPLETE); require(log[0].sequence == log[1].sequence && log[0].backend == "MMAI"); }},
		{"native_event_failure_is_not_success", [] { std::vector<EventRecord> log; EventChannel c("MMAI", [&](const auto & e) { log.push_back(e); }); rejects([&] { c.deliver(2, "activeStack", [] { throw std::runtime_error("failure"); }); }); require(log.size() == 2 && log.back().phase == EventPhase::FAILED); }},
		{"nested_native_events_keep_identity", [] { std::vector<EventRecord> log; EventChannel c("MMAI", [&](const auto & e) { log.push_back(e); }); c.deliver(3, "outer", [&] { c.deliver(3, "inner", [] {}); }); require(log.size() == 4); require(log[0].sequence == 1 && log[1].sequence == 2 && log[2].sequence == 2 && log[3].sequence == 1); }},
		{"commands_only_on_decision_events", [] { Fixture f; require(f.commands.empty()); f.decide(); require(f.commands.size() == 1 && f.session.hasPendingCommand()); require(f.commands[0].session == 7 && f.commands[0].basedOnRevision == 1 && f.commands[0].eventSequence == 2); }},
		{"same_event_replay_does_not_duplicate_command", [] { Fixture f; f.decide(); f.session.post(input(2, InputKind::DECIDE)); require(f.commands.size() == 1); }},
		{"conflicting_duplicate_faults", [] { Fixture f; rejects([&] { f.session.post(input(1, InputKind::UPDATE, observation(2))); }); require(f.session.isFaulted()); }},
		{"event_gap_faults", [] { Fixture f; rejects([&] { f.session.post(input(3, InputKind::DECIDE)); }); require(f.session.isFaulted()); }},
		{"cross_session_event_rejected", [] { Fixture f; auto e = input(2, InputKind::DECIDE); e.session = 8; rejects([&] { f.session.post(e); }); require(f.commands.empty()); }},
		{"stale_backend_decision_rejected", [] { Fixture f; f.policy.decision->revision = 0; rejects([&] { f.decide(); }); require(f.commands.empty()); }},
		{"illegal_action_rejected", [] { Fixture f; f.policy.decision->action = "attack:hidden-target"; rejects([&] { f.decide(); }); require(f.commands.empty()); }},
		{"no_second_command_before_ack", [] { Fixture f; f.decide(); rejects([&] { f.session.post(input(3, InputKind::DECIDE)); }); require(f.commands.size() == 1); }},
		{"wrong_request_ack_rejected", [] { Fixture f; f.decide(); rejects([&] { f.session.post(input(3, InputKind::ACCEPTED, observation(2), 99)); }); }},
		{"ack_without_applied_state_cannot_trigger_action", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::ACCEPTED, {}, 1)); require(!f.session.hasPendingCommand()); rejects([&] { f.session.post(input(4, InputKind::DECIDE)); }); }},
		{"same_revision_is_not_progress_after_acceptance", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::ACCEPTED, observation(), 1)); rejects([&] { f.session.post(input(4, InputKind::DECIDE)); }); }},
		{"accepted_action_then_updated_state_allows_next_decision", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::ACCEPTED, observation(2), 1)); f.policy.decision->revision = 2; f.session.post(input(4, InputKind::DECIDE)); require(f.commands.size() == 2 && f.commands.back().request == 2); }},
		{"rejected_action_not_retried_on_same_state", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::REJECTED, {}, 1)); rejects([&] { f.session.post(input(4, InputKind::DECIDE)); }); require(f.commands.size() == 1); }},
		{"different_action_allowed_after_rejection", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::REJECTED, {}, 1)); f.policy.decision->action = "defend"; f.session.post(input(4, InputKind::DECIDE)); require(f.commands.size() == 2); }},
		{"new_state_can_make_previously_rejected_action_legal", [] { Fixture f; f.decide(); f.session.post(input(3, InputKind::REJECTED, {}, 1)); f.session.post(input(4, InputKind::UPDATE, observation(2))); f.policy.decision->revision = 2; f.session.post(input(5, InputKind::DECIDE)); require(f.commands.size() == 2); }},
		{"timeout_is_not_a_success_or_automatic_retry", [] { Fixture f; f.decide(); rejects([&] { f.session.post(input(3, InputKind::TIMEOUT, {}, 1)); }); require(f.session.isFaulted() && f.commands.size() == 1); }},
		{"event_after_end_rejected", [] { Fixture f; f.session.post(input(2, InputKind::END)); require(f.session.isEnded()); rejects([&] { f.session.post(input(3, InputKind::DECIDE)); }); }},
		{"unsupported_ruleset_not_silently_translated", [] { Fixture f; auto s = observation(2); s.ruleset = "h2-test"; rejects([&] { f.session.post(input(2, InputKind::UPDATE, s)); }); require(f.commands.empty()); }},
		{"state_cannot_change_without_revision", [] { Fixture f; auto s = observation(); s.payload = "different snapshot"; rejects([&] { f.session.post(input(2, InputKind::UPDATE, s)); }); }},
		{"policy_cannot_send_unsolicited_commands", [] { Fixture f; f.policy.unsolicited = true; rejects([&] { f.session.post(input(2, InputKind::UPDATE, observation(2))); }); require(f.commands.empty()); }},
		{"reentrant_ack_is_queued_after_pending_is_set", [] { Policy p; DecisionSession * pointer = nullptr; std::vector<Command> commands; DecisionSession s(7, p, [&](const Command & c) { commands.push_back(c); pointer->post(input(3, InputKind::ACCEPTED, observation(2), c.request)); }); pointer = &s; s.post(input(1, InputKind::START, observation())); s.post(input(2, InputKind::DECIDE)); require(commands.size() == 1 && !s.hasPendingCommand()); require(p.observed.size() == 3 && p.observed.back() == InputKind::ACCEPTED); }},
		{"transport_failure_does_not_retry", [] { Policy p; int calls = 0; DecisionSession s(7, p, [&](const Command &) { ++calls; throw std::runtime_error("transport failed"); }); s.post(input(1, InputKind::START, observation())); rejects([&] { s.post(input(2, InputKind::DECIDE)); }); require(s.isFaulted() && calls == 1); rejects([&] { s.post(input(2, InputKind::DECIDE)); }); require(calls == 1); }},
		{"empty_decision_is_an_error_not_idle", [] { Fixture f; f.policy.decision.reset(); rejects([&] { f.decide(); }); require(f.session.isFaulted()); }},
		{"wrong_thread_is_rejected_before_state_access", [] { Fixture f; bool rejected = false; std::thread t([&] { try { f.decide(); } catch(const std::logic_error &) { rejected = true; } }); t.join(); require(rejected && f.commands.empty()); f.decide(); require(f.commands.size() == 1); }},
		{"missing_start_rejected", [] { Policy p; DecisionSession s(7, p, [](const Command &) {}); rejects([&] { s.post(input(1, InputKind::DECIDE)); }); }},
		{"observation_cannot_move_backwards", [] { Fixture f; f.session.post(input(2, InputKind::UPDATE, observation(3))); rejects([&] { f.session.post(input(3, InputKind::UPDATE, observation(2))); }); }},
	};
	int failed = 0;
	for(const auto & [name, test] : cases)
	{
		try { test(); std::cout << "PASS " << name << '\n'; }
		catch(const std::exception & error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
	}
	std::cout << cases.size() - failed << '/' << cases.size() << " adapter contract tests passed\n";
	return failed ? 1 : 0;
}
