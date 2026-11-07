/*
 * router.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "BAI/router.h"

#include "StdInc.h"
#include "callback/CBattleCallback.h"
#include "callback/IGameInfoCallback.h"
#include "filesystem/Filesystem.h"
#include "lib/CRandomGenerator.h"
#include "json/JsonUtils.h"

#include "../../BattleAI/BattleAI.h"
#include "../../StupidAI/StupidAI.h"

#include "BAI/factory.h"
#include "BAI/nn_model_resolution.h"
#include "BAI/scripted/scripted_model.h"
#include "BAI/v13/BAI_v13.h"

#include "AI/MMAI/common.h"

namespace MMAI::BAI
{
// key => {model, path}
using ModelStorage = std::map<std::string, std::pair<std::shared_ptr<MMAI::Schema::IModel>, std::string>>;

namespace
{
	struct ModelRepository
	{
		mutable std::mutex mutex;
		mutable ModelStorage models;
		float temperature = 1.0;
		uint64_t seed = 0;
		std::map<std::string, std::string> paths;
		std::shared_ptr<ScriptedModel> fallbackModel;
		std::string fallbackName;
	};

	std::shared_ptr<ModelRepository> InitModelRepository()
	{
		auto repo = std::make_unique<ModelRepository>();
		auto json = JsonUtils::assembleFromFiles("MMAI/CONFIG/mmai-settings.json");
		std::string fallback;

		if(json.isStruct())
		{
			JsonUtils::validate(json, "vcmi:mmaiSettings", "mmai");
			repo->temperature = static_cast<float>(json["temperature"].Float());

			repo->seed = json["seed"].Integer();
			if(repo->seed == 0)
				repo->seed = CRandomGenerator::getDefault().nextInt();

			for(const auto & [key, node] : json["models"].Struct())
				repo->paths.try_emplace(key, "MMAI/models/" + node.String());

			fallback = json["fallback"].isNull() ? "BattleAI" : json["fallback"].String();
		}
		else
		{
			logAi->error("Could not load MMAI config. Is MMAI mod enabled?");
			fallback = "BattleAI";
		}

		logAi->debug("MMAI: repo initialized with fallback: %s", fallback);
		repo->fallbackModel = std::make_unique<ScriptedModel>(fallback);
		repo->fallbackName = fallback;

		return repo;
	}

	const ModelRepository * GetModelRepository()
	{
		static const auto repo = InitModelRepository();
		return repo.get();
	}

	Schema::IModel * GetModel(const std::string & key, bool requireNeuralModel)
	{
		const auto * repo = GetModelRepository();
		auto lock = std::lock_guard<std::mutex>(repo->mutex);

		std::shared_ptr<Schema::IModel> model = nullptr;

		// Search for "foo.bar.baz" -> "foo.bar" -> "foo"
		auto currentKey = key;
		auto keysToAdd = std::vector<std::string>{};

		while(true)
		{
			logAi->debug("MMAI: trying %s", currentKey);
			auto it = repo->models.find(currentKey);

			if(it != repo->models.end())
			{
				// Do not reuse a hybrid-cached substitute for a configured strict neural role.
				if(requireNeuralModel && !mayCacheResolvedNNModel(repo->paths, currentKey, it->second.second))
					throw std::runtime_error("MMAI: cached neural policy differs from configured model for " + currentKey);
				model = it->second.first;
				logAi->debug("MMAI: cache hit: %s (%s)", currentKey, it->second.second);
				break;
			}

			logAi->debug("MMAI: cache miss: %s", currentKey);
			keysToAdd.push_back(currentKey);

			auto itPath = repo->paths.find(currentKey);
			if(itPath != repo->paths.end())
			{
				const auto & path = itPath->second;
				logAi->debug("MMAI: Loading %s model from: %s", currentKey, path);
				try
				{
					model = CreateNNModel(path, repo->temperature, repo->seed);
					for(const auto & k : keysToAdd)
					{
						// A failed configured role may inherit its parent in hybrid mode,
						// but must not cache the substitute under that explicit role.
						if(!mayCacheResolvedNNModel(repo->paths, k, path))
							continue;
						logAi->debug("MMAI: cache write: %s (%s)", k, path);
						repo->models.try_emplace(k, model, path);
					}
					break;
				}
				catch(const std::exception & e)
				{
					logAi->error("MMAI: load error: %s: %s", currentKey, std::string(e.what()));
					// Refuse silently changing the neural policy after a failed
					// explicitly configured ONNX load. Legacy hybrid remains unchanged.
					enforceStrictNNModelLoad(requireNeuralModel, currentKey, path, e);
				}
			}
			else
			{
				logAi->warn("MMAI: load error: %s: no path configured", currentKey);
			}

			// Try next key (if any)
			auto pos = currentKey.rfind('.');
			if(pos == std::string::npos)
				break;

			currentKey = currentKey.substr(0, pos);
		}

		if(!model)
		{
			// A user-selected strict MMAI must never return even a temporary
			// scripted model to the router. Legacy hybrid callers may still use
			// the explicitly configured fallback, but strict selection fails here.
			if(requireNeuralModel)
			{
				logAi->error("MMAI: %s: no neural model available; refusing scripted fallback %s",
					key, repo->fallbackName);
				throw std::runtime_error("MMAI: no neural model available for " + key
					+ "; refusing scripted fallback " + repo->fallbackName + " under the MMAI identity");
			}
			logAi->error("MMAI: %s: falling back to %s", key, repo->fallbackName);
			ASSERT(repo->fallbackModel != nullptr, "fallback error: model is null");
			model = repo->fallbackModel;
		}

		return model.get();
	}

	// Convert a memory address for logging purposes
	std::string MakeAddrStr(const void * p)
	{
		std::ostringstream oss;
		oss << p;
		return oss.str();
	}

}

#define MMAI_LOG_TAG LogTag _(logtag + "." + __func__)

Router::Router(bool requireNeuralModel) : neuralOnly(requireNeuralModel), addrstr(MakeAddrStr(this)), basetag(addrstr + ":MMAI"), logtag(basetag) {}

Router::~Router() = default;

void Router::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB)
{
	env = ENV;
	cb = CB;
	colorname = cb->getPlayerID().value().toString();
	bai.reset();

	// Initialize all explicitly configured neural battle roles before the
	// first activeStack event. A GUI autobattle must not discover missing or
	// incompatible ONNX weights only after dispatching the first battle turn.
	// Models are cached by GetModel and reused unchanged at battleStart.
	// Unconfigured role variants retain their existing parent resolution.
	if(neuralOnly)
	{
		const auto * repo = GetModelRepository();
		bool hasConfiguredBattleRole = false;
		for(const auto & [key, path] : repo->paths)
		{
			const bool attacker = key == "attacker" || key == "attacker.siege";
			const bool defender = key == "defender" || key == "defender.siege";
			if(!attacker && !defender)
				continue;

			hasConfiguredBattleRole = true;
			auto * model = GetModel(key, true);
			validateModelSide(model->getSide(), attacker ? Schema::Side::LEFT : Schema::Side::RIGHT, true, key);
			logAi->info("MMAI: preloaded neural role %s from %s (schema V%d, side %d)",
				key, path, model->getVersion(), static_cast<int>(model->getSide()));
		}

		if(!hasConfiguredBattleRole)
			throw std::runtime_error("MMAI: no configured attacker/defender neural model roles; refusing scripted fallback");
	}
}

void Router::initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences prefs)
{
	MMAI_LOG_TAG;
	autocombatPreferences = prefs;
	initBattleInterface(ENV, CB);
}

/*
 * Delegated methods
 */

void Router::actionFinished(const BattleID & bid, const BattleAction & action)
{
	MMAI_LOG_TAG;
	bai->actionFinished(bid, action);
}

void Router::actionStarted(const BattleID & bid, const BattleAction & action)
{
	MMAI_LOG_TAG;
	bai->actionStarted(bid, action);
}

void Router::activeStack(const BattleID & bid, const CStack * astack)
{
	MMAI_LOG_TAG;
	bai->activeStack(bid, astack);
}

void Router::battleAttack(const BattleID & bid, const BattleAttack * ba)
{
	MMAI_LOG_TAG;
	bai->battleAttack(bid, ba);
}

void Router::battleCatapultAttacked(const BattleID & bid, const CatapultAttack & ca)
{
	MMAI_LOG_TAG;
	bai->battleCatapultAttacked(bid, ca);
}

void Router::battleEnd(const BattleID & bid, const BattleResult * br, QuestionID questionID)
{
	MMAI_LOG_TAG;
	bai->battleEnd(bid, br, questionID);
}

void Router::battleGateStateChanged(const BattleID & bid, const EGateState state)
{
	MMAI_LOG_TAG;
	bai->battleGateStateChanged(bid, state);
};

void Router::battleLogMessage(const BattleID & bid, const std::vector<MetaString> & lines)
{
	MMAI_LOG_TAG;
	bai->battleLogMessage(bid, lines);
};

void Router::battleNewRound(const BattleID & bid)
{
	MMAI_LOG_TAG;
	bai->battleNewRound(bid);
}

void Router::battleNewRoundFirst(const BattleID & bid)
{
	MMAI_LOG_TAG;
	bai->battleNewRoundFirst(bid);
}

void Router::battleObstaclesChanged(const BattleID & bid, const ObstacleChanges & obstacle)
{
	MMAI_LOG_TAG;
	bai->battleObstaclesChanged(bid, obstacle);
};

void Router::battleSpellCast(const BattleID & bid, const BattleSpellCast * sc)
{
	MMAI_LOG_TAG;
	bai->battleSpellCast(bid, sc);
}

void Router::battleStackMoved(const BattleID & bid, const CStack * stack, const BattleHexArray & dest, int distance, bool teleport)
{
	MMAI_LOG_TAG;
	bai->battleStackMoved(bid, stack, dest, distance, teleport);
}

void Router::battleStacksAttacked(const BattleID & bid, const std::vector<BattleStackAttacked> & bsa, bool ranged)
{
	MMAI_LOG_TAG;
	bai->battleStacksAttacked(bid, bsa, ranged);
}

void Router::battleStacksEffectsSet(const BattleID & bid, const SetStackEffect & sse)
{
	MMAI_LOG_TAG;
	bai->battleStacksEffectsSet(bid, sse);
}

void Router::battleStart(
	const BattleID & bid,
	const CCreatureSet * army1,
	const CCreatureSet * army2,
	int3 tile,
	const CGHeroInstance * hero1,
	const CGHeroInstance * hero2,
	BattleSide side,
	bool replayAllowed
)
{
	MMAI_LOG_TAG;
	Schema::IModel * model;

	std::string modelkey = side == BattleSide::ATTACKER ? "attacker" : "defender";

	if(cb->getBattle(bid)->battleGetWallState(EWallPart::GATE) != EWallState::NONE)
		modelkey += ".siege";

	model = GetModel(modelkey, neuralOnly);
	// The launcher-selected MMAI is advertised as a neural policy. Without a
	// loadable model, refusing the battle is safer than silently executing
	// BattleAI/StupidAI under the MMAI name. Legacy direct Router users may
	// still intentionally opt into the scripted fallback.
	if(neuralOnly && model->getType() != Schema::ModelType::NN)
		throw std::runtime_error("MMAI: no neural model available for " + modelkey
			+ "; refusing scripted fallback " + model->getName() + " under the MMAI identity");


	logtag = basetag + ".v" + std::to_string(model->getVersion());
	LogTag _2(logtag + "." + __func__);

	auto modelside = model->getSide();
	auto realside = static_cast<Schema::Side>(EI(side));

	if(!validateModelSide(modelside, realside, neuralOnly, modelkey))
	{
		logAi->warn(
			"The loaded '%s' model was not trained to play as %s (modelside=%d, realside=%d)",
			modelkey,
			modelkey,
			static_cast<int>(modelside),
			static_cast<int>(realside)
		);
	}

	switch(model->getType())
	{
		case Schema::ModelType::SCRIPTED:
			if(model->getName() == "StupidAI")
			{
				bai = std::make_shared<CStupidAI>();
				bai->initBattleInterface(env, cb, autocombatPreferences);
			}
			else if(model->getName() == "BattleAI")
			{
				bai = std::make_shared<CBattleAI>();
				bai->initBattleInterface(env, cb, autocombatPreferences);
			}
			else
			{
				THROW_FORMAT("Unexpected scripted model name: %s", model->getName());
			}
			break;
		case Schema::ModelType::NN:
			// XXX: must not call initBattleInterface here
			bai = CreateBAI(model, env, cb, autocombatPreferences.enableSpellsUsage, autocombatPreferences.enableTacticsUsage, neuralOnly);
			break;

		default:
			THROW_FORMAT("Unexpected model type: %d", EI(model->getType()));
	}

	bai->battleStart(bid, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void Router::battleTriggerEffect(const BattleID & bid, const BattleTriggerEffect & bte)
{
	MMAI_LOG_TAG;
	bai->battleTriggerEffect(bid, bte);
}

void Router::battleUnitsChanged(const BattleID & bid, const std::vector<UnitChanges> & changes)
{
	MMAI_LOG_TAG;
	bai->battleUnitsChanged(bid, changes);
}

void Router::yourTacticPhase(const BattleID & bid, int distance)
{
	MMAI_LOG_TAG;
	bai->yourTacticPhase(bid, distance);
}
}
