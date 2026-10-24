# Actual pathfinder publication protocol reference (staged, unexecuted)

This separate excluded target must be overlaid only after the risk-consumer implementation and its shared PlanningRiskConsumerAccess.h pass source review. It does not modify qualification4, enable a model scheduler, execute battles, or estimate battle outcomes.

Include AdventurePathRiskReference.cmake from test/CMakeLists.txt after AdventureScenarioReference.cmake. The core scenario generator and all its existing dependencies remain required. Compile vcmi_path_risk_reference incrementally with the same 180-second/12-new-object qualification guard; run from build/bin using public checksum-pinned test data. Two separate gtest-filter processes, each at most 180 seconds, aggregate at most 360 seconds. Require both successful exits and exact XML testcase completion; timeout or unsupported packets are incomplete, never passes.

Tests:
- ActualPathfinderRequiresEligibleFavorablePublication: the existing public visible 999-pikeman monster against a real 20-pikeman hero must first have NO real path. Unsafe evidence cannot rescue it; injected favorable evidence must produce a real path for that exact hero/current army; production invalidation must remove the rescue.
- ActualEarlierCombatCannotUseDestinationOnlyPublication: a public one-tile dirt corridor bounded by water contains a weak one-pikeman guard before the 999-pikeman target. Actual intermediate paths must exist and all have positive encounter danger. A destination-only favorable publication cannot rescue the later target. Destination overlap/visibility checks reject invalid fixture premises.

The favorable prediction is deliberately counterfactual. It proves that eligible evidence reaches the production bypassBattle pre-pruner, not that this army can win. Actual fight calibration remains entirely separate. No claim about win rate, enjoyment, autonomous goal selection, or asynchronous client lifecycle follows from these tests.

The bridge header is a separate executable snapshot of the reviewed qualification4 interaction bridge. Its sole lock-contract extension is an optional synchronous operation inside the existing storage-before-GS locked readiness scope. Shared publication access is copied byte-for-byte from the model owner's header. No scheduler regression translation unit is linked into this executable.

Public provenance: existing MiniTest and ObjectPropertyTest templates and generated blocked_goal_alternative map; no user maps. Python corridor generation has executed successfully; C++ compilation, real path preconditions and native test outcomes are pending. Hash source files, the pinned archive, executable and both used maps in any subsequent qualification report.
