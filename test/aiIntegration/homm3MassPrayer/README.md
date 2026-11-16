# Expert Dispel mass Prayer-only composition

Pinned upstream: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21,
`src/ai_tactical.cpp`. Run generate_oracle.py on that file. Manifest records
whole-file SHA256, exact getDispelValue/considerEnchantment bodies and the exact
Dispel switch arm from considerSpell. Long is explicitly int32_t in the host.

The original getDispelValue copies one army, then getCancelValue modifies that
copy. considerEnchantment sums each eligible target's integer value; Expert
considerSpell adds the independently summed enemy side to the friendly side.
Other stacks remain pre-cast during each valuation. The host getCancelValue
asserts every original army remains prayed and delegates scalar arithmetic to
the separate nine-body pinned Prayer oracle, not production arithmetic.

10,000 deterministic compositions of 2..14 targets compare the production
signed-side sum and scalar core to these exact caller bodies. Overflow/sign
rejections are checked outside original undefined-overflow cases. Native tests
also run a real-damage counterexample whose wrong globally cleaned board has a
different result, and pass the correct independent target facts through this
same pinned caller. This is caller/component parity, not the complete original
caster threat/target census: those inputs remain existing native projections.
