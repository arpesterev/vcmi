# Historical native threat baseline audit

`FHeroes2NativeThreatTest.cpp` formerly selected an external
`../../../evidence/BaselineFHeroes2BattleAI.h` when `FHER_THREAT_BASELINE` was
manually defined. That path resolves outside the repository. No CMake option,
CI job, checked-in baseline header or baseline-header hash accompanies it.
It was a local audit hook, not the pinned fheroes2 source oracle or a supported
build configuration. Removing it leaves every normal test body and assertion
unchanged. The class-name macro still isolates this translation unit's controller
from the factory implementation in other translation units.

For a historical candidate/baseline investigation, retain two isolated Git
worktrees with explicit full commit IDs and separate build directories. Record
compiler/options, dependency hashes, the complete test harness revision and any
harness-only backport patch. Compile each controller from its own versioned
header; run the same declared fixture inputs and retain commands, exit status,
XML, actions and timing logs for both runs. Changed-choice tests may deliberately
fail on a baseline: preserve those failures rather than weakening assertions.
If a local header snapshot is used, archive its exact bytes, SHA-256 and provenance
with the investigation; an unspecified old external file cannot be reconstructed
or qualified from the former macro alone.

The checked-in `PinnedThreatFixtureOracle.h` and `source-manifest.json` remain the
source-backed arithmetic checks. Native baseline comparisons are separate from
`CI/fheroes2_reference`'s four coarse cross-engine properties, and neither
establishes full source decision or geometry parity.
