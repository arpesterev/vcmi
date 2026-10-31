/*
 * DecisionSession.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace AIIntegration
{
/// Only player-visible state may be encoded in payload. Ports own rule translation.
struct Observation
{
	std::uint64_t revision = 0;
	std::string ruleset;
	std::string payload;
	std::vector<std::string> legalActions;
	bool operator==(const Observation &) const = default;
};

enum class InputKind { START, UPDATE, DECIDE, ACCEPTED, REJECTED, TIMEOUT, END };
struct InputEvent
{
	std::uint64_t session = 0;
	std::uint64_t sequence = 0;
	InputKind kind = InputKind::START;
	std::optional<Observation> observation;
	std::uint64_t request = 0;
	bool operator==(const InputEvent &) const = default;
};

struct Decision
{
	std::uint64_t revision;
	std::string action;
};
struct Command
{
	std::uint64_t session;
	std::uint64_t request;
	std::uint64_t eventSequence;
	std::uint64_t basedOnRevision;
	std::string action;
};

class EventPolicy
{
public:
	virtual ~EventPolicy() = default;
	virtual bool supports(const std::string & ruleset) const = 0;
	virtual std::optional<Decision> onEvent(const InputEvent & event) = 0;
};

/// Single owner thread. Reentrant acknowledgements are queued as owned values.
class DecisionSession
{
public:
	using Send = std::function<void(const Command &)>;
	DecisionSession(std::uint64_t session, EventPolicy & policy, Send send)
		: session(session), policy(policy), send(std::move(send))
	{
		if(session == 0 || !this->send)
			throw std::invalid_argument("AI session requires a nonzero ID and command transport");
	}
	DecisionSession(const DecisionSession &) = delete;
	DecisionSession & operator=(const DecisionSession &) = delete;

	void post(InputEvent event)
	{
		if(std::this_thread::get_id() != owner)
			throw std::logic_error("AI events must be posted on the session owner thread");
		if(failed)
			throw std::logic_error("AI session is faulted; resynchronization requires a new session");
		if(event.session != session)
			throw std::invalid_argument("AI event belongs to another session");
		if(inbox.size() >= 256)
			throw std::runtime_error("AI event queue limit exceeded");
		inbox.push_back(std::move(event));
		if(dispatching)
			return;
		dispatching = true;
		try
		{
			while(!inbox.empty())
			{
				auto next = std::move(inbox.front());
				inbox.pop_front();
				apply(next);
			}
		}
		catch(const std::exception &)
		{
			failed = true;
			dispatching = false;
			inbox.clear();
			throw;
		}
		dispatching = false;
	}

	bool hasPendingCommand() const { return pending.has_value(); }
	bool isFaulted() const { return failed; }
	bool isEnded() const { return ended; }

private:
	void apply(const InputEvent & event)
	{
		if(previous && event == *previous)
			return;
		if(event.sequence != (previous ? previous->sequence + 1 : 1))
			throw std::runtime_error("AI event gap, duplicate conflict or out-of-order delivery");
		if(ended)
			throw std::logic_error("AI event after session end");
		if(!started && event.kind != InputKind::START)
			throw std::logic_error("AI session must start with START");
		if(started && event.kind == InputKind::START)
			throw std::logic_error("AI session cannot start twice");
		if(event.kind == InputKind::START && !event.observation)
			throw std::invalid_argument("AI session start requires an observation");
		if(event.kind == InputKind::UPDATE && !event.observation)
			throw std::invalid_argument("AI state update requires an observation");

		if(event.observation)
		{
			const auto & incoming = *event.observation;
			if(!policy.supports(incoming.ruleset))
				throw std::runtime_error("AI backend cannot represent this ruleset");
			if(state && (incoming.ruleset != state->ruleset || incoming.revision < state->revision))
				throw std::runtime_error("AI ruleset changed or observation moved backwards");
			if(state && incoming.revision == state->revision && incoming != *state)
				throw std::runtime_error("AI observation changed without a revision change");
			if(!state || incoming.revision != state->revision)
				rejectedActions.clear();
			state = incoming;
		}

		if(event.kind == InputKind::START)
			started = true;
		if(event.kind == InputKind::ACCEPTED || event.kind == InputKind::REJECTED || event.kind == InputKind::TIMEOUT)
		{
			if(!pending || event.request != pending->request)
				throw std::runtime_error("AI acknowledgement does not match the pending request");
			if(event.kind == InputKind::TIMEOUT)
				throw std::runtime_error("AI command timed out with unknown outcome; do not retry automatically");
			if(event.kind == InputKind::REJECTED && state->revision == pending->basedOnRevision)
				rejectedActions.insert(pending->action);
			if(event.kind == InputKind::ACCEPTED)
			{
				if(pending->basedOnRevision == std::numeric_limits<std::uint64_t>::max())
					throw std::overflow_error("AI observation revision exhausted");
				minDecisionRevision = pending->basedOnRevision + 1;
			}
			pending.reset();
			if(event.kind == InputKind::ACCEPTED)
				stateUsable = false;
		}
		if(event.observation)
			stateUsable = state->revision >= minDecisionRevision;
		if(event.kind == InputKind::DECIDE && (!stateUsable || pending))
			throw std::logic_error("AI decision requires applied state and no pending command");
		if(event.kind == InputKind::END)
		{
			ended = true;
			pending.reset();
		}

		previous = event;
		auto decision = policy.onEvent(event);
		if(!decision)
		{
			if(event.kind == InputKind::DECIDE)
				throw std::runtime_error("AI backend returned no decision");
			return;
		}
		if(event.kind != InputKind::DECIDE)
			throw std::logic_error("AI backend issued an action outside a decision event");
		if(decision->revision != state->revision)
			throw std::runtime_error("AI backend returned an action based on stale state");
		if(std::find(state->legalActions.begin(), state->legalActions.end(), decision->action) == state->legalActions.end())
			throw std::runtime_error("AI backend returned an illegal action");
		if(rejectedActions.count(decision->action))
			throw std::runtime_error("AI backend repeated a rejected action without a state change");
		if(nextRequest == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("AI request sequence exhausted");
		Command command{session, ++nextRequest, event.sequence, state->revision, std::move(decision->action)};
		pending = command;
		send(command);
	}

	const std::thread::id owner = std::this_thread::get_id();
	std::uint64_t session;
	EventPolicy & policy;
	Send send;
	std::deque<InputEvent> inbox;
	std::optional<InputEvent> previous;
	std::optional<Observation> state;
	std::optional<Command> pending;
	std::set<std::string> rejectedActions;
	std::uint64_t nextRequest = 0;
	std::uint64_t minDecisionRevision = 0;
	bool dispatching = false;
	bool started = false;
	bool ended = false;
	bool failed = false;
	bool stateUsable = false;
};
}
