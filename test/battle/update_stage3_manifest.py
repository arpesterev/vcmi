#!/usr/bin/env python3
"""Inventory staged fixture inputs only; not a build/test qualification result."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
paths = [
    "test/battle/ForcedBattleReference.cpp",
    "test/battle/ForcedBattleStage3.md",
    "test/battle/PlanningBattleSnapshotChecks.h",
    "AI/Nullkiller2/Engine/Experimental/PlanningBattleSnapshot.h",
    "AI/Nullkiller2/Engine/Experimental/SampledBattleRollout.h",
]
files = [{"path": path, "sha256": hashlib.sha256((root / path).read_bytes()).hexdigest()} for path in paths]
manifest = {
    "schema": 1,
    "scope": "fixture-side staged inventory; integrator must generate complete candidate/build manifest",
    "status": "uncompiled_unexecuted",
    "frozen_candidate_modified": False,
    "expected_cases": 58,
    "matrix": {
        "first_oracle": 6,
        "count_sweep": {"cases": 42, "archers": 20, "walking_dead_counts": [10,20,30,40,60,80,120], "orientations": 2, "oracle_seeds": [17041,17047,17053]},
        "mixed_army": 2,
        "real_neutral_split_upgrade": 2,
        "native_mechanics_and_specialty": 6,
    },
    "sample_seeds": [17041,17047,17053],
    "server_action_cap_per_battle": 2000,
    "filter": "ForcedBattleReference.*:Predeclared/ForcedBattleCountSweep.*",
    "known_unqualified": ["exhausted ammunition", "explicit blocked-edge double-wide placement", "obstacle-rich matrix", "own spellcasting policy"],
    "files": files,
}
(root / "test/battle/ForcedBattleStage3Manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
