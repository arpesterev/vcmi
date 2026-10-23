/*
 * BattleSpellSearchBenchmark.cpp, bounded opt-in VCMI experiment.
 * GPL-2.0-or-later; see license.txt.
 * Uses public APIs present in upstream 1.7.5 as well as the candidate.
 */
#include "StdInc.h"
#include "BattleCallerFixture.h"
#include "AI/BattleAI/SpellTargetsEvaluator.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/spells/CSpellHandler.h"
#include "lib/spells/ISpellMechanics.h"
#include "lib/spells/Problem.h"
#include "lib/logging/CLogger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <set>

namespace test
{
namespace callerprobe
{
TEST(BattleSpellSearchBenchmark, DISABLED_BoundedRealSpellSearch)
{
#ifdef VCMI_BATTLE_AI_TEST_TRACE
	GTEST_SKIP() << "Use the separate no-trace Release build for spell-search timing.";
#else
	struct Logging
	{
		std::string mock = ::testing::GMOCK_FLAG(verbose);
		std::streamsize precision = std::cout.precision();
		CLogger * ai = dynamic_cast<CLogger *>(logAi);
		CLogger * global = dynamic_cast<CLogger *>(logGlobal);
		ELogLevel::ELogLevel aiLevel = ai ? ai->getLevel() : ELogLevel::NOT_SET;
		ELogLevel::ELogLevel globalLevel = global ? global->getLevel() : ELogLevel::NOT_SET;
		Logging()
		{
			::testing::GMOCK_FLAG(verbose) = "error";
			if(ai) ai->setLevel(ELogLevel::ERROR);
			if(global) global->setLevel(ELogLevel::ERROR);
		}
		~Logging()
		{
			if(ai) ai->setLevel(aiLevel);
			if(global) global->setLevel(globalLevel);
			::testing::GMOCK_FLAG(verbose) = mock;
			std::cout.precision(precision);
		}
	} logging;
	ASSERT_NE(logging.ai, nullptr);
	ASSERT_NE(logging.global, nullptr);
	using Clock = std::chrono::steady_clock;
	auto elapsed = [](Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	};
	const auto wholeStart = Clock::now();
	constexpr double cellCapMs = 60000;
	constexpr double totalCapMs = 300000;
	constexpr size_t targetCap = 8192;
	std::cout << std::setprecision(17);
	std::cout << "SPELL_SEARCH_CONFIG\ttrace=off\tmode=passive\tlevel=expert\trounds=1\tcell_cap_ms=60000\ttotal_cap_ms=300000\ttarget_cap=8192\tpasses=1\tschema=2\taudit=complete_domain\tteleport_target_cap=8\n";
	for(const int count : {6, 14, 28})
		for(const auto spellId : {SpellID::MAGIC_ARROW, SpellID::TELEPORT, SpellID::REMOVE_OBSTACLE})
		{
			if(elapsed(wholeStart) >= totalCapMs)
			{
				std::cout << "SPELL_SEARCH_TOTAL_CAP\t300000\n";
				return; // Workflow rejects missing/capped rows, never counts them as success.
			}
			Scenario scene(benchmarkSpecs(count, true));
			IBattleInfo::ObstacleCList obstacles;
			std::vector<JsonNode> obstacleSnapshots;
			for(int index = 0; index < 3; ++index)
			{
				auto obstacle = std::make_shared<SpellCreatedObstacle>();
				obstacle->uniqueID = 100 + index;
				obstacle->ID = SpellID::FORCE_FIELD;
				obstacle->pos = BattleHex(8, 3 + index);
				obstacle->customSize.insert(obstacle->pos);
				obstacle->casterSide = BattleSide::DEFENDER;
				obstacle->trigger = SpellID::NONE;
				ObstacleChanges snapshot;
				obstacle->toInfo(snapshot);
				obstacleSnapshots.push_back(snapshot.data);
				obstacles.push_back(obstacle);
			}
			// setupEmptyBattlefield() installs an explicit EXPECT_CALL returning no
			// obstacles. A later ON_CALL cannot override that expectation's action.
			EXPECT_CALL(*scene.callback, getAllObstacles()).WillRepeatedly(Return(obstacles));
			ASSERT_EQ(scene.callback->battleGetAllObstacles(), obstacles);
			ASSERT_EQ(scene.state->battleGetAllObstacles(), obstacles);
			{
				auto child = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.state);
				ASSERT_EQ(child->battleGetAllObstacles(), obstacles);
			}
			ASSERT_EQ(scene.state->battleAliveUnits().size(), static_cast<size_t>(count));
			scene.populateCache();
			const auto before = scene.snapshot();
			const auto * spell = SpellID(spellId).toSpell();
			ASSERT_NE(spell, nullptr);
			const std::string key = spellId == SpellID::MAGIC_ARROW ? "magicArrow"
				: spellId == SpellID::TELEPORT ? "teleport" : "removeObstacle";
			auto caster = scene.state->getForUpdate(1);
			spells::BattleCast cast(scene.state.get(), caster.get(), spells::Mode::PASSIVE, spell);
			cast.setSpellLevel(3);
			cast.setEffectPower(10);
			cast.setEffectDuration(3);
			auto mechanics = spell->battleMechanics(&cast);
			const bool teleport = spellId == SpellID::TELEPORT;
			const bool obstacleSpell = spellId == SpellID::REMOVE_OBSTACLE;
			const std::vector<spells::AimType> expectedTypes = teleport
				? std::vector<spells::AimType>{spells::AimType::CREATURE, spells::AimType::LOCATION}
				: std::vector<spells::AimType>{obstacleSpell ? spells::AimType::LOCATION : spells::AimType::CREATURE};
			ASSERT_EQ(mechanics->getTargetTypes(), expectedTypes);
			const size_t rawBound = teleport ? count * GameConstants::BFIELD_SIZE
				: obstacleSpell ? GameConstants::BFIELD_SIZE : count;
			const auto cellStart = Clock::now();
			// Independent complete-domain audit, not the candidate's shortlist API.
			// All fixture units occupy distinct single hexes, so the ordered hex
			// tuple is a canonical target key, including the Teleport source unit.
			auto canonical = [](const spells::Target & target)
			{
				std::vector<int> result;
				for(const auto & destination : target)
					result.push_back(destination.hexValue.toInt());
				return result;
			};
			std::set<std::vector<int>> legalPool;
			size_t legalChecked = 0;
			auto audit = [&](const spells::Target & target)
			{
				++legalChecked;
				spells::detail::ProblemImpl ignored;
				if(mechanics->canBeCastAt(target, ignored))
				{
					EXPECT_TRUE(legalPool.insert(canonical(target)).second);
				}
			};
			if(obstacleSpell)
			{
				for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
					audit({spells::Destination(BattleHex(hex))});
			}
			else
			{
				const auto units = mechanics->battle()->battleGetAllUnits(false);
				ASSERT_EQ(units.size(), static_cast<size_t>(count));
				std::set<int> sourceHexes;
				for(const auto * unit : units)
				{
					ASSERT_TRUE(sourceHexes.insert(unit->getPosition().toInt()).second);
					if(teleport)
					{
						// Check complete CREATURE+LOCATION pairs only. No heuristic,
						// friendly-only or no-op filtering may shrink the audit pool.
						for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
							audit({spells::Destination(unit->getPosition()), spells::Destination(BattleHex(hex))});
					}
					else
						audit({spells::Destination(unit->getPosition())});
				}
			}
			const auto legalityAuditMs = elapsed(cellStart);
			ASSERT_EQ(legalChecked, rawBound);
			ASSERT_FALSE(legalPool.empty());
			const auto enumerationStart = Clock::now();
			const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
			const auto enumerationMs = elapsed(enumerationStart);
			std::set<std::vector<int>> selectedKeys;
			size_t selectedInPool = 0;
			for(const auto & target : targets)
			{
				ASSERT_EQ(target.size(), expectedTypes.size());
				spells::detail::ProblemImpl ignored;
				ASSERT_TRUE(mechanics->canBeCastAt(target, ignored));
				const auto key = canonical(target);
				ASSERT_TRUE(selectedKeys.insert(key).second);
				ASSERT_EQ(legalPool.count(key), 1u);
				++selectedInPool;
			}
			ASSERT_LE(targets.size(), legalPool.size());
			// Empty baseline enumerators are unsupported, not 100% pruning.
			const std::string selectionMode = targets.empty() ? "empty_enumerator"
				: teleport ? "teleport_k8" : "full_enumeration";
			const auto pruned = targets.empty() ? 0u : legalPool.size() - targets.size();
			if(teleport)
			{
				ASSERT_LE(targets.size(), 8u);
			}
			else if(!targets.empty())
			{
				ASSERT_EQ(selectedKeys, legalPool);
			}
			const auto evalStart = Clock::now();
			size_t evaluated = 0;
			size_t changed = 0;
			double checksum = 0;
			std::string status = elapsed(cellStart) >= cellCapMs || elapsed(wholeStart) >= totalCapMs
				? "TIME_CAP" : targets.size() > targetCap ? "TARGET_CAP" : "COMPLETE";
			if(status == "COMPLETE")
				for(const auto & target : targets)
				{
					if(elapsed(cellStart) >= cellCapMs || elapsed(wholeStart) >= totalCapMs)
					{
						status = "TIME_CAP";
						break;
					}
					auto state = std::make_shared<HypotheticBattle>(scene.environment.get(), scene.state);
					auto currentCaster = state->getForUpdate(1);
					spells::BattleCast currentCast(state.get(), currentCaster.get(), spells::Mode::PASSIVE, spell);
					currentCast.setSpellLevel(3);
					currentCast.setEffectPower(10);
					currentCast.setEffectDuration(3);
					currentCast.castEval(state->getServerCallback(), target);
					bool changedState = state->battleGetAllObstacles().size() != obstacles.size();
					for(const auto * unit : scene.state->battleAliveUnits())
					{
						const auto * updated = state->battleGetUnitByID(unit->unitId());
						changedState = changedState || !updated
							|| updated->getAvailableHealth() != unit->getAvailableHealth()
							|| updated->getPosition() != unit->getPosition();
					}
					changed += changedState;
					// Reproduce the expensive public post-cast search, without hero
					// mana/send callbacks or APIs newly introduced by this patch.
					DamageCache innerCache;
					innerCache.buildDamageCache(state, BattleSide::ATTACKER);
					const auto * active = state->battleGetUnitByID(1);
					float score = EvaluationResult::INEFFECTIVE_SCORE;
					if(active && active->alive())
					{
						PotentialTargets innerTargets(active, innerCache, state);
						BattleExchangeEvaluator evaluator(state, scene.environment, 1.0f, 1);
						evaluator.updateReachabilityMap(state);
						const auto movement = evaluator.findMoveTowardsUnreachable(active, innerTargets, innerCache, state);
						score = movement.score;
						if(!innerTargets.possibleAttacks.empty())
							score = std::max(score, evaluator.findBestTarget(active, innerTargets, innerCache, state).score);
					}
					ASSERT_TRUE(std::isfinite(score));
					checksum += score;
					++evaluated;
				}
			const auto evaluationMs = elapsed(evalStart);
			ASSERT_TRUE(std::isfinite(legalityAuditMs) && std::isfinite(enumerationMs)
				&& std::isfinite(evaluationMs) && std::isfinite(checksum));
			EXPECT_EQ(scene.snapshot(), before);
			EXPECT_EQ(scene.callback->battleGetAllObstacles(), obstacles);
			EXPECT_EQ(scene.state->battleGetAllObstacles(), obstacles);
			for(size_t index = 0; index < obstacles.size(); ++index)
			{
				ObstacleChanges snapshot;
				// Engine serialization API is non-const; match its read-only use.
				const_cast<CObstacleInstance *>(obstacles[index].get())->toInfo(snapshot);
				EXPECT_EQ(snapshot.data, obstacleSnapshots[index]);
			}
			const auto cellMs = elapsed(cellStart);
			if(status == "COMPLETE" && (cellMs >= cellCapMs || elapsed(wholeStart) >= totalCapMs))
				status = "TIME_CAP";
			std::cout << "SPELL_SEARCH\t" << count << '\t' << key << '\t' << targets.size()
				<< '\t' << evaluated << '\t' << status << '\t' << enumerationMs << '\t' << evaluationMs
				<< '\t' << checksum << '\t' << changed << '\t' << mechanics->getTargetTypes().size()
				<< "\tobstacle_entries=3\tsource_units=" << count << "\tfriendly_units=" << count / 2
				<< "\thex_domain=" << GameConstants::BFIELD_SIZE
				<< "\traw_combination_bound=" << rawBound << "\tlegal_pool=" << legalPool.size()
				<< "\tselected=" << targets.size() << "\tpruned=" << pruned
				<< "\tselection_mode=" << selectionMode << "\tlegal_checked=" << legalChecked
				<< "\tselected_in_pool=" << selectedInPool << "\tselected_unique=" << selectedKeys.size()
				<< "\tlegality_audit_ms=" << legalityAuditMs << "\tcell_ms=" << cellMs << '\n';
		}
	const auto totalMs = elapsed(wholeStart);
	std::cout << "SPELL_SEARCH_END\tcells=9\ttotal_ms=" << totalMs
		<< "\tstatus=" << (totalMs >= totalCapMs ? "TIME_CAP" : "COMPLETE") << '\n';
#endif
}
}
}
