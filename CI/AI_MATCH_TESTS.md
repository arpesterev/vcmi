# Full-map AI regression

`CI/run_ai_match.py` launches the ordinary `vcmiclient --headless --testmap`
client on a generated 36-by-36 map. Both players use Nullkiller2. Adventure
planning, movement, recruitment, construction, questions, combat AI and victory
processing use the normal game path. No test code selects goals or injects
battle outcomes.

The fixture has two Castle towns, fixed starting armies, resource piles and a
single guarded passage containing an artifact. `vcmitest --export-ai-match-map`
exports it with the native H3M builder and prints an `AI_MATCH_MAP=` line.

## Acceptance

The match must complete within 28 game days and 600 wall-clock seconds. Both
players must complete a turn. Applied client events must demonstrate resource
pickup, construction, recruitment, combat damage, a confirmed battle result,
collection of the guarded artifact after combat, a town changing owner, and one
winner with both players finished. A clean process exit is required as well.

A second run uses EmptyAI for both players. It must start correctly, complete
turns and reach the day limit without collecting resources or winning. Startup
failure, a crash or a wall-clock timeout does not satisfy this negative control.
The Python unit tests validate this result checker, not the game itself.

## Run on Linux

Build `vcmitest`, `vcmiserver` and `vcmiclient` with the test preset, then run from
the repository root:

```sh
bin="$PWD/out/build/linux-gcc-test/bin"
(cd "$bin" && ./vcmitest --export-ai-match-map) | tee /tmp/ai-map-export.log
map=$(sed -n 's/^AI_MATCH_MAP=//p' /tmp/ai-map-export.log)
python3 CI/run_ai_match.py \
  --client "$bin/vcmiclient" --map "$map" \
  --data "$HOME/.local/share/vcmi" --output /tmp/vcmi-ai-match
```

Use a new output directory for every run. The harness creates isolated XDG
configuration, cache and save directories and shares the supplied game data
through read-only-use symlinks. It does not edit the source data or the user's
normal settings. The current harness is Linux-only.

`VCMI_AI_MATCH_TRACE` enables the client-side JSONL observer only in headless
mode. It reads applied state; the AI's visibility and callbacks are unchanged.
`VCMI_TEST_RNG_SEED` replaces time-based default RNG seeds in the test process.
It does not promise identical parallel scheduling or cross-platform traces.
Neither variable is set for normal gameplay.

## Diagnostics and limits

Each run writes `events.jsonl`, `client.log` and `result.json`. A failure prints
the unmet conditions, last applied state and log tail. CI also writes the result
to its step summary. There are no release or artifact uploads; bounded compiler
objects may be stored in GitHub Actions cache for trusted reruns.

This is an end-to-end regression on one controlled map, not a playing-strength
benchmark. Passing it does not prove optimal town defence, sensible decisions
on every map, or improved win rate. CI uses public regression game data; the
same map can also be run against a separately installed game-data set.
