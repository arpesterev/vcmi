import unittest
from decision_holdout_evidence import CASES, summarize_holdouts

def log():
    lines=[]
    for family,seed in CASES:
        prefix=f'family={family} oracle_seed={seed}'
        lines.append(f'FORCED_DECISION_FORECAST {prefix} captured=0 policy=0 revalidated=0 record=0 variants=0 draws=0 samples=0 recommendation=0 adverse_upper=1 defeat_lower=0 tolerance=-1 worst_loss=1 all_finished=0 own_magic=0 elapsed_us=42 stopped=0 reason=unsupported actual mechanics')
        lines.append(f'FORCED_DECISION_REFERENCE {prefix} winner=1 actions=20 battlefield=3 split=3 scalar_ratio=2 initial_value=100 lost_value=100 loss_fraction=1 loss_0_2=20')
    return '\n'.join(lines)

class HoldoutEvidenceTest(unittest.TestCase):
    def test_all_unknown_kept_descriptively_not_promoted(self):
        result=summarize_holdouts(log())
        self.assertEqual(result['counts']['actual_terminal'],13)
        self.assertEqual(result['counts']['captured'],0)
        self.assertEqual(result['counts']['unknown'],13)
    def test_three_variants_preserved_and_four_rejected(self):
        text=log(); family,seed=CASES[0]; first=text.splitlines()[0]
        text=text.replace(first, first.replace('variants=0','variants=3'))
        for variant in range(3):
            text+=f'\nFORCED_DECISION_VARIANT family={family} oracle_seed={seed} variant={variant} battlefield={variant+2} terrain=0'
        self.assertEqual(len(summarize_holdouts(text)['cases'][0]['variants']),3)
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('variants=3','variants=4'))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('draws=0','draws=385'))
    def test_missing_actual_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts('\n'.join(log().splitlines()[:-1]))
    def test_duplicate_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts(log()+'\n'+log().splitlines()[0])
    def test_unvalidated_recommendation_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts(log().replace('recommendation=0','recommendation=1'))
    def test_zero_variant_false_support_fails(self):
        text=log().replace('captured=0 policy=0 revalidated=0 record=0','captured=1 policy=1 revalidated=1 record=1').replace('recommendation=0','recommendation=1').replace('all_finished=0','all_finished=1')
        with self.assertRaises(ValueError): summarize_holdouts(text)
    def test_inconsistent_actual_loss_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts(log().replace('loss_fraction=1','loss_fraction=0'))
    def test_seed_substitution_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts(log().replace('oracle_seed=29009','oracle_seed=104729'))
    def test_nonfinite_loss_fails(self):
        with self.assertRaises(ValueError): summarize_holdouts(log().replace('loss_fraction=1','loss_fraction=nan'))
    def test_rollout_limit_retained_unknown_not_terminal_or_favorable(self):
        text=log(); family,seed=CASES[0]; prefix=f'family={family} oracle_seed={seed}'
        first=text.splitlines()[0]
        forecast=f'FORCED_DECISION_FORECAST {prefix} captured=1 policy=1 revalidated=1 record=1 variants=1 draws=1 samples=0 recommendation=0 adverse_upper=1 defeat_lower=0 tolerance=0.2 worst_loss=1 all_finished=0 own_magic=0 elapsed_us=10000 stopped=0 reason=none'
        text=text.replace(first,forecast)
        text+=f'\nFORCED_DECISION_VARIANT {prefix} variant=0 battlefield=3 terrain=0'
        sample=f'FORCED_DECISION_SAMPLE {prefix} variant=0 index=0 sampler_seed=104729 status=3 winner=-1 initial_value=100 remaining_value=50 actions=256 bonus=-1 reason=intrinsic action limit reached'
        text+='\n'+sample
        data=summarize_holdouts(text)
        self.assertEqual(data['counts']['rollout_limit_samples'],1)
        self.assertEqual(data['counts']['finished_samples'],0)
        self.assertEqual(data['counts']['unknown'],13)
        for bad in (sample.replace('winner=-1','winner=0'),sample.replace('intrinsic action limit reached','none'),sample.replace('status=3','status=4'),sample+' agrees=1'):
            with self.assertRaises(ValueError): summarize_holdouts(text.replace(sample,bad))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('recommendation=0','recommendation=1'))

    def test_valid_admitted_prefix_and_disagreement_kept(self):
        text=log(); family,seed=CASES[0]; prefix=f'family={family} oracle_seed={seed}'
        original=text.splitlines()[0]
        text=text.replace(original,f'FORCED_DECISION_FORECAST {prefix} captured=1 policy=1 revalidated=1 record=1 variants=1 draws=128 samples=128 recommendation=1 adverse_upper=0.04 defeat_lower=0 tolerance=0.2 worst_loss=0 all_finished=1 own_magic=0 elapsed_us=10000 stopped=0 reason=')
        text+=f'\nFORCED_DECISION_VARIANT {prefix} variant=0 battlefield=3 terrain=0'
        for i in range(128):
            text+=f'\nFORCED_DECISION_SAMPLE {prefix} variant=0 index={i} sampler_seed={104729+i*7919} status=2 winner=0 initial_value=100 remaining_value=100 actions=9 bonus=-1 reason=none'
        self.assertEqual(summarize_holdouts(text)['counts']['false_favorable_observations'],1)
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('status=2 winner=0','status=3 winner=-1').replace('reason=none','reason=intrinsic action limit reached'))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('tolerance=0.2','tolerance=-1'))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('initial_value=100 remaining_value=100','initial_value=0 remaining_value=0'))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('initial_value=100 remaining_value=100','initial_value=100 remaining_value=101'))
        with self.assertRaises(ValueError): summarize_holdouts(text.replace('sampler_seed=104729','sampler_seed=29009'))
        with self.assertRaises(ValueError): summarize_holdouts(text+'\n'+text.splitlines()[-1])
        with self.assertRaises(ValueError): summarize_holdouts('\n'.join(text.splitlines()[:-1]))

if __name__=='__main__': unittest.main()
