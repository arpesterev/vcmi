/* Single-owner graph/value binding. GPL-2.0-or-later. */
#pragma once
#include "BonusEvaluationSnapshot.h"
#include "OwnedBonusLogicalDomain.h"
using OwnedSpellPersistenceFacts = std::map<SpellID, bool>;
enum class OwnedBonusExpiry { Round, GetsTurn, Attack, OwnAttack, BeingAttacked, IndirectDamage };
class OwnedBonusValueCopier;
class DLL_LINKAGE OwnedBonusGraphSession final
{
 friend class OwnedBattleBonusModel;
 friend class HypotheticFrameArena;
 OwnedLogicalBonusEvent makeObservedEvent(const std::vector<Bonus> & values, const OwnedBonusValueCopier & copies) const;
 TConstBonusListPtr queryWithStableCopies(BonusGraphSnapshot::NodeId node, const CSelector & selector, const std::string & key, OwnedBonusValueCopier & copies) const;
 std::vector<OwnedLogicalBonusEvent> importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const;
 struct State;
 mutable std::unique_ptr<State> state;
 explicit OwnedBonusGraphSession(std::unique_ptr<State> value);
 OwnedBonusGraphSession transactionCopy() const;
 const BonusEvaluationSnapshot & evaluationRef() const;
 void replaceEvaluation(BonusEvaluationSnapshot evaluation);
 size_t eventSize(const OwnedLogicalBonusEvent & event) const;
 void applyEventItem(BonusGraphSnapshot::NodeId node, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd);
 void applyEventItemImpl(BonusGraphSnapshot::NodeId node, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd);
 bool removeEventItem(BonusGraphSnapshot::NodeId node, const OwnedLogicalBonusEvent & event, size_t index);
public:
 using BonusId=BonusGraphSnapshot::BonusId;
 using NodeId=BonusGraphSnapshot::NodeId;
 ~OwnedBonusGraphSession();
 OwnedBonusGraphSession(OwnedBonusGraphSession &&) noexcept;
 OwnedBonusGraphSession & operator=(OwnedBonusGraphSession &&) noexcept;
 OwnedBonusGraphSession(const OwnedBonusGraphSession &)=delete;
 OwnedBonusGraphSession & operator=(const OwnedBonusGraphSession &)=delete;
 static OwnedBonusGraphSession capture(BonusEvaluationSnapshot evaluation, OwnedSpellPersistenceFacts persistence = {});
 OwnedBonusGraphSession fork()const;
 BonusEvaluationSnapshot evaluation()const;
 OwnedLogicalBonusEvent eventFromBonuses(const std::vector<BonusId> & ids)const;
 OwnedLogicalBonusEvent makeEvent(const std::vector<Bonus> & values)const;
 /// Bounded bonus-only expiry; one recursive root is an atomic publication.
 bool expireBonuses(BonusGraphSnapshot::NodeId node, OwnedBonusExpiry event);
 /// Removes recursive exported spell effects using capture-time definition values.
 bool removeNonpersistentSpells(NodeId node);
 /// Removes selected exports recursively in one transaction.
 bool removeBonuses(NodeId node, const CSelector & selector);
 void setTurnsRemain(BonusId id,int turns);
 BonusId addEventRoot(NodeId node,const OwnedLogicalBonusEvent & event,size_t index);
 uint64_t revision()const;
 TConstBonusListPtr query(NodeId node, const CSelector & selector, const std::string & cachingStr = "") const;
 std::vector<OwnedBonusValueId> queryMembership(NodeId node, const CSelector & selector, const std::string & cachingStr = "") const;
 TConstBonusListPtr resolveMembership(const std::vector<OwnedBonusValueId> & ids) const;
 std::vector<OwnedBonusValueId> capturedMembership(const std::vector<BonusGraphSnapshot::ValueId> & ids) const;
 int64_t nodeVersion(NodeId node) const;
 void invalidateNodeAndDescendants(NodeId node);
};
