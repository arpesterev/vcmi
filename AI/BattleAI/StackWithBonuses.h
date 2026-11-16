/*
 * StackWithBonuses.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <vstd/RNG.h>

#include <vcmi/Environment.h>
#include <vcmi/ServerCallback.h>

#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleProxy.h"
#include "../../lib/battle/CUnitState.h"

class HypotheticBattle;
class HypotheticFrameArena;
class OrdinaryUnitViewStore;
class BattleInfo;

///Fake random generator, used by AI to evaluate random server behavior
class RNGStub final : public vstd::RNG
{
public:
	int nextInt() override
	{
		return 0;
	}

	int nextBinomialInt(int coinsCount, double coinChance) override
	{
		return coinsCount * coinChance;
	}

	int nextInt(int lower, int upper) override
	{
		return (lower + upper) / 2;
	}
	int64_t nextInt64(int64_t lower, int64_t upper) override
	{
		return (lower + upper) / 2;
	}
	double nextDouble(double lower, double upper) override
	{
		return (lower + upper) / 2;
	}

	int nextInt(int upper) override
	{
		return upper / 2;
	}
	int64_t nextInt64(int64_t upper) override
	{
		return upper / 2;
	}
	double nextDouble(double upper) override
	{
		return upper / 2;
	}
};

class StackWithBonuses : public battle::CUnitState, public virtual IBonusBearer
{
public:
	std::vector<Bonus> bonusesToAdd;
	std::vector<Bonus> bonusesToUpdate;
	std::set<std::shared_ptr<Bonus>> bonusesToRemove;
	int treeVersionLocal;

	StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info);

	virtual ~StackWithBonuses();

	StackWithBonuses & operator= (const battle::CUnitState & other);

	///IUnitInfo
	const Creature * unitType() const override;

	int32_t unitBaseAmount() const override;

	uint32_t unitId() const override;
	BattleSide unitSide() const override;
	PlayerColor unitOwner() const override;
	SlotID unitSlot() const override;

	///IBonusBearer
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;

	int32_t getTreeVersion() const override;

	void addUnitBonus(const std::vector<Bonus> & bonus);
	void updateUnitBonus(const std::vector<Bonus> & bonus);
	void removeUnitBonus(const std::vector<Bonus> & bonus);

	void removeUnitBonus(const CSelector & selector);

	void spendMana(ServerCallback * server, const int spellCost) const override;
	std::string getDescription() const override;

protected:
 // Owned-frame subclass sets every identity value and supplies its own bearer.
 StackWithBonuses(const Creature * type, uint32_t amount, uint32_t id,
  BattleSide side, PlayerColor owner, SlotID slot);
 virtual bool hasOwnedFrame() const { return false; }
 enum class LocalBonusMutation { Add, Update, Remove };
 virtual bool mutateLocalBonuses(LocalBonusMutation, const std::vector<Bonus> &) { return false; }
 virtual bool removeLocalBonuses(const CSelector &) { return false; }
private:
 const IBonusBearer * origBearer;
	const HypotheticBattle * owner;

	const Creature * type;
	ui32 baseAmount;
	uint32_t id;
	BattleSide side;
	PlayerColor player;
	SlotID slot;
};

class OwnedLogicalBonusEvent;
struct OwnedStackEffect;
struct OwnedFreshStackEffect;
struct SetStackEffect;
class HypotheticBattle final : public BattleProxy, public battle::IUnitEnvironment
{
public:
	std::map<uint32_t, std::shared_ptr<StackWithBonuses>> stackStates;

	const Environment * env;

	HypotheticBattle(const Environment * ENV, Subject realBattle);
 // Experimental, serialized caller-held native read epoch. No AI enablement.
 static std::shared_ptr<HypotheticBattle> captureOwnedFixture(const BattleInfo & battle);
 std::shared_ptr<HypotheticBattle> forkOwnedFixture() const;
 // Typed owned events only. Raw external component vectors remain unsupported.
 OwnedLogicalBonusEvent makeOwnedBonusEvent(const std::vector<Bonus> & values) const;
 OwnedLogicalBonusEvent ownedEventFromUnitExports(uint32_t id, const CSelector & selector) const;
 void addUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event);
 void updateUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event);
 void removeUnitBonus(uint32_t id, const OwnedLogicalBonusEvent & event);
 /// Certify a component-free packet without applying it; rejects foreign battle IDs.
 OwnedStackEffect makeOwnedScalarStackEffect(const SetStackEffect & effect) const;
 /// Constructs fresh value-described components; all native component handles are rejected.
 OwnedStackEffect makeOwnedFreshStackEffect(const OwnedFreshStackEffect & effect) const;
 void applyOwnedStackEffect(const OwnedStackEffect & effect);
 /// Bonuses only; no turn state, log, combat events or obstacle processing.
 int applyOwnedDefendBonuses(uint32_t id);
 /// Bonus-only native expiry phases. Caller owns state updates and combat dispatch.
 void expireOwnedRoundBonuses(bool firstRound);
 void expireOwnedTurnBonuses(uint32_t id, BattleUnitTurnReason reason);
 void expireOwnedAttackBonuses(uint32_t id, bool counter);
 void expireOwnedDamageBonuses(uint32_t id, int64_t healthDelta);
 /// Ordered StacksInjured pre-load phase; not gated on health delta.
 void expireOwnedIndirectDamageBonuses(const std::vector<uint32_t> & ids);
 /// Query checkpoint at the caller-selected native melee/ranged sequence boundary.
 OwnedStackEffect captureOwnedScalarSequenceCleanup(const std::vector<uint32_t> & ids) const;
 /// Bounded produced-state packet allowlist. No combat generation.
 void applyOwnedStatePacket(CPackForClient & packet);


	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;

	std::shared_ptr<StackWithBonuses> getForUpdate(uint32_t id);
	/// Copies scalar state without lifecycle processing or bonus changes.
	std::shared_ptr<StackWithBonuses> copyUnitState(uint32_t id, const battle::CUnitState & state);
	void damageUnit(uint32_t id, int64_t & amount);
	void recordUnitAttack(uint32_t id, bool ranged, bool counter);

 std::optional<PlayerColor> getPlayerID() const override;
 TStacks getStacksIf(const TStackFilter & predicate) const override;
 BattleField getBattlefieldType() const override;
 TerrainId getTerrainType() const override;
 ObstacleCList getAllObstacles() const override;
 PlayerColor getSidePlayer(BattleSide side) const override;
 const CArmedInstance * getSideArmy(BattleSide side) const override;
 const CGHeroInstance * getSideHero(BattleSide side) const override;
 ui8 getTacticDist() const override;
 BattleSide getTacticsSide() const override;
 int32_t getRound() const override;
 const CGTownInstance * getDefendedTown() const override;
 EWallState getWallState(EWallPart partOfWall) const override;
 EGateState getGateState() const override;
 int32_t getCastSpells(BattleSide side) const override;
 int32_t getEnchanterCounter(BattleSide side) const override;
 const IBonusBearer * getBonusBearer() const override;
	BattleID getBattleID() const override;

	int32_t getActiveStackID() const override;

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	void nextRound() override;
	void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) override;

	void addUnit(uint32_t id, const JsonNode & data) override;
	void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) override;
	void moveUnit(uint32_t id, const BattleHex & destination) override;
	void removeUnit(uint32_t id) override;

	void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;

	void setWallState(EWallPart partOfWall, EWallState state) override;

	void addObstacle(const ObstacleChanges & changes) override;
	void updateObstacle(const ObstacleChanges& changes) override;
	void removeObstacle(uint32_t id) override;

	uint32_t nextUnitId() const override;

	int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override;
	std::vector<SpellID> getUsedSpells(BattleSide side) const override;
	int3 getLocation() const override;
	BattleLayout getLayout() const override;

	int32_t getTreeVersion() const;

	void makeWait(const battle::Unit * activeStack);

	void resetActiveUnit()
	{
		activeUnitId = -1;
	}

	ServerCallback * getServerCallback();
	const scripting::Pool & getScriptContextPool() const override;

private:

 explicit HypotheticBattle(std::shared_ptr<HypotheticFrameArena> arena, int32_t active, BattleID battleID);
 std::shared_ptr<HypotheticFrameArena> ownedArena;
 std::shared_ptr<OrdinaryUnitViewStore> ordinaryStore;
 BattleID ownedBattleID;
 class HypotheticServerCallback : public ServerCallback
	{
	public:
		HypotheticServerCallback(HypotheticBattle * owner_);

		void complain(const std::string & problem) override;
		bool describeChanges() const override;

		vstd::RNG * getRNG() override;
		bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) override;

		void apply(CPackForClient & pack) override;

		void apply(BattleLogMessage & pack) override;
		void apply(BattleStackMoved & pack) override;
		void apply(BattleUnitsChanged & pack) override;
		void apply(SetStackEffect & pack) override;
		void apply(StacksInjured & pack) override;
		void apply(BattleObstaclesChanged & pack) override;
		void apply(CatapultAttack & pack) override;
	private:
		HypotheticBattle * owner;
		RNGStub rngStub;
	};

	class HypotheticEnvironment : public Environment
	{
	public:
		HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment);

		const Services * services() const override;
		const BattleCb * battle(const BattleID & battleID) const override;
		const GameCb * game() const override;

	private:
		HypotheticBattle * owner;
		const Environment * env;
	};

	std::vector<uint32_t> unitOrder;
	int32_t bonusTreeVersion;
	int32_t activeUnitId;
	mutable uint32_t nextId;

	std::unique_ptr<HypotheticServerCallback> serverCallback;
	std::unique_ptr<HypotheticEnvironment> localEnvironment;
};
