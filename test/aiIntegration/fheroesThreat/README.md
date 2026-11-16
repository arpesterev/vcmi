# Ordinary threat arithmetic oracle

Pinned ihhub/fheroes2 commit and exact battle_troop.cpp Git blob are recorded in source-manifest.json. generate_oracle.py extracts five unmodified method bodies; supporting types model only ordinary melee creatures with no spell modes or special abilities. The sole mode stub is retaliation already spent. Generated ThreatOracle.cpp is committed so the differential check does not require Python or network at build time.

The source uses integer per-bound truncation with a minimum of one before averaging potential damage. Retaliation estimates surviving count from current health and innate hit points, then multiplies by integer per-creature damage midpoint. Live attack/defense are adapter inputs, not source-equivalent creature identities. Native geometry remains outside this arithmetic oracle.

Configure this directory with CMake and run CTest, or compile ThreatOracle.cpp with C++17. Checks remain active under NDEBUG. Current contract rejects invalid unit count/health and operations outside uint32 source arithmetic domain rather than relying on overflow.
