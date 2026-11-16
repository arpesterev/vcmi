/*
 * AmmoCartQuery.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
namespace AmmoCartQuery
{
/// The first matching cart suppresses all later carts and artifact fallback.
template<class Units, class Matches, class IsCart, class Alive, class Fallback>
bool evaluate(const Units & units, Matches matches, IsCart isCart, Alive alive, Fallback fallback)
{
 for(const auto & unit : units)
  if(matches(unit) && isCart(unit))
   return alive(unit);
 return fallback();
}
}
