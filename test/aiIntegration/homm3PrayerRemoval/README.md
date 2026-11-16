# Single-effect Prayer cancellation source core (native producer pending)

Pin e12906accf96c2662b1abadbc49ddc111991bf21, sushi-shi/homm3-decomp.
`generate_oracle.py /path/to/ai_tactical.cpp` extracts exact getCancelValue,
getPrayerValue, getDefenseSkillValue/getDefenseBoostValue, getAttackSkillValue/
both getAttackBoostValue overloads, getSpeedValue and getDuration. Macro long
int32_t models source signed arithmetic. Scalar host exposes separately supplied
cached metadata and cleaned damage facts; it throws if any value function reads
the not-yet-cancelled Prayer copy. No production formula is called by the oracle.

For one positive Prayer, getCancelValue always evaluates with the buff OWNER's
caster: this for friendly, enemyCaster for enemy. Friendly cancellation negates
its value; enemy cancellation adds it. Cure's badSpellsOnly skips positive Prayer.
The original remaining duration and mastery, not Dispel caster level/power,
parameterize Prayer revaluation after cancellation. All three component functions
truncate individually before summation. Sequential multi-effect cancellation is
not part of this interface.

PrayerRemovalFacts requires a certified cleaned baseline and owner caster
context. Defense estimated ratios and average damage are recalculated on the
cleaned unit, but representative enemy identity and incoming-damage totals stay
from the original pre-cancellation caster. Attack target/distance and speed
arrival metadata retain source census provenance while using cleaned stats.
This separation prevents the invalid 'negate the original cast score' shortcut.

The owner caster's winLikely derives from the SAME parent's simulation residual
ledger. EnemyCaster construction does not rerun a reversed simulation. It scans
the opposite-owner survivors. A prospective native producer must match every
initial unit/survivor to its current snapshot before relying on that ledger;
absence of an opposite-side outcome is not proof of false winLikely.

Current core evidence: 20,000 original cancellation call chains, randomized
mastery/duration/rounds, completion, both ownership signs, intentionally opposite
caster winLikely values, different cached incoming and cleaned damage ratios,
HP caps, target timing and speed increases/decreases. Cure is checked for every
case; malformed certificate and arithmetic overflow cases reject. Finite source
zero-reduced-damage without overkill is admitted; overkill dividing by zero is
rejected rather than running undefined source conversion.

No native removal path is enabled by this checkpoint. The existing native Prayer
cast path and Cure remain unchanged. Native threat/goal producers currently are
explicit projections, not the complete original caster census; any bridge must
state that distinction and prove correct cached/cleaned contexts and actual
server-accepted Dispel before claiming integration.
