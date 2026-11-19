#include "StdInc.h"
#include "OwnedBonusValues.h"
#include "BonusParameters.h"
namespace
{
class ValueCopier
{
 std::map<const Bonus *, std::shared_ptr<Bonus>> values;
 std::map<const BonusParameters *, std::shared_ptr<BonusParameters>> parameters;
 struct ParameterVisitor
 {
  ValueCopier & copier;
  void operator &(BonusParameters::storage_type & data)
  {
   if(auto * combat=std::get_if<BonusParametersOnCombatEvent>(&data))
    for(auto & effect:combat->effects)
     if(auto * bonus=std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
      bonus->bonus=copier.copy(bonus->bonus);
  }
 };
public:
 std::shared_ptr<Bonus> copy(const std::shared_ptr<const Bonus> & source)
 {
  if(!source)return nullptr;
  if(auto found=values.find(source.get());found!=values.end())return found->second;
  const auto * sourcePtr = source.get();
  if(typeid(*sourcePtr)!=typeid(Bonus))throw std::invalid_argument("Unsupported owned Bonus subtype");
  auto result=std::make_shared<Bonus>(*source);values.emplace(source.get(),result);
  if(source->parameters)
  {
   auto found=parameters.find(source->parameters.get());
   if(found!=parameters.end())result->parameters=found->second;
   else
   {
    auto parameter=std::make_shared<BonusParameters>(*source->parameters);
    parameters.emplace(source->parameters.get(),parameter);
    ParameterVisitor visitor{*this};parameter->serialize(visitor);result->parameters=parameter;
   }
  }
  return result;
 }
};
}
OwnedBonusValues::OwnedBonusValues(std::vector<std::shared_ptr<const Bonus>> values):roots(std::move(values)){}
OwnedBonusValues OwnedBonusValues::capture(const BonusGraphSnapshot & graph)
{
 if(!graph.sameOrigin(graph))throw std::invalid_argument("Values require an owned graph capture");
 return OwnedBonusValues(graph.bonuses());
}
const std::vector<std::shared_ptr<const Bonus>> & OwnedBonusValues::bonuses() const{return roots;}
OwnedBonusValues OwnedBonusValues::withTurnsRemain(BonusId id,int turns) const
{
 return withTurnsRemain({{id,turns}});
}
OwnedBonusValues OwnedBonusValues::withTurnsRemain(const std::vector<std::pair<BonusId,int>> & changes) const
{
 for(const auto & change:changes)(void)roots.at(change.first);
 ValueCopier copier;std::vector<std::shared_ptr<const Bonus>> next;next.reserve(roots.size());
 for(const auto & root:roots)next.push_back(copier.copy(root));
 for(const auto & change:changes)copier.copy(roots.at(change.first))->turnsRemain=change.second;
 return OwnedBonusValues(std::move(next));
}
std::vector<std::shared_ptr<Bonus>> OwnedBonusValues::query(const std::vector<BonusId> & ids) const
{
 for(auto id:ids)(void)roots.at(id);
 ValueCopier copier;std::vector<std::shared_ptr<Bonus>> result;result.reserve(ids.size());
 for(auto id:ids)result.push_back(copier.copy(roots.at(id)));
 return result;
}
