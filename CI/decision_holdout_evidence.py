"""Strict descriptive holdout evidence; no calibration/activation verdict."""
from collections import Counter, defaultdict
import math
from forced_battle_evidence import _fields

FAMILIES = ('neutral_challenge', 'grandelf_70', 'grandelf_80', 'sprite_skeleton')
CASES = [(family, seed) for seed in (29009, 29017, 29021) for family in FAMILIES]
CASES.append(('neutral_known_regression', 17041))
EXPECTED_TEST_NAMES = [f'IndependentServer/ForcedDecisionHoldout.ProducerThenIndependentServer/{i}' for i in range(13)]

def summarize_holdouts(log):
    rows = defaultdict(dict); samples = defaultdict(dict); variants = defaultdict(dict)
    def integer(row,key,allowed=None):
        value=int(row[key])
        if allowed is not None and value not in allowed: raise ValueError(f'invalid {key}')
        return value
    def number(row,key,low=0,high=float('inf')):
        value=float(row[key])
        if not math.isfinite(value) or not low <= value <= high: raise ValueError(f'invalid {key}')
        return value
    for line in log.splitlines():
        tag, _, text=line.partition(' ')
        if tag not in ('FORCED_DECISION_FORECAST','FORCED_DECISION_REFERENCE','FORCED_DECISION_SAMPLE','FORCED_DECISION_VARIANT'): continue
        row=_fields(text); key=(row['family'],integer(row,'oracle_seed'))
        if key not in CASES: raise ValueError(f'undeclared holdout {key}')
        if tag.endswith('SAMPLE'):
            variant=integer(row,'variant',{0,1,2}); index=integer(row,'index',range(128))
            if (variant,index) in samples[key]: raise ValueError('duplicate sample')
            if integer(row,'sampler_seed') != 104729+index*7919: raise ValueError('wrong public seed')
            status=integer(row,'status',{0,1,2,3}); winner=integer(row,'winner',{-1,0,1})
            if (status==2)!=(winner!=-1): raise ValueError('invalid terminal winner')
            if status!=2 and 'agrees' in row: raise ValueError('nonterminal agreement')
            if status!=2 and row.get('reason','').strip().lower() in ('','none'): raise ValueError('missing unknown reason')
            initial=number(row,'initial_value'); remaining=number(row,'remaining_value'); number(row,'actions')
            if remaining>initial or (status==2 and initial<=0): raise ValueError('invalid sample army values')
            samples[key][variant,index]=row
        elif tag.endswith('VARIANT'):
            variant=integer(row,'variant',{0,1,2})
            if variant in variants[key]: raise ValueError('duplicate variant')
            integer(row,'battlefield'); integer(row,'terrain'); variants[key][variant]=row
        else:
            if tag in rows[key]: raise ValueError('duplicate forecast/reference')
            rows[key][tag]=row
    counts=Counter(); cases=[]
    for key in CASES:
        if set(rows[key]) != {'FORCED_DECISION_FORECAST','FORCED_DECISION_REFERENCE'}: raise ValueError(f'missing holdout evidence {key}')
        forecast=rows[key]['FORCED_DECISION_FORECAST']; actual=rows[key]['FORCED_DECISION_REFERENCE']
        count=integer(forecast,'variants',range(4)); draws=integer(forecast,'draws',range(385))
        recommendation=integer(forecast,'recommendation',{0,1,2})
        if recommendation and not count: raise ValueError('recommendation without variants')
        for field in ('captured','policy','revalidated','record','all_finished','own_magic','stopped'): integer(forecast,field,{0,1})
        if recommendation and not all(int(forecast[k]) for k in ('captured','policy','revalidated','record','all_finished')): raise ValueError('recommendation without validated support')
        number(forecast,'adverse_upper',0,1); number(forecast,'defeat_lower',0,1)
        tolerance=number(forecast,'tolerance',-1,1); number(forecast,'worst_loss',0,1); number(forecast,'elapsed_us')
        if recommendation and tolerance<0: raise ValueError('recommendation without loss policy')
        if set(variants[key])!=set(range(count)): raise ValueError('missing declared variant')
        if len(samples[key])>draws: raise ValueError('unbudgeted samples')
        assessment_count=integer(forecast,'samples',range(385))
        if recommendation and assessment_count!=len(samples[key]): raise ValueError('assessment/sample mismatch')
        for variant in range(count):
            indices=sorted(i for v,i in samples[key] if v==variant)
            if indices!=list(range(len(indices))): raise ValueError('nonprefix samples')
            if recommendation and len(indices) not in (16,32,64,128): raise ValueError('noncheckpoint recommendation')
        if any(v>=count for v,i in samples[key]): raise ValueError('undeclared sample variant')
        if not int(forecast['record']) and samples[key]: raise ValueError('samples without record')
        if recommendation and any(int(r['status'])!=2 for r in samples[key].values()): raise ValueError('unsupported recommendation')
        winner=integer(actual,'winner',{0,1}); integer(actual,'actions',range(2001))
        initial=number(actual,'initial_value',1); lost=number(actual,'lost_value'); loss=number(actual,'loss_fraction',0,1)
        if not math.isclose(loss,lost/initial,rel_tol=1e-5,abs_tol=1e-6): raise ValueError('inconsistent actual loss')
        number(actual,'scalar_ratio'); integer(actual,'battlefield'); integer(actual,'split',range(1,8))
        for field,value in actual.items():
            if field.startswith('loss_') and field!='loss_fraction': number(actual,field)
        false_favorable=recommendation==1 and (winner!=0 or loss>tolerance)
        false_defeat=recommendation==2 and winner==0
        counts['actual_terminal']+=1; counts['sampled_scenarios']+=len(samples[key]); counts['started_draws']+=draws
        counts['captured']+=int(forecast['captured']); counts['all_finished_records']+=int(forecast['all_finished'])
        counts['false_favorable_observations']+=false_favorable; counts['false_defeat_observations']+=false_defeat
        counts['stopped_decisions']+=int(forecast['stopped'])
        counts[{0:'unknown',1:'favorable',2:'likely_defeat'}[recommendation]]+=1
        counts['unsupported_samples']+=sum(int(r['status'])==0 for r in samples[key].values())
        counts['budget_exhausted_samples']+=sum(int(r['status'])==1 for r in samples[key].values())
        counts['finished_samples']+=sum(int(r['status'])==2 for r in samples[key].values())
        counts['rollout_limit_samples']+=sum(int(r['status'])==3 for r in samples[key].values())
        cases.append(dict(family=key[0],oracle_seed=key[1],forecast=forecast,actual=actual,
            variants=list(variants[key].values()),samples=list(samples[key].values()),
            false_favorable_observation=false_favorable,false_defeat_observation=false_defeat))
    return dict(counts=dict(counts),cases=cases,interpretation='Independent fixed-policy server outcomes; simulator bounds are not calibrated real battle odds. No automatic activation approval.')
