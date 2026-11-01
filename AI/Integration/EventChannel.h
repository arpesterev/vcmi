/*
 * EventChannel.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace AIIntegration
{
enum class EventPhase { BEGIN, COMPLETE, FAILED };
struct EventRecord
{
	std::string backend;
	std::uint64_t sequence;
	std::int64_t battle;
	std::string kind;
	EventPhase phase;
};

/// Synchronous delivery keeps native callback references valid. Not an async queue.
class EventChannel
{
public:
	using Observer = std::function<void(const EventRecord &)>;
	explicit EventChannel(std::string backend, Observer observer = {})
		: backend(std::move(backend)), observer(std::move(observer))
	{
		if(this->backend.empty())
			throw std::invalid_argument("AI event channel requires a backend name");
	}

	EventChannel(const EventChannel &) = delete;
	EventChannel & operator=(const EventChannel &) = delete;

	template<typename Handler>
	void deliver(std::int64_t battle, std::string kind, Handler && handler)
	{
		if(kind.empty())
			throw std::invalid_argument("AI event requires a kind");
		if(nextSequence == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("AI event sequence exhausted");
		EventRecord event{backend, ++nextSequence, battle, std::move(kind), EventPhase::BEGIN};
		observe(event);
		try
		{
			std::forward<Handler>(handler)();
		}
		catch(const std::exception &)
		{
			event.phase = EventPhase::FAILED;
			observe(event);
			throw;
		}
		event.phase = EventPhase::COMPLETE;
		observe(event);
	}

private:
	void observe(const EventRecord & event) const
	{
		if(observer)
			observer(event);
	}
	std::string backend;
	Observer observer;
	std::uint64_t nextSequence = 0;
};
}
