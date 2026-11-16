// Native ammunition fixtures, inside AIIntegrationNativeBattle.
	void ammoAssertOrdinary(const CStack * unit)
	{
		ASSERT_NE(unit, nullptr);
		ASSERT_TRUE(unit->alive());
		ASSERT_EQ(unit->unitOwner(), battle()->battleGetOwner(unit));
		ASSERT_FALSE(unit->isClone());
		for(const auto type : {BonusType::ATTACKS_ALL_ADJACENT, BonusType::TWO_HEX_ATTACK_BREATH,
			BonusType::JOUSTING, BonusType::FIRST_STRIKE, BonusType::SPELL_AFTER_ATTACK,
			BonusType::SPELL_BEFORE_ATTACK, BonusType::SPELL_LIKE_ATTACK})
			ASSERT_FALSE(unit->hasBonusOfType(type));
	}

	void ammoChoose(CStack * actor, BattleAction & result)
	{
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		const auto id = battle()->getBattleID();
		const auto player = battle()->battleGetOwner(actor);
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		auto policy = AIFactory::createBattleAI("FHeroes2Tactics");
		ASSERT_NE(policy, nullptr);
		policy->playerID = player;
		auto environment = std::make_shared<NativePortEnvironment>(makeCallback(player), callback);
		AutocombatPreferences preferences;
		preferences.enableSpellsUsage = false;
		policy->initBattleInterface(environment, callback, preferences);
		policy->battleStart(id, battle()->getSideArmy(BattleSide::ATTACKER), battle()->getSideArmy(BattleSide::DEFENDER),
			battle()->getLocation(), attackerSideHero, defenderSideHero, actor->unitSide(), false);
		const auto snapshot = ammoSnapshot(*battle());
		policy->activeStack(id, actor);
		ASSERT_TRUE(callback->action.has_value());
		ASSERT_FALSE(callback->spellAction);
		ammoCheckSnapshot(snapshot, *battle());
		result = *callback->action;
		callback->action.reset();
		policy->activeStack(id, actor);
		ASSERT_TRUE(callback->action.has_value());
		EXPECT_EQ(callback->action->toString(), result.toString());
		ammoCheckSnapshot(snapshot, *battle());
	}

	void ammoAcceptAndComplete(CStack * actor, const BattleAction & action, const CStack * victim = nullptr)
	{
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		const auto origin = actor->getPosition();
		const auto health = victim ? victim->getAvailableHealth() : 0;
		const auto damage = recorder.damage;
		const auto movements = recorder.movementPackets;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),
			battle()->battleGetOwner(actor), action));
		ASSERT_FALSE(gameState()->currentBattles.empty()) << "A competing enemy must survive the decisive action";
		if(victim)
		{
			EXPECT_LT(victim->getAvailableHealth(), health);
			EXPECT_GT(recorder.damage, damage);
		}
		else
		{
			EXPECT_NE(actor->getPosition(), origin);
			EXPECT_EQ(actor->getPosition(), action.target.front().hexValue);
			EXPECT_GT(recorder.movementPackets, movements);
		}
		runToCompletion(std::nullopt, false, "FHeroes2Tactics", "HoMM3OriginalTactics");
		EXPECT_TRUE(gameState()->currentBattles.empty());
		EXPECT_GT(recorder.acceptedResults, 0);
	}

	void exerciseAmmoPosition(AmmoPositionMode mode)
	{
		const bool mixed = mode == AmmoPositionMode::Mixed || mode == AmmoPositionMode::WideMixed;
		const bool wide = mode == AmmoPositionMode::WideMixed;
		const bool loaded = mode == AmmoPositionMode::LoadedBlocked || mode == AmmoPositionMode::Forgotten;
		prepareBattle(CreatureID(1), 1000, CreatureID(0), 100);
		ASSERT_FALSE(HasFatalFailure());
		battle()->obstacles.clear();
		CStack * actor = nullptr;
		CStack * isolated = nullptr;
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::ATTACKER) actor = unit.get(); else isolated = unit.get();
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(isolated, nullptr);
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		actor->setPosition(BattleHex(8, 5));
		isolated->setPosition(BattleHex(6, 5));
		const BattleHex cluster(11, 5);
		const auto firstHex = cluster.cloneInDirection(BattleHex::TOP_RIGHT);
		const auto secondHex = cluster.cloneInDirection(BattleHex::RIGHT);
		auto * first = addStack(BattleSide::DEFENDER, CreatureID(2), firstHex, 10);
		auto * second = addStack(BattleSide::DEFENDER, (wide ? CreatureID(CreatureID::BALLISTA) : CreatureID(2)), secondHex, wide ? 1 : 10);
		auto * melee = mixed ? addStack(BattleSide::DEFENDER, CreatureID(0),
			cluster.cloneInDirection(BattleHex::BOTTOM_RIGHT), 10) : nullptr;
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		ASSERT_TRUE(first->isShooter());
		ASSERT_TRUE(second->isShooter());
		ASSERT_EQ(second->doubleWide(), wide);
		if(wide)
		{
			ASSERT_EQ(second->creatureId(), CreatureID(CreatureID::BALLISTA));
			ASSERT_TRUE(second->hasBonusOfType(BonusType::SIEGE_WEAPON));
			ASSERT_FALSE(second->isCatapult());
			ASSERT_FALSE(second->isInvincible());
			ASSERT_TRUE(second->shots.canUse(1));
			ASSERT_GT(second->shots.available(), 0);
		}
		ASSERT_FALSE(actor->doubleWide());
		ASSERT_FALSE(actor->isShooter());
		ASSERT_FALSE(actor->hasBonusOfType(BonusType::FLYING));
		for(auto * unit : {actor, isolated, first, second})
		{
			ammoAssertOrdinary(unit);
			ASSERT_FALSE(HasFatalFailure());
		}
		if(melee) { ammoAssertOrdinary(melee); ASSERT_FALSE(HasFatalFailure()); }
		ASSERT_FALSE(first->unitHasAmmoCart(first));
		ASSERT_FALSE(second->unitHasAmmoCart(second));
		if(!loaded)
		{
			if(!mixed) first->shots.use(first->shots.available());
			second->shots.use(second->shots.available());
			ASSERT_EQ(second->shots.available(), 0);
			ASSERT_FALSE(second->shots.canUse(1));
		}
		if(mode == AmmoPositionMode::Supplied)
		{
			giveArtifact(defenderSideHero, ArtifactID::AMMO_CART, ArtifactPosition::MACH2);
			auto * cart = addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(15, 0), 1);
			ASSERT_NE(cart, nullptr);
			ASSERT_TRUE(cart->isAmmoCart());
			ASSERT_TRUE(cart->alive());
			ASSERT_NE(defenderSideHero->getArt(ArtifactPosition::MACH2), nullptr);
			ASSERT_TRUE(battle()->battleMatchOwner(cart, second, true));
			for(auto * archer : {first, second})
			{
				ASSERT_EQ(archer->shots.available(), 0);
				ASSERT_EQ(archer->getBattle(), battle());
				ASSERT_TRUE(archer->unitHasAmmoCart(archer));
				ASSERT_TRUE(archer->shots.canUse(1));
			}
		}
		if(mode == AmmoPositionMode::LoadedBlocked)
		{
			for(const auto & hex : {firstHex.cloneInDirection(BattleHex::TOP_RIGHT),
				second->getHexes().back().cloneInDirection(BattleHex::BOTTOM_RIGHT)})
			{
				ASSERT_EQ(battle()->battleGetUnitByPos(hex), nullptr);
				ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(0), hex, 1), nullptr);
			}
		}
		if(mode == AmmoPositionMode::Forgotten)
		{
			teachBattleSpell(attackerSideHero, SpellID::FORGETFULNESS);
			attackerSideHero->setSecSkillLevel(SecondarySkill::WATER_MAGIC, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(castOn(attackerSideHero, SpellID::FORGETFULNESS, first));
			ASSERT_TRUE(first->hasBonusOfType(BonusType::FORGETFULL));
			ASSERT_TRUE(second->hasBonusOfType(BonusType::FORGETFULL));
		}
		if(loaded)
			for(const auto * archer : {first, second})
			{
				ASSERT_TRUE(archer->shots.canUse(1));
				ASSERT_FALSE(battle()->battleCanShoot(archer));
			}
		const auto player = battle()->battleGetOwner(actor);
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		const auto view = callback->getBattle(battle()->getBattleID());
		const auto routes = view->getReachability(actor);
		const auto available = view->battleGetAvailableHexes(routes, actor, false);
		ASSERT_TRUE(available.contains(cluster));
		ASSERT_FALSE(view->meleeAttackHexes(actor, first, cluster).empty());
		ASSERT_FALSE(view->meleeAttackHexes(actor, second, cluster).empty());
		if(wide)
		{
			bool twoAims = false;
			for(const auto & from : available)
				if(view->meleeAttackHexes(actor, second, from).size() == 2) twoAims = true;
			ASSERT_TRUE(twoAims) << "The wide regression must expose both head and tail aims";
		}
		const auto at = [&](const std::vector<AmmoAttackOption> & options, const CStack * enemy, BattleHex from)
		{
			for(const auto & option : options)
				if(option.enemy == enemy && option.from == from) return option;
			return AmmoAttackOption{};
		};
		const auto initial = ammoPositionOracle(*view, *actor, AmmoOracleClass::Usable);
		const auto a = at(initial, first, cluster);
		const auto b = at(initial, second, cluster);
		ASSERT_NE(a.enemy, nullptr);
		ASSERT_NE(b.enemy, nullptr);
		const auto m = melee ? at(initial, melee, cluster) : AmmoAttackOption{};
		if(mixed) ASSERT_NE(m.enemy, nullptr);
		const double lower = mixed ? a.hitValue + std::max(b.hitValue, m.hitValue) : std::max(a.hitValue, b.hitValue);
		const double upper = a.hitValue + b.hitValue + (mixed ? m.hitValue : 0.0);
		ASSERT_GT(upper, lower + 0.002);
		// Native injury packets select an integer-size isolated target inside
		// the strict bracket; the policy never participates in fixture selection.
		bool bracket = false;
		for(int count = isolated->getCount(); count > 1; --count)
		{
			const auto options = ammoPositionOracle(*view, *actor, AmmoOracleClass::Usable);
			double isolatedValue = -std::numeric_limits<double>::infinity();
			for(const auto & option : options)
				if(option.enemy == isolated) isolatedValue = std::max(isolatedValue, option.hitValue);
			if(isolatedValue > lower + 0.001 && isolatedValue < upper - 0.001) { bracket = true; break; }
			injure(isolated, isolated->getMaxHealth());
		}
		ASSERT_TRUE(bracket) << "No native count meets max < isolated < sum";
		const auto options = ammoPositionOracle(*view, *actor, AmmoOracleClass::Usable);
		const auto expected = ammoBestPosition(options);
		ASSERT_NE(expected.enemy, nullptr);
		const bool clusterWins = loaded || mode == AmmoPositionMode::Supplied;
		if(clusterWins) ASSERT_TRUE(expected.enemy == first || expected.enemy == second);
		else ASSERT_EQ(expected.enemy, isolated);
		const auto wrongClass = clusterWins ? (mode == AmmoPositionMode::Supplied
			? AmmoOracleClass::Counter : AmmoOracleClass::ShootLegal) : AmmoOracleClass::Identity;
		const auto wrong = ammoBestPosition(ammoPositionOracle(*view, *actor, wrongClass));
		ASSERT_NE(expected.enemy, wrong.enemy);
		ASSERT_NE(expected.from, wrong.from);
		double competing = -std::numeric_limits<double>::infinity();
		for(const auto & option : options)
		{
			std::cout << "AMMO_POSITION_OPTION unit=" << option.enemy->unitId() << " from=" << option.from
				<< " aim=" << option.aim << " exchange=" << option.hitValue << " position=" << option.positionValue << '\n';
			if((option.enemy == isolated) == clusterWins) competing = std::max(competing, option.positionValue);
		}
		ASSERT_GT(expected.positionValue, competing + 0.001);
		const auto clusterValue = at(options, first, cluster).positionValue;
		ASSERT_DOUBLE_EQ(clusterValue, clusterWins ? upper : mixed ? lower : std::max(a.hitValue, b.hitValue));
		const auto stance = AIIntegration::FHeroes2TacticsContext::tactics(*view, *actor,
			view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY));
		ASSERT_FALSE(stance.defensive);
		std::cout << "AMMO_POSITION_BRACKET lower=" << lower << " upper=" << upper
			<< " expected=" << expected.from << " wrong=" << wrong.from << " target=" << expected.enemy->unitId() << '\n';
		BattleAction action;
		ammoChoose(actor, action);
		ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
		ASSERT_GE(action.target.size(), 2u);
		EXPECT_EQ(action.target[0].hexValue, expected.from);
		EXPECT_EQ(action.target[1].hexValue, expected.aim);
		ASSERT_EQ(battle()->battleGetUnitByPos(action.target[1].hexValue), expected.enemy);
		ammoAcceptAndComplete(actor, action, expected.enemy);
	}

	struct AmmoPathProof
	{
		const CStack * enemy = nullptr;
		BattleHex goal;
		double score = 0;
		bool cannotEvade = false;
		std::vector<std::pair<BattleHex, double>> risks;
		BattleHex step;
	};

	bool ammoEngaged(const CStack & enemy, CPlayerBattleCallback & view)
	{
		for(const auto * ally : view.battleGetStacks(CBattleInfoEssentials::ONLY_MINE))
			for(const auto & own : ally->getHexes())
				for(const auto & other : enemy.getHexes())
					if(BattleHex::getDistance(own, other) == 1) return true;
		return false;
	}

	// Distant-target proof uses native damage and the documented port mapping,
	// not threatAgainst/projectRelativeThreat/preferDistantTarget.
	double ammoDistantScore(CPlayerBattleCallback & view, const CStack & enemy, const CStack & actor, int route)
	{
		int distance = GameConstants::BFIELD_SIZE;
		for(const auto & a : actor.getHexes())
			for(const auto & b : enemy.getHexes())
				distance = std::min(distance, static_cast<int>(BattleHex::getDistance(a, b)));
		distance = std::max(1, distance);
		const bool ranged = enemy.isShooter() && view.battleCanShoot(&enemy);
		BattleAttackInfo attack(&enemy, &actor, distance, ranged);
		DamageEstimation retaliation;
		const auto damage = view.battleEstimateDamage(attack, &retaliation);
		double value = (static_cast<double>(damage.damage.min) + damage.damage.max) / 2.0;
		if(!ranged && !enemy.hasBonusOfType(BonusType::FLYING))
		{
			if(enemy.getMovementRange() == 0) return 0;
			if(distance > enemy.getMovementRange() + 1)
				value *= static_cast<double>(enemy.getMovementRange()) / (1.5 * distance);
		}
		if(enemy.getTotalAttacks(ranged) > 1)
		{
			const double response = ranged ? 0.0 : (static_cast<double>(retaliation.damage.min) + retaliation.damage.max) / 2.0;
			value *= 2.0 - std::min(response / std::max<double>(1, enemy.getAvailableHealth()), 1.0);
		}
		if(enemy.hasBonusOfType(BonusType::NOT_ACTIVE)) value = 0;
		else if(enemy.moved()) value /= 1.25;
		return value / std::max(1, route);
	}

	AmmoPathProof ammoPathOracle(CPlayerBattleCallback & view, const CStack & actor,
		const CStack * forcePursuer = nullptr, bool ignorePursuer = false)
	{
		const auto routes = view.getReachability(&actor);
		const auto available = view.battleGetAvailableHexes(routes, &actor, false);
		auto enemies = view.battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY);
		std::sort(enemies.begin(), enemies.end(), [](const CStack * a, const CStack * b) { return a->unitId() < b->unitId(); });
		AmmoPathProof best;
		for(const auto * enemy : enemies)
		{
			if(!enemy->alive() || enemy->isInvincible() || !view.battleCanAttackUnit(&actor, enemy)) continue;
			BattleHex goal;
			int route = GameConstants::BFIELD_SIZE;
			// Enumerate the entire native board, not the policy's attackable-hex list.
			for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
			{
				const BattleHex from(i);
				if(!from.isAvailable() || !routes.isReachable(from)
					|| !view.isMeleeAttackPossible(&actor, enemy, from)) continue;
				const int distance = routes.distances[i];
				if(distance < route || (distance == route && (!goal.isValid() || i < goal.toInt())))
				{ goal = from; route = distance; }
			}
			if(!goal.isValid()) continue;
			const auto speed = enemy->hasBonusOfType(BonusType::NOT_ACTIVE) ? 0 : enemy->getMovementRange();
			const bool cannotEvade = (enemy->isShooter() && enemy->shots.canUse(1)) || speed == 0
				|| (!enemy->hasBonusOfType(BonusType::FLYING) && speed < actor.getMovementRange());
			const double score = ammoDistantScore(view, *enemy, actor, route);
			if(!best.enemy || (cannotEvade && !best.cannotEvade)
				|| (cannotEvade == best.cannotEvade && score >= best.score))
			{ best.enemy = enemy; best.goal = goal; best.score = score; best.cannotEvade = cannotEvade; }
		}
		if(!best.enemy) return best;
		std::vector<BattleHex> path;
		for(BattleHex cell = best.goal; cell.isValid();)
		{
			if(available.contains(cell) && cell != actor.getPosition()) path.push_back(cell);
			const auto previous = routes.predecessors[cell.toInt()];
			if(previous == cell) break;
			cell = previous;
		}
		std::reverse(path.begin(), path.end());
		double minimum = std::numeric_limits<double>::infinity();
		for(const auto & step : path)
		{
			double risk = 0;
			for(const auto * enemy : enemies)
			{
				if(!enemy->alive()) continue;
				if(enemy == forcePursuer && ignorePursuer) continue;
				if(enemy != forcePursuer)
				{
					if(enemy->hasBonusOfType(BonusType::FLYING) || enemy->hasBonusOfType(BonusType::NOT_ACTIVE)) continue;
					if(enemy->isShooter() && enemy->shots.canUse(1) && !ammoEngaged(*enemy, view)) continue;
				}
				const int speed = enemy->getMovementRange();
				if(speed <= 0) continue;
				int separation = GameConstants::BFIELD_SIZE;
				for(const auto & own : actor.getHexes(step))
					for(const auto & other : enemy->getHexes())
						separation = std::min(separation, static_cast<int>(BattleHex::getDistance(own, other)));
				if(separation <= speed + 1) risk += enemy->estimateCombatValue();
			}
			best.risks.emplace_back(step, risk);
			if(!best.step.isValid() || risk < minimum || std::fabs(risk - minimum) < 0.001)
			{ minimum = risk; best.step = step; }
		}
		return best;
	}

	void exerciseAmmoPath(AmmoPathMode mode)
	{
		prepareBattle(CreatureID(1), 40, CreatureID(2), 1);
		ASSERT_FALSE(HasFatalFailure());
		battle()->obstacles.clear();
		CStack * actor = nullptr;
		CStack * archer = nullptr;
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::ATTACKER) actor = unit.get(); else archer = unit.get();
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(archer, nullptr);
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		actor->setPosition(BattleHex(2, 5));
		archer->setPosition(BattleHex(mode == AmmoPathMode::AlreadyMoved ? 12 : 11, 4));
		auto * goal = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(14, 5), 200);
		ASSERT_NE(goal, nullptr);
		ASSERT_FALSE(actor->doubleWide());
		ASSERT_FALSE(actor->isShooter());
		ASSERT_FALSE(actor->hasBonusOfType(BonusType::FLYING));
		ASSERT_TRUE(archer->isShooter());
		ASSERT_FALSE(archer->doubleWide());
		ASSERT_FALSE(archer->unitHasAmmoCart(archer));
		ASSERT_LT(goal->getMovementRange(), actor->getMovementRange());
		ASSERT_LT(archer->getMovementRange(), actor->getMovementRange());
		for(const auto * unit : {actor, archer, goal}) { ammoAssertOrdinary(unit); ASSERT_FALSE(HasFatalFailure()); }
		const bool loaded = mode == AmmoPathMode::Loaded || mode == AmmoPathMode::Engaged;
		if(!loaded)
		{
			archer->shots.use(archer->shots.available() - (mode == AmmoPathMode::LastShot ? 1 : 0));
			ASSERT_EQ(archer->shots.available(), mode == AmmoPathMode::LastShot ? 1 : 0);
		}
		if(mode == AmmoPathMode::Engaged)
		{
			ASSERT_NE(addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(11, 3), 1), nullptr);
			ASSERT_FALSE(battle()->battleCanShoot(archer));
		}
		if(mode == AmmoPathMode::Blind)
		{
			teachBattleSpell(attackerSideHero, SpellID::BLIND);
			ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLIND, archer));
			ASSERT_TRUE(archer->hasBonusOfType(BonusType::NOT_ACTIVE));
			ASSERT_GT(archer->getMovementRange(), 0);
		}
		if(mode == AmmoPathMode::Flying)
		{
			archer->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::FLYING, BonusSource::OTHER, 1, BonusSourceID()));
			ASSERT_TRUE(archer->hasBonusOfType(BonusType::FLYING));
		}
		if(mode == AmmoPathMode::AlreadyMoved || mode == AmmoPathMode::LastShot)
		{
			const auto id = battle()->getBattleID();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id, battle()->battleGetOwner(actor), BattleAction::makeWait(actor)));
			bool archerActed = false;
			for(int turns = 0; turns < 12 && battle()->battleActiveUnit() != actor; ++turns)
			{
				const auto * active = battle()->battleActiveUnit();
				ASSERT_NE(active, nullptr);
				if(active == archer)
				{
					ASSERT_FALSE(archerActed);
					archerActed = true;
					if(mode == AmmoPathMode::LastShot)
					{
						ASSERT_TRUE(archer->shots.canUse(1));
						ASSERT_EQ(archer->shots.available(), 1);
						ASSERT_TRUE(battle()->battleCanShoot(archer, actor->getPosition()));
						const auto shots = recorder.shots;
						const auto health = actor->getAvailableHealth();
						ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id, battle()->battleGetOwner(archer), BattleAction::makeShotAttack(archer, actor)));
						ASSERT_EQ(recorder.shots, shots + 1);
						ASSERT_LT(actor->getAvailableHealth(), health);
						ASSERT_EQ(archer->shots.available(), 0);
						ASSERT_FALSE(archer->shots.canUse(1));
						continue;
					}
					const BattleHex destination(11, 4);
					const auto origin = archer->getPosition();
					const auto movementPackets = recorder.movementPackets;
					ASSERT_NE(origin, destination);
					ASSERT_TRUE(battle()->battleGetAvailableHexes(archer, false).contains(destination));
					ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,
						battle()->battleGetOwner(archer), BattleAction::makeMove(archer, destination)));
					ASSERT_EQ(archer->getPosition(), destination);
					ASSERT_GT(recorder.movementPackets, movementPackets);
					ASSERT_EQ(archer->getTurnStatus(), battle::UnitTurnStatus::ACTED);
					ASSERT_TRUE(archer->moved());
					ASSERT_FALSE(archer->defended());
					std::cout << "AMMO_PATH_ACTED origin=" << origin << " destination=" << destination
						<< " movementPackets=" << recorder.movementPackets - movementPackets << '\n';
					continue;
				}
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id, battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
			}
			ASSERT_TRUE(archerActed);
			ASSERT_EQ(archer->getTurnStatus(), battle::UnitTurnStatus::ACTED);
			ASSERT_TRUE(archer->moved());
			ASSERT_FALSE(archer->defended());
			ASSERT_EQ(battle()->battleActiveUnit(), actor);
		}
		ASSERT_EQ(archer->shots.canUse(1), loaded);
		const auto player = battle()->battleGetOwner(actor);
		auto callback = std::make_shared<DecisionCallback>(player);
		callback->onBattleStarted(battle());
		const auto view = callback->getBattle(battle()->getBattleID());
		ASSERT_EQ(ammoEngaged(*archer, *view), mode == AmmoPathMode::Engaged);
		ASSERT_TRUE(ammoPositionOracle(*view, *actor, AmmoOracleClass::Usable).empty())
			<< "No immediate melee candidate may bypass cautious approach";
		// Read the live strengths after the real last shot and ordinary scheduler.
		double enemyStrength = 0;
		double rangedStrength = 0;
		for(const auto * enemy : view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
		{
			enemyStrength += enemy->estimateCombatValue();
			if(enemy->isShooter() && enemy->shots.canUse(1) && !enemy->hasBonusOfType(BonusType::NOT_ACTIVE))
				rangedStrength += enemy->estimateCombatValue();
		}
		ASSERT_GT(enemyStrength, 0);
		ASSERT_LT(rangedStrength / enemyStrength, 0.15);
		const auto stance = AIIntegration::FHeroes2TacticsContext::tactics(*view, *actor,
			view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY));
		ASSERT_FALSE(stance.defensive);
		ASSERT_TRUE(stance.cautiousOffensive);
		const auto expected = ammoPathOracle(*view, *actor);
		const auto threatened = ammoPathOracle(*view, *actor, archer, false);
		const auto exempt = ammoPathOracle(*view, *actor, archer, true);
		ASSERT_EQ(expected.enemy, goal);
		ASSERT_EQ(threatened.enemy, goal);
		ASSERT_EQ(exempt.enemy, goal);
		ASSERT_EQ(expected.goal, threatened.goal);
		ASSERT_EQ(expected.goal, exempt.goal);
		ASSERT_TRUE(expected.cannotEvade);
		ASSERT_GT(expected.score, 0);
		const auto routes = view->getReachability(actor);
		for(const auto * competitor : view->battleGetStacks(CBattleInfoEssentials::ONLY_ENEMY))
		{
			if(competitor == goal) continue;
			int route = GameConstants::BFIELD_SIZE;
			for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
				if(routes.isReachable(BattleHex(i)) && view->isMeleeAttackPossible(actor, competitor, BattleHex(i)))
					route = std::min(route, static_cast<int>(routes.distances[i]));
			const double competing = ammoDistantScore(*view, *competitor, *actor, route);
			ASSERT_GT(expected.score, competing + 0.001) << "Winning goal must not depend on enemy visitation order";
			std::cout << "AMMO_PATH_GOAL winner=" << goal->unitId() << " score=" << expected.score
				<< " competitor=" << competitor->unitId() << " score=" << competing << '\n';
		}
		ASSERT_TRUE(threatened.step.isValid());
		ASSERT_TRUE(exempt.step.isValid());
		ASSERT_NE(threatened.step, exempt.step);
		ASSERT_LT(routes.distances[threatened.step.toInt()], routes.distances[exempt.step.toInt()]);
		const bool contributes = mode == AmmoPathMode::Exhausted || mode == AmmoPathMode::Engaged
			|| mode == AmmoPathMode::AlreadyMoved || mode == AmmoPathMode::LastShot;
		ASSERT_EQ(expected.step, contributes ? threatened.step : exempt.step);
		// On this open single-hex geometry, verify the risk boundary against
		// native reachable attack-from cells, not only a distance formula.
		const auto enemyRoutes = view->getReachability(archer);
		const auto enemyPositions = view->battleGetAvailableHexes(enemyRoutes, archer, false);
		const auto nativeCanApproach = [&](BattleHex step)
		{
			for(const auto & from : enemyPositions)
				if(from.isAvailable() && enemyRoutes.distances[from.toInt()] <= archer->getMovementRange()
					&& view->isMeleeAttackPossible(archer, actor, from, step)) return true;
			return false;
		};
		ASSERT_GT(BattleHex::getDistance(archer->getPosition(), threatened.step), archer->getMovementRange() + 1);
		ASSERT_LE(BattleHex::getDistance(archer->getPosition(), exempt.step), archer->getMovementRange() + 1);
		ASSERT_FALSE(nativeCanApproach(threatened.step));
		ASSERT_TRUE(nativeCanApproach(exempt.step));
		for(size_t i = 0; i < threatened.risks.size(); ++i)
		{
			ASSERT_EQ(threatened.risks[i].first, exempt.risks[i].first);
			std::cout << "AMMO_PATH_RISK step=" << threatened.risks[i].first << " melee=" << threatened.risks[i].second
				<< " exempt=" << exempt.risks[i].second << '\n';
		}
		const auto riskAt = [](const AmmoPathProof & proof, BattleHex step)
		{
			for(const auto & entry : proof.risks) if(entry.first == step) return entry.second;
			return -1.0;
		};
		ASSERT_GT(riskAt(threatened, exempt.step), riskAt(threatened, threatened.step) + 0.001);
		ASSERT_DOUBLE_EQ(riskAt(exempt, threatened.step), riskAt(exempt, exempt.step));
		std::cout << "AMMO_PATH_CHOICE goal=" << expected.goal << " safer=" << threatened.step
			<< " farther=" << exempt.step << " expected=" << expected.step << " moved=" << archer->moved() << '\n';
		BattleAction action;
		ammoChoose(actor, action);
		ASSERT_FALSE(HasFatalFailure());
		ASSERT_EQ(action.actionType, EActionType::WALK);
		ASSERT_FALSE(action.target.empty());
		EXPECT_EQ(action.target.front().hexValue, expected.step);
		ammoAcceptAndComplete(actor, action);
	}
