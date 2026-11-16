/* Native capture access prototype. GPL-2.0-or-later. */
#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace battle::prototype
{
class NativeSnapshotAccessTestPeer;

/// Primitive only: uninstrumented native accesses receive no protection.
/// One coordinator must cover the entire source graph, caches and generation.
class NativeSnapshotAccess final
{
	friend class NativeSnapshotAccessTestPeer;
	enum class Kind : std::size_t { Read, CacheRead, Write, Capture };
	struct Entry
	{
		Kind kind;
		std::size_t depth;
	};
	std::mutex mutex;
	std::condition_variable changed;
	std::map<std::thread::id, Entry> owners;
	std::array<std::size_t, 4> pending{};
	std::size_t readers = 0;
	bool exclusive = false;

	static constexpr std::size_t index(Kind kind) { return static_cast<std::size_t>(kind); }
	std::size_t exclusiveWaiters() const
	{
		return pending[index(Kind::CacheRead)] + pending[index(Kind::Write)] + pending[index(Kind::Capture)];
	}

	// Created and destroyed while mutex is held, including allocation/wait failure.
	struct Pending final
	{
		NativeSnapshotAccess & access;
		Kind kind;
		Pending(NativeSnapshotAccess & access, Kind kind) : access(access), kind(kind)
		{
			++access.pending[index(kind)];
			access.changed.notify_all();
		}
		~Pending()
		{
			--access.pending[index(kind)];
			access.changed.notify_all();
		}
	};

	void enter(Kind kind)
	{
		std::unique_lock lock(mutex);
		const auto id = std::this_thread::get_id();
		const auto owner = owners.find(id);
		if(owner != owners.end())
		{
			const auto outer = owner->second.kind;
			const bool allowed = kind == Kind::Read
				|| (kind == Kind::CacheRead && outer != Kind::Read)
				|| (kind == Kind::Write && outer == Kind::Write);
			if(!allowed)
				throw std::logic_error("NativeSnapshotAccess: forbidden upgrade or nested capture/write");
			++owner->second.depth;
			return;
		}
		Pending waiting(*this, kind);
		changed.wait(lock, [&]
		{
			return kind == Kind::Read ? !exclusive && exclusiveWaiters() == 0 : !exclusive && readers == 0;
		});
		owners.emplace(id, Entry{kind, 1});
		if(kind == Kind::Read)
			++readers;
		else
			exclusive = true;
	}

	void leave() noexcept
	{
		std::lock_guard lock(mutex);
		const auto owner = owners.find(std::this_thread::get_id());
		if(owner == owners.end())
			std::terminate();
		if(--owner->second.depth != 0)
			return;
		if(owner->second.kind == Kind::Read)
			--readers;
		else
			exclusive = false;
		owners.erase(owner);
		changed.notify_all();
	}

	struct Guard final
	{
		NativeSnapshotAccess & access;
		Guard(NativeSnapshotAccess & access, Kind kind) : access(access) { access.enter(kind); }
		~Guard() { access.leave(); }
		Guard(const Guard &) = delete;
		Guard & operator=(const Guard &) = delete;
	};

	template<class Function> void invoke(Kind kind, Function && function)
	{
		static_assert(std::is_void_v<std::invoke_result_t<Function>>, "Access callbacks must return void");
		Guard guard(*this, kind);
		std::invoke(std::forward<Function>(function));
	}
public:
	NativeSnapshotAccess() = default;
	NativeSnapshotAccess(const NativeSnapshotAccess &) = delete;
	NativeSnapshotAccess & operator=(const NativeSnapshotAccess &) = delete;

	/// Strictly non-mutating reads only; const getters that fill caches use cacheRead.
	template<class Function> void read(Function && function) { invoke(Kind::Read, std::forward<Function>(function)); }
	/// Exclusive cache publication; permits nested reads/cache reads.
	template<class Function> void cacheRead(Function && function) { invoke(Kind::CacheRead, std::forward<Function>(function)); }
	/// Exclusive full state mutation; permits nested writes and reads/cache reads.
	template<class Function> void write(Function && function) { invoke(Kind::Write, std::forward<Function>(function)); }
	/// Exclusive capture; requires no current access on this thread.
	template<class Function> void capture(Function && function) { invoke(Kind::Capture, std::forward<Function>(function)); }
};
}
