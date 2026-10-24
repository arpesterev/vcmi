"""Validate the predeclared forced-battle matrix, not model accuracy/calibration."""
from collections import Counter, defaultdict
import math
import re

SEEDS = (17041, 17047, 17053)
# Include the player-bound production capture path, not just direct battle snapshots.
REQUIRED_SMOKES = ('ordinary_melee', 'ranged_slow_melee', 'undead_melee', 'neutral_0_0')

def expected_cases():
    rows = []
    def direct(name, test, seed):
        rows.append(dict(id=name, kind='direct', test='ForcedBattleReference.'+test, seed=seed))
    for name, test, seed in (
        ('ordinary_melee','OrdinaryMelee',17041),
        ('ranged_slow_melee','RangedAgainstSlowMelee',17047),
        ('slow_melee_ranged','SlowMeleeAgainstRanged',17053),
        ('undead_melee','UndeadMelee',17041),
        ('flying_ranged','FlyingAgainstRanged',17053),
        ('ranged_mirror','RangedMirror',17047),
        ('mixed_low','MixedArmyLowerDefenderCount',17041),
        ('mixed_high','MixedArmyHigherDefenderCount',17041),
        ('marksman_double_shot','NativeMarksmanDoubleShot',17041),
        ('grand_elf_double_shot','NativeGrandElfDoubleShot',17047),
        ('griffin_retaliation','NativeGriffinRetaliation',17041),
        ('royal_griffin_retaliation','NativeRoyalGriffinRetaliation',17047),
        ('sprite_blocks_retaliation','NativeSpriteBlocksRetaliation',17053)):
        direct(name,test,seed)
    index = 0
    for count in (10,20,30,40,60,80,120):
        for seed in SEEDS:
            for reverse in (False,True):
                name = f"sweep_{'slow_ranged' if reverse else 'ranged_slow'}_{count}_{seed}"
                rows.append(dict(id=name,kind='direct',test=f'Predeclared/ForcedBattleCountSweep.ArcherWalkingDead/{index}',seed=seed))
                index += 1
    for upgrade, specialty, test in ((0,0,'RealNeutralSplit'),(1,0,'RealNeutralSingleUpgrade'),(0,1,'NativeHeroSpecialtyTroops')):
        rows.append(dict(id=f'neutral_{upgrade}_{specialty}',kind='neutral',upgrade=upgrade,specialty=specialty,
                         test='ForcedBattleReference.'+test,seed=17041))
    return rows

EXPECTED_TEST_NAMES = sorted(row['test'] for row in expected_cases())
_KEY = re.compile(r'(?:^|\s)([A-Za-z_][A-Za-z0-9_]*)=')

def _fields(text):
    matches = list(_KEY.finditer(text)); result = {}
    for i, match in enumerate(matches):
        key = match.group(1)
        value = text[match.end():matches[i+1].start() if i+1 < len(matches) else len(text)].strip()
        # Legacy deterministic endpoints intentionally repeat these fields;
        # they are not sampled accuracy observations and are never scored here.
        if key.startswith('forecast_'):
            continue
        if key in result:
            raise ValueError(f'duplicate field {key}')
        result[key] = value
    return result

def _int(row, key, allowed=None, low=None, high=None):
    try: value = int(row[key])
    except (KeyError, ValueError) as exc: raise ValueError(f'invalid/missing integer {key}: {row}') from exc
    if (allowed is not None and value not in allowed) or (low is not None and value < low) or (high is not None and value > high):
        raise ValueError(f'out-of-range {key}={value}')
    return value

def _nonnegative(value, key):
    try: number = float(value)
    except ValueError as exc: raise ValueError(f'invalid {key}={value}') from exc
    if not math.isfinite(number) or number < 0:
        raise ValueError(f'invalid {key}={value}')
    return number

def summarize_evidence(log, *, require_smokes=True):
    specs = {row['id']: row for row in expected_cases()}
    references = {}; captures = defaultdict(dict); samples = defaultdict(dict)
    tags = {'FORCED_BATTLE_REFERENCE':'reference', 'FORCED_NEUTRAL_REFERENCE':'reference',
            'FORCED_BATTLE_CAPTURE':'capture', 'FORCED_NEUTRAL_CAPTURE':'capture',
            'FORCED_BATTLE_SAMPLE':'sample', 'FORCED_NEUTRAL_SAMPLE':'sample'}
    for line in log.splitlines():
        tag, _, text = line.partition(' ')
        if tag not in tags: continue
        row = _fields(text)
        if _int(row,'policy_version') != 2: raise ValueError('wrong battle policy version')
        # Existing informational line, not a capture opportunity/result.
        if tag == 'FORCED_NEUTRAL_CAPTURE' and set(row) == {'selected','policy_version'}:
            continue
        neutral = tag.startswith('FORCED_NEUTRAL_')
        if neutral:
            upgrade = _int(row,'upgrade',{0,1}); specialty = _int(row,'specialty',{0,1})
            identity = f'neutral_{upgrade}_{specialty}'
        else:
            identity = row.get('case')
        if identity not in specs or (specs[identity]['kind']=='neutral') != neutral:
            raise ValueError(f'unexpected case identity {identity}')
        kind = tags[tag]
        if kind == 'reference':
            if identity in references: raise ValueError(f'duplicate terminal reference {identity}')
            if _int(row,'seed') != specs[identity]['seed']: raise ValueError(f'wrong oracle seed {identity}')
            _int(row,'actions',low=1,high=2000)
            if neutral:
                row['actual_winner'] = _int(row,'winner',{0,1})
                _int(row,'split',low=2,high=7); _int(row,'battlefield',low=0)
            else:
                if row.get('winner') not in ('attacker','defender'): raise ValueError(f'invalid winner {identity}')
                row['actual_winner'] = int(row['winner']=='defender')
                for key in ('lost_ai_a','lost_ai_d'):
                    if key not in row: raise ValueError(f'missing casualty value {key}')
                if 'scalar_ratio' in row: _nonnegative(row['scalar_ratio'],'scalar_ratio')
            for key, value in row.items():
                if key.startswith('loss_'): _int(row,key,low=0)
                if key.startswith('lost_ai_'): _nonnegative(value,key)
            references[identity] = row
        elif kind == 'capture':
            if not neutral and _int(row,'seed') != specs[identity]['seed']: raise ValueError(f'wrong capture seed {identity}')
            field = _int(row,'field',low=0) if neutral else 'direct'
            if field in captures[identity]: raise ValueError(f'duplicate capture {identity}/{field}')
            admitted = _int(row,'admitted',{0,1})
            if 'reason' not in row or (not admitted and row['reason'].strip().lower() in ('','none')):
                raise ValueError(f'missing rejected-capture reason {identity}')
            captures[identity][field] = row
        else:
            if neutral:
                sample_seed = _int(row,'sample_seed',set(SEEDS)); index = SEEDS.index(sample_seed)
            else:
                if _int(row,'oracle_seed') != specs[identity]['seed']: raise ValueError(f'wrong sample oracle seed {identity}')
                index = _int(row,'sample_index',{0,1,2})
            if index in samples[identity]: raise ValueError(f'duplicate sample {identity}/{index}')
            status = _int(row,'status',{0,1,2,3}); winner = _int(row,'winner',{-1,0,1})
            if (status == 2 and winner == -1) or (status != 2 and winner != -1):
                raise ValueError(f'nonterminal/terminal winner inconsistency {identity}')
            if 'reason' not in row or (status != 2 and row['reason'].strip().lower() in ('','none')):
                raise ValueError(f'missing unsupported-sample reason {identity}')
            if status != 2 and 'agrees' in row: raise ValueError(f'nonterminal accuracy {identity}')
            for key in ('a_survivors','d_survivors'):
                if key in row: _int(row,key,low=0)
            for key in ('a_lost_value','d_lost_value'):
                if key in row: _nonnegative(row[key],key)
            samples[identity][index] = row
    missing = set(specs)-set(references)
    if missing: raise ValueError(f'missing terminal references: {sorted(missing)}')
    counts = Counter(); capture_reasons = Counter(); sample_reasons = Counter(); results = []; smoke_failures = []
    for identity, spec in specs.items():
        reference = references[identity]
        field = int(reference['battlefield']) if spec['kind']=='neutral' else 'direct'
        if field not in captures[identity]: raise ValueError(f'missing selected capture {identity}/{field}')
        selected = captures[identity][field]; admitted = int(selected['admitted'])
        expected_indices = {0,1,2} if admitted else set()
        if set(samples[identity]) != expected_indices:
            raise ValueError(f'wrong sample count/indices for admitted={admitted} {identity}')
        counts['actual_terminal'] += 1
        counts['capture_opportunities'] += len(captures[identity])
        counts['selected_capture_admitted' if admitted else 'selected_capture_rejected'] += 1
        for capture in captures[identity].values():
            counts['captures_admitted' if int(capture['admitted']) else 'captures_rejected'] += 1
            if not int(capture['admitted']): capture_reasons[capture['reason']] += 1
        rows = []
        for index, row in sorted(samples[identity].items()):
            status = int(row['status']); counts['sample_executions'] += 1
            counts[{0:'samples_unsupported',1:'samples_budget_exhausted',2:'samples_finished',3:'samples_rollout_limit'}[status]] += 1
            result = dict(row, sample_index=index, sample_seed=SEEDS[index])
            if status == 2:
                agrees = int(row['winner']) == reference['actual_winner']
                if 'agrees' in row and _int(row,'agrees',{0,1}) != int(agrees): raise ValueError(f'wrong reported agreement {identity}')
                result['winner_agrees'] = agrees
                counts['finished_winner_agrees' if agrees else 'finished_winner_disagrees'] += 1
            else: sample_reasons[row['reason']] += 1
            rows.append(result)
        if identity in REQUIRED_SMOKES and (not admitted or len(rows)!=3 or any(int(row['status'])!=2 for row in rows)):
            smoke_failures.append(f'predeclared supported smoke did not execute three terminal samples: {identity}')
        results.append(dict(id=identity,test=spec['test'],actual=reference,selected_capture=selected,samples=rows))
    for key in ('actual_terminal','capture_opportunities','captures_admitted','captures_rejected','selected_capture_admitted',
                'selected_capture_rejected','sample_executions','samples_unsupported','samples_budget_exhausted','samples_finished','samples_rollout_limit',
                'finished_winner_agrees','finished_winner_disagrees'):
        counts.setdefault(key,0)
    if require_smokes and smoke_failures:
        raise ValueError("; ".join(smoke_failures))
    return dict(policy_version=2, supported_smoke_failures=smoke_failures, expected_cases=58, counts=dict(counts),required_supported_smokes=list(REQUIRED_SMOKES),
                rejected_capture_reasons=dict(capture_reasons),nonterminal_sample_reasons=dict(sample_reasons),cases=results,
                interpretation='Fixed-policy outcome comparisons only. Unsupported, budget and intrinsic rollout-limit outcomes are not losses; endpoint straddles are not accuracy; no calibrated probability or safety claim.')
