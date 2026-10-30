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
	::testing::AddGlobalTestEnvironment(new CVcmiTestConfig());
	return RUN_ALL_TESTS();
}
