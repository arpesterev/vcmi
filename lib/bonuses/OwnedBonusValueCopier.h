/* Internal owned value closure copier. GPL-2.0-or-later. */
#pragma once
#include "BonusParameters.h"

// Input must already belong to an acyclic deep-owned closure. Configuration
// remains shared through const handles; const-bypassing writes are unsupported.
class OwnedBonusValueCopier
{
 std::map<const Bonus *, std::shared_ptr<Bonus>> values;
 // Retained keys prevent address reuse when the copier spans query transactions.
 std::vector<std::shared_ptr<const Bonus>> sources;
 std::map<const BonusParameters *, std::shared_ptr<BonusParameters>> parameters;
 std::map<const BonusParameters *, std::shared_ptr<const BonusParameters>> parameterSources;
 struct ParameterVisitor
 {
  OwnedBonusValueCopier & copier;
  void operator &(BonusParameters::storage_type & data)
  {
   if(auto * combat=std::get_if<BonusParametersOnCombatEvent>(&data))
    for(auto & effect:combat->effects)
     if(auto * bonus=std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
      bonus->bonus=copier.copy(bonus->bonus);
  }
 };
public:
 const std::vector<std::shared_ptr<const Bonus>> & ownedSources() const { return sources; }
 // Lookup only: does not certify or copy an external parameter object.
 std::shared_ptr<const BonusParameters> sourceParameters(const std::shared_ptr<const BonusParameters> & copy) const
 {
  auto found = parameterSources.find(copy.get());
  if(found == parameterSources.end()) return nullptr;
  const auto & expected = parameters.at(found->second.get());
  if(expected.owner_before(copy) || copy.owner_before(expected))
   throw std::invalid_argument("Foreign observed parameter ownership");
  return found->second;
 }
 std::shared_ptr<const BonusParameters> copiedParameters(const BonusParameters * source) const
 {
  auto found=parameters.find(source);return found==parameters.end()?nullptr:found->second;
 }
 std::shared_ptr<Bonus> copiedBonus(const Bonus * source) const
 {
  auto found=values.find(source);return found==values.end()?nullptr:found->second;
 }
 std::shared_ptr<Bonus> copy(const std::shared_ptr<const Bonus> & source)
 {
  if(!source)return nullptr;
  if(auto found=values.find(source.get());found!=values.end())return found->second;
  if(typeid(*source)!=typeid(Bonus))throw std::invalid_argument("Unsupported owned Bonus subtype");
  auto result=std::make_shared<Bonus>(*source);values.emplace(source.get(),result);sources.push_back(source);
  if(source->parameters)
  {
   auto found=parameters.find(source->parameters.get());
   if(found!=parameters.end())result->parameters=found->second;
   else
   {
    auto parameter=std::make_shared<BonusParameters>(*source->parameters);
    parameters.emplace(source->parameters.get(),parameter);
    parameterSources.emplace(parameter.get(),source->parameters);
    ParameterVisitor visitor{*this};parameter->serialize(visitor);result->parameters=parameter;
   }
  }
  return result;
 }
};