/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CVcmiTestConfig.h"
#include "ai/FullMapFixture.h"
#include "../lib/filesystem/CMemoryBuffer.h"
#include "../lib/logging/CLogger.h"

#include <deque>
#include <mutex>

namespace
{
struct TestLogBuffer
{
	std::mutex mutex;
	std::deque<std::string> messages;
};

class TestLogTarget final : public ILogTarget
{
	std::shared_ptr<TestLogBuffer> buffer;
public:
	explicit TestLogTarget(std::shared_ptr<TestLogBuffer> buffer) : buffer(std::move(buffer)) {}

	void write(const LogRecord & record) override
	{
		std::lock_guard lock(buffer->mutex);
		if(buffer->messages.size() == 300)
			buffer->messages.pop_front();
		buffer->messages.push_back(record.domain.getName() + ": " + record.message);
	}
};

class FailedTestLogPrinter final : public ::testing::EmptyTestEventListener
{
	std::shared_ptr<TestLogBuffer> buffer;
public:
	explicit FailedTestLogPrinter(std::shared_ptr<TestLogBuffer> buffer) : buffer(std::move(buffer)) {}

	void OnTestStart(const ::testing::TestInfo &) override
	{
		std::lock_guard lock(buffer->mutex);
		buffer->messages.clear();
	}

	void OnTestEnd(const ::testing::TestInfo & info) override
	{
		if(!info.result()->Failed())
			return;
		std::lock_guard lock(buffer->mutex);
		std::cerr << "Last engine messages for " << info.test_suite_name() << "." << info.name() << ":\n";
		for(const auto & message : buffer->messages)
			std::cerr << message << '\n';
	}
};
}

int main(int argc, char * argv[])
{
	if(argc == 2 && std::string(argv[1]) == "--export-ai-match-map")
	{
		try
		{
			CVcmiTestConfig environment;
			environment.SetUp();
			exportFullMapFixture();
			environment.TearDown();
			return 0;
		}
		catch(const std::exception & error)
		{
			std::cerr << "Cannot export full-map fixture: " << error.what() << std::endl;
			return 1;
		}
	}
	::testing::InitGoogleTest(&argc, argv);
	auto logBuffer = std::make_shared<TestLogBuffer>();
	CLogger::getGlobalLogger()->addTarget(std::make_unique<TestLogTarget>(logBuffer));
	CLogger::getLogger(CLoggerDomain("ai"))->setLevel(ELogLevel::DEBUG);
	::testing::UnitTest::GetInstance()->listeners().Append(new FailedTestLogPrinter(logBuffer));
	::testing::AddGlobalTestEnvironment(new CVcmiTestConfig());
	return RUN_ALL_TESTS();
}
