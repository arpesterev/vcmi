#include "lib/StdInc.h"
#include <gtest/gtest.h>
#include "lib/battle/NativeSnapshotAccess.h"
#include <atomic>
#include <chrono>
#include <future>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <system_error>
#include <vector>

namespace battle::prototype
{
// No scheduling assumption: observe a waiter registered inside enter().
class NativeSnapshotAccessTestPeer
{
public:
	enum class Waiting { Read, CacheRead, Write, Capture };
	static bool waitFor(NativeSnapshotAccess & access, Waiting kind)
	{
		std::unique_lock lock(access.mutex);
		return access.changed.wait_for(lock, std::chrono::seconds(10), [&]
		{
			return access.pending.at(static_cast<std::size_t>(kind)) != 0;
		});
	}
};
}

using battle::prototype::NativeSnapshotAccess;
using Peer = battle::prototype::NativeSnapshotAccessTestPeer;

namespace
{
constexpr auto watchdog = std::chrono::seconds(10);

[[noreturn]] void fixtureFatal(const char * message)
{
	std::fprintf(stderr, "NativeSnapshotAccess fixture: %s\n", message);
	std::abort();
}

// Declare every worker before ReleaseOnExit: unwinding releases before joining.
class ReleaseOnExit final
{
	std::promise<void> promise;
	bool released = false;
public:
	std::shared_future<void> future() { return promise.get_future().share(); }
	void release()
	{
		if(!released)
		{
			promise.set_value();
			released = true;
		}
	}
	~ReleaseOnExit() { release(); }
};

void awaitRelease(const std::shared_future<void> & resume)
{
	if(resume.wait_for(watchdog) != std::future_status::ready)
		throw std::runtime_error("fixture release watchdog expired");
	resume.get();
}

class AsyncWorker final
{
	struct Signal
	{
		std::mutex mutex;
		std::condition_variable changed;
		bool ready = false;
		bool completed = false;
	};
	std::shared_ptr<Signal> signal = std::make_shared<Signal>();
	std::future<void> task;
	struct Completion
	{
		std::shared_ptr<Signal> signal;
		~Completion()
		{
			std::lock_guard lock(signal->mutex);
			signal->completed = true;
			signal->changed.notify_all();
		}
	};
public:
	template<class Function> void start(Function function)
	{
		if(task.valid()) throw std::logic_error("worker already started");
		task = std::async(std::launch::async, [state = signal, function = std::move(function)]() mutable
		{
			Completion completed{state};
			auto ready = [state]
			{
				std::lock_guard lock(state->mutex);
				state->ready = true;
				state->changed.notify_all();
			};
			function(ready);
		});
	}
	void awaitReady()
	{
		std::unique_lock lock(signal->mutex);
		if(!signal->changed.wait_for(lock, watchdog, [&] { return signal->ready || signal->completed; }))
			throw std::runtime_error("worker startup watchdog expired");
		if(signal->ready) return;
		lock.unlock();
		get(); // Propagate the original setup exception stored by std::async.
		throw std::logic_error("worker finished without signalling readiness");
	}
	void get()
	{
		if(task.wait_for(watchdog) != std::future_status::ready)
			throw std::runtime_error("worker completion watchdog expired");
		task.get();
	}
	~AsyncWorker() noexcept
	{
		if(!task.valid()) return;
		// Never detach a worker referencing test locals, or join forever on unwind.
		if(task.wait_for(watchdog) != std::future_status::ready)
			fixtureFatal("worker did not stop after release; terminating test process");
		try { task.get(); }
		catch(const std::exception & error)
		{
			fixtureFatal(error.what()); // Unobserved secondary worker failure is fatal, never hidden.
		}
	}
};

// The timeout is a watchdog, never evidence that a contender was excluded.
void checkSplitPublication(bool writer, bool membership)
{
	NativeSnapshotAccess access;
	int value = 0, version = 0;
	std::vector<int> requests;
	std::atomic<bool> captured{false};
	int capturedValue = -1, capturedVersion = -1;
	std::size_t capturedRequests = 0;
	AsyncWorker mutator, capture;
	ReleaseOnExit finish;
	auto resume = finish.future();
	mutator.start([&, resume](auto ready)
	{
		auto mutate = [&]
		{
			value = 17;
			if(membership) requests.push_back(3);
			ready();
			awaitRelease(resume);
			if(membership) requests.push_back(4);
			version = 17;
		};
		if(writer) access.write(mutate); else access.cacheRead(mutate);
	});
	mutator.awaitReady();
	capture.start([&](auto)
	{
		access.capture([&]
		{
			capturedValue = value;
			capturedVersion = version;
			capturedRequests = requests.size();
			captured = true;
		});
	});
	EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Capture));
	EXPECT_FALSE(captured.load());
	finish.release();
	mutator.get();
	capture.get();
	EXPECT_TRUE(captured.load());
	EXPECT_EQ(capturedValue, 17);
	EXPECT_EQ(capturedVersion, 17);
	EXPECT_EQ(capturedRequests, membership ? 2u : 0u);
}
}

TEST(NativeSnapshotAccessTest, CaptureWaitsForCompleteCacheValueAndVersion)
{
	checkSplitPublication(false, false);
}

TEST(NativeSnapshotAccessTest, CaptureWaitsForCompleteRequestAndPerTurnMembership)
{
	checkSplitPublication(false, true);
}

TEST(NativeSnapshotAccessTest, CaptureWaitsForCompleteScalarAndGraphWrite)
{
	checkSplitPublication(true, true);
}

TEST(NativeSnapshotAccessTest, CaptureExcludesAllOtherAccessKinds)
{
	NativeSnapshotAccess access;
	std::atomic<int> completed{0};
	AsyncWorker holder, reader, cache, writer, capture;
	ReleaseOnExit release;
	auto resume = release.future();
	holder.start([&, resume](auto ready)
	{
		access.capture([&] { ready(); awaitRelease(resume); });
	});
	holder.awaitReady();
	reader.start([&](auto) { access.read([&] { ++completed; }); });
	cache.start([&](auto) { access.cacheRead([&] { ++completed; }); });
	writer.start([&](auto) { access.write([&] { ++completed; }); });
	capture.start([&](auto) { access.capture([&] { ++completed; }); });
	EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Read));
	EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::CacheRead));
	EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Write));
	EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Capture));
	EXPECT_EQ(completed.load(), 0);
	release.release();
	holder.get(); reader.get(); cache.get(); writer.get(); capture.get();
	EXPECT_EQ(completed.load(), 4);
}

TEST(NativeSnapshotAccessTest, PureReadersOverlap)
{
	NativeSnapshotAccess access;
	AsyncWorker first, second;
	ReleaseOnExit release;
	auto resume = release.future();
	first.start([&, resume](auto ready)
	{
		access.read([&] { ready(); awaitRelease(resume); });
	});
	first.awaitReady();
	second.start([&](auto ready)
	{
		access.read([&] { ready(); });
	});
	second.awaitReady();
	release.release();
	first.get(); second.get();
}

TEST(NativeSnapshotAccessTest, NestedCacheReadsUnderCaptureAndWriteDoNotDeadlock)
{
	NativeSnapshotAccess access;
	int calls = 0;
	auto nested = [&]
	{
		access.read([&] { access.cacheRead([&] { access.read([&] { ++calls; }); }); });
	};
	access.capture(nested);
	access.write([&] { access.write(nested); });
	access.cacheRead(nested);
	EXPECT_EQ(calls, 3);
}

TEST(NativeSnapshotAccessTest, PureReaderCannotUpgrade)
{
	NativeSnapshotAccess access;
	bool invoked = false;
	access.read([&]
	{
		access.read([&]
		{
			EXPECT_THROW(access.capture([&] { invoked = true; }), std::logic_error);
			EXPECT_THROW(access.write([&] { invoked = true; }), std::logic_error);
			EXPECT_THROW(access.cacheRead([&] { invoked = true; }), std::logic_error);
		});
	});
	EXPECT_FALSE(invoked);
	EXPECT_NO_THROW(access.capture([] {}));
}

TEST(NativeSnapshotAccessTest, NoNestedCaptureOrCaptureMutation)
{
	NativeSnapshotAccess access;
	access.write([&] { EXPECT_THROW(access.capture([] {}), std::logic_error); });
	access.cacheRead([&]
	{
		EXPECT_THROW(access.capture([] {}), std::logic_error);
		EXPECT_THROW(access.write([] {}), std::logic_error);
	});
	access.capture([&]
	{
		EXPECT_THROW(access.capture([] {}), std::logic_error);
		EXPECT_THROW(access.write([] {}), std::logic_error);
	});
}

TEST(NativeSnapshotAccessTest, ExceptionsReleaseEveryAccessKind)
{
	NativeSnapshotAccess access;
	auto fail = [] { throw std::runtime_error("test failure"); };
	EXPECT_THROW(access.read(fail), std::runtime_error);
	EXPECT_THROW(access.cacheRead(fail), std::runtime_error);
	EXPECT_THROW(access.write([&] { access.read(fail); }), std::runtime_error);
	EXPECT_THROW(access.capture([&] { access.cacheRead(fail); }), std::runtime_error);
	AsyncWorker capture;
	capture.start([&](auto) { access.capture([] {}); });
	EXPECT_NO_THROW(capture.get());
}

TEST(NativeSnapshotAccessTest, NestedExceptionKeepsOuterCaptureHeld)
{
	NativeSnapshotAccess access;
	std::atomic<bool> entered{false};
	AsyncWorker waiter;
	access.capture([&]
	{
		EXPECT_THROW(access.cacheRead([] { throw std::runtime_error("nested"); }), std::runtime_error);
		waiter.start([&](auto) { access.write([&] { entered = true; }); });
		EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Write));
		EXPECT_FALSE(entered.load());
		EXPECT_NO_THROW(access.read([] {}));
	});
	waiter.get();
	EXPECT_TRUE(entered.load());
}

TEST(NativeSnapshotAccessTest, WaitingCaptureDoesNotBlockExistingReaderRecursion)
{
	NativeSnapshotAccess access;
	AsyncWorker capture;
	access.read([&]
	{
		capture.start([&](auto) { access.capture([] {}); });
		EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Capture));
		EXPECT_NO_THROW(access.read([] {}));
	});
	capture.get();
}

TEST(NativeSnapshotAccessTest, WaitingWriterPrecedesNewPureReader)
{
	NativeSnapshotAccess access;
	int value = 0, seen = -1;
	AsyncWorker writer, reader;
	access.read([&]
	{
		writer.start([&](auto) { access.write([&] { value = 42; }); });
		EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Write));
		reader.start([&](auto) { access.read([&] { seen = value; }); });
		EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Read));
	});
	writer.get(); reader.get();
	EXPECT_EQ(seen, 42);
}

TEST(NativeSnapshotAccessFixtureTest, SetupExceptionPropagatesBeforeReady)
{
	AsyncWorker worker;
	worker.start([](auto) { throw std::range_error("original setup failure"); });
	EXPECT_THROW(worker.awaitReady(), std::range_error);
}

TEST(NativeSnapshotAccessFixtureTest, LaunchFailureUnwindsReleaseBeforeJoin)
{
	NativeSnapshotAccess access;
	std::atomic<bool> finished{false};
	auto scenario = [&]
	{
		AsyncWorker holder, neverLaunched;
		ReleaseOnExit release;
		auto resume = release.future();
		holder.start([&, resume](auto ready)
		{
			access.capture([&] { ready(); awaitRelease(resume); });
			finished = true;
		});
		holder.awaitReady();
		// Same unwind point as failure to create the next std::async thread.
		throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
	};
	EXPECT_THROW(scenario(), std::system_error);
	EXPECT_TRUE(finished.load());
	EXPECT_NO_THROW(access.capture([] {}));
}

TEST(NativeSnapshotAccessFixtureTest, AssertionExceptionReleasesHolderAndPendingWorker)
{
	NativeSnapshotAccess access;
	std::atomic<int> finished{0};
	auto scenario = [&]
	{
		AsyncWorker holder, waiter;
		ReleaseOnExit release;
		auto resume = release.future();
		holder.start([&, resume](auto ready)
		{
			access.capture([&] { ready(); awaitRelease(resume); });
			++finished;
		});
		holder.awaitReady();
		waiter.start([&](auto) { access.write([&] { ++finished; }); });
		EXPECT_TRUE(Peer::waitFor(access, Peer::Waiting::Write));
		// A throwing assertion at this point takes the same C++ unwind path.
		throw std::runtime_error("injected assertion exception");
	};
	EXPECT_THROW(scenario(), std::runtime_error);
	EXPECT_EQ(finished.load(), 2);
	EXPECT_NO_THROW(access.capture([] {}));
}
