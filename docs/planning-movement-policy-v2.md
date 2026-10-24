# Planning battle movement policy v2 (staged, inactive)

The sampled policy and matching forced-server action chooser replace only their
movement fallback. Shot and immediately reachable melee priorities, random draws,
combat mechanics, 256-activation/64-round limits and collective decision deadline
are unchanged. The legacy detached endpoint predictor remains policy v1.

Policy v1 minimized geometric distance to an occupied enemy primary hex. It could
stay in a local minimum indefinitely, even where the engine's BFS found a legal
route around an obstacle or occupied stack. Policy v2 selects a reachable legal
attack-origin primary hex, ordering by BFS distance, enemy unit ID and hex index.
It walks the existing predecessor chain back to the furthest current-turn legal
hex. Flying units use engine-approved landings with distance/hex ties, because
flying predecessors are not a walking path. No authoritative RNG is queried.

The extra route traversal is capped at battlefield size. Reachability is computed
once per non-shooting activation and reused for immediate melee and movement.
Unsupported/unfinished encounters remain unknown; no sample or draw cap increases.

The adaptive memo namespace changes to decision-v2. Reference logs must separately
identify sampled/reference policy v2 and legacy endpoint policy v1. Original
predeclared encounters and server seeds are retained, including losing and unknown
cases. Old policy-v1 agreement and casualty statistics do not validate policy v2.

Required before qualification: independent source review; actual obstacle routing
control; matching real-server chooser; explicit metadata/parser version handling.
Required before activation: useful favorable and rejection production decisions,
all-context support, source invariance, deadline and cancellation tests, and honest
reporting of supported/unknown counts and realized misses. No current claim of
calibrated win probability or user-hardware wall-time is made.
