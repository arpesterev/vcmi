/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cstdint>
#include <optional>
#include <span>
namespace AIIntegration::Ports::HoMM3
{
// considerSpell(DISPEL): each side is summed by considerEnchantment first,
// then Expert adds the enemy side. Each input was individually truncated by
// getCancelValue. Never truncate a floating aggregate of the components.
inline std::optional<int32_t> sumPrayerRemovalValues(std::span<const int32_t> friendly,
 std::span<const int32_t> enemy)
{
 int64_t ours=0,theirs=0;
 for(const auto value:friendly){if(value>0)return std::nullopt;ours+=value;if(ours<INT32_MIN)return std::nullopt;}
 for(const auto value:enemy){if(value<0)return std::nullopt;theirs+=value;if(theirs>INT32_MAX)return std::nullopt;}
 const auto total=ours+theirs;
 if(total<INT32_MIN||total>INT32_MAX)return std::nullopt;
 return static_cast<int32_t>(total);
}
}
