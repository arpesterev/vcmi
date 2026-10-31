/*
 * BackendRegistry.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AIIntegration
{
enum class BackendId { HOMM3, FHEROES2, TUM, MMAI };

struct BackendSpec
{
	BackendId id;
	std::string_view name;
	std::string_view upstream;
};

inline constexpr std::array<BackendSpec, 4> backendSpecs{{
	{BackendId::HOMM3, "HoMM3Original", "sushi-shi/homm3-decomp"},
	{BackendId::FHEROES2, "FHeroes2", "ihhub/fheroes2"},
	{BackendId::TUM, "TUMEnchanted", "SadnessPower/TUM-Enchanted"},
	{BackendId::MMAI, "MMAI", "smanolloff/vcmi-gym"}
}};

inline const BackendSpec & backendSpec(BackendId id)
{
	for(const auto & spec : backendSpecs)
		if(spec.id == id)
			return spec;
	throw std::invalid_argument("Unknown AI backend ID");
}

inline const BackendSpec * findBackend(std::string_view name)
{
	for(const auto & spec : backendSpecs)
		if(spec.name == name)
			return &spec;
	return nullptr;
}

/// A build reference identifies source, not proof of behavioral equivalence.
struct BackendBuild
{
	BackendId id;
	std::string sourceRevision;
	std::string adapterRevision;
	std::vector<std::string> rulesets;
};

inline bool isCommitHash(std::string_view value)
{
	return value.size() == 40 && std::all_of(value.begin(), value.end(), [](char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	});
}

/// Registration must bind actual port code; there is no default implementation.
template<typename Interface>
class BackendRegistry
{
public:
	using Factory = std::function<std::shared_ptr<Interface>()>;

	void add(BackendBuild build, Factory factory)
	{
		backendSpec(build.id);
		if(!factory || !isCommitHash(build.sourceRevision) || !isCommitHash(build.adapterRevision))
			throw std::invalid_argument("AI backend requires a factory and pinned source/adapter commits");
		if(build.rulesets.empty() || std::any_of(build.rulesets.begin(), build.rulesets.end(), [](const auto & rule) { return rule.empty(); }))
			throw std::invalid_argument("AI backend must declare supported rulesets");
		std::lock_guard guard(mutex);
		if(frozen)
			throw std::logic_error("AI backend registry is frozen for this game");
		if(entries.count(build.id))
			throw std::logic_error("AI backend is already registered");
		const auto id = build.id;
		entries.emplace(id, Entry{std::move(build), std::move(factory)});
	}

	bool available(BackendId id, std::string_view ruleset) const
	{
		backendSpec(id);
		std::lock_guard guard(mutex);
		const auto entry = entries.find(id);
		return entry != entries.end() && supports(entry->second.build, ruleset);
	}

	std::shared_ptr<Interface> create(BackendId id, std::string_view ruleset)
	{
		backendSpec(id);
		Factory factory;
		{
			std::lock_guard guard(mutex);
			const auto entry = entries.find(id);
			if(entry == entries.end())
				throw std::runtime_error(std::string(backendSpec(id).name) + ": upstream port is not linked");
			if(!supports(entry->second.build, ruleset))
				throw std::runtime_error(std::string(backendSpec(id).name) + ": unsupported ruleset " + std::string(ruleset));
			frozen = true;
			factory = entry->second.factory;
		}
		auto instance = factory();
		if(!instance)
			throw std::runtime_error(std::string(backendSpec(id).name) + ": factory returned no implementation");
		return instance;
	}

	BackendBuild provenance(BackendId id) const
	{
		std::lock_guard guard(mutex);
		const auto entry = entries.find(id);
		if(entry == entries.end())
			throw std::runtime_error("AI backend is not registered");
		return entry->second.build;
	}

private:
	struct Entry { BackendBuild build; Factory factory; };
	static bool supports(const BackendBuild & build, std::string_view ruleset)
	{
		return std::any_of(build.rulesets.begin(), build.rulesets.end(), [ruleset](const auto & supported) { return supported == ruleset; });
	}
	mutable std::mutex mutex;
	std::map<BackendId, Entry> entries;
	bool frozen = false;
};
}
