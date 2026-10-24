import unittest
from forced_battle_evidence import EXPECTED_TEST_NAMES, REQUIRED_SMOKES, expected_cases, summarize_evidence


def make_log(all_admitted=False):
    lines=[]
    for spec in expected_cases():
        name=spec['id']; seed=spec['seed']; admitted=int(all_admitted or name in REQUIRED_SMOKES)
        reason='none' if admitted else 'visibility or owner eligibility is unsupported'
        if spec['kind']=='direct':
            lines.append(f'FORCED_BATTLE_CAPTURE case={name} seed={seed} admitted={admitted} reason={reason}')
            lines.append(f'FORCED_BATTLE_REFERENCE case={name} seed={seed} winner=attacker actions=9 scalar_ratio=1.2 lost_ai_a=60 lost_ai_d=100 loss_a_56=1 forecast_status=0 forecast_winner=-1 forecast_status=0 forecast_winner=-1')
            if admitted:
                for index in range(3):
                    lines.append(f'FORCED_BATTLE_SAMPLE case={name} oracle_seed={seed} sample_index={index} status=2 winner=0 reason=none bonus=-1 a_survivors=10 d_survivors=0 a_lost_value=60 d_lost_value=100 agrees=1')
        else:
            key=f"upgrade={spec['upgrade']} specialty={spec['specialty']}"
            lines.append(f'FORCED_NEUTRAL_CAPTURE {key} field=3 admitted={admitted} reason={reason}')
            if admitted:
                # Real fixture emits neutral samples before its terminal row.
                for sample_seed in (17041,17047,17053):
                    lines.append(f'FORCED_NEUTRAL_SAMPLE {key} sample_seed={sample_seed} status=2 winner=1 reason=none')
            lines.append(f'FORCED_NEUTRAL_REFERENCE {key} battlefield=3 seed={seed} split=3 winner=1 actions=20 loss_0_2=20 loss_1_58=12')
    return '\n'.join(lines)


class EvidenceTest(unittest.TestCase):
    def test_exact_matrix(self):
        specs=expected_cases()
        self.assertEqual(len(specs),58)
        self.assertEqual(len(set(EXPECTED_TEST_NAMES)),58)
        self.assertEqual(sum(s['kind']=='direct' for s in specs),55)
        self.assertIn('Predeclared/ForcedBattleCountSweep.ArcherWalkingDead/41',EXPECTED_TEST_NAMES)
        self.assertIn('ForcedBattleReference.NativeHeroSpecialtyTroops',EXPECTED_TEST_NAMES)

    def test_partial_support_is_valid_not_174_predictions(self):
        data=summarize_evidence(make_log())
        self.assertEqual(data['counts']['actual_terminal'],58)
        self.assertEqual(data['counts']['sample_executions'],12)
        self.assertEqual(data['counts']['selected_capture_rejected'],54)
        self.assertEqual(data['rejected_capture_reasons'],{'visibility or owner eligibility is unsupported':54})

    def test_full_support_174_samples(self):
        data=summarize_evidence(make_log(True))
        self.assertEqual(data['counts']['samples_finished'],174)
        self.assertEqual(data['counts']['captures_admitted'],58)
        self.assertEqual(data['counts']['finished_winner_agrees'],174)

    def test_neutral_variants_counted_separately(self):
        text=make_log()+'\nFORCED_NEUTRAL_CAPTURE upgrade=0 specialty=0 field=4 admitted=1 reason=none'
        data=summarize_evidence(text)
        self.assertEqual(data['counts']['capture_opportunities'],59)
        self.assertEqual(data['counts']['captures_admitted'],5)
        self.assertEqual(data['counts']['sample_executions'],12)

    def test_non_smoke_unsupported_sample_reason_spaces_preserved(self):
        text=make_log(True).replace('case=mixed_high oracle_seed=17041 sample_index=0 status=2 winner=0 reason=none bonus=-1 a_survivors=10 d_survivors=0 a_lost_value=60 d_lost_value=100 agrees=1',
          'case=mixed_high oracle_seed=17041 sample_index=0 status=0 winner=-1 reason=unknown bonus on a unit bonus=17 a_survivors=0 d_survivors=0 a_lost_value=0 d_lost_value=0')
        data=summarize_evidence(text)
        self.assertEqual(data['counts']['samples_unsupported'],1)
        self.assertEqual(data['nonterminal_sample_reasons'],{'unknown bonus on a unit':1})

    def test_rollout_limit_is_nonterminal_and_not_agreement(self):
        text=make_log(True)
        old=next(x for x in text.splitlines() if x.startswith('FORCED_BATTLE_SAMPLE case=mixed_high ') and 'sample_index=0 ' in x)
        new='FORCED_BATTLE_SAMPLE case=mixed_high oracle_seed=17041 sample_index=0 status=3 winner=-1 reason=intrinsic action limit reached bonus=-1'
        text=text.replace(old,new)
        data=summarize_evidence(text)
        self.assertEqual(data['counts']['samples_rollout_limit'],1)
        self.assertEqual(data['counts']['samples_finished'],173)
        self.assertEqual(data['counts']['finished_winner_agrees'],173)
        self.assertIn('intrinsic action limit reached',data['nonterminal_sample_reasons'])
        for bad in (new.replace('winner=-1','winner=0'),new+' agrees=1',new.replace('intrinsic action limit reached','none')):
            self.assert_bad(text.replace(new,bad))

    def test_rollout_limit_cannot_pass_required_smoke(self):
        text=make_log().replace('FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=2 winner=1 reason=none',
            'FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=3 winner=-1 reason=intrinsic round limit reached')
        self.assert_bad(text)
        data=summarize_evidence(text,require_smokes=False)
        self.assertEqual(data['counts']['samples_rollout_limit'],1)
        self.assertTrue(data['supported_smoke_failures'])

    def test_valid_disagreement_is_not_rejected(self):
        text=make_log().replace('sample_index=0 status=2 winner=0','sample_index=0 status=2 winner=1').replace('d_lost_value=100 agrees=1','d_lost_value=100')
        data=summarize_evidence(text)
        self.assertEqual(data['counts']['finished_winner_disagrees'],3)

    def assert_bad(self,text):
        with self.assertRaises(ValueError): summarize_evidence(text)

    def test_missing_actual(self):
        self.assert_bad('\n'.join(x for x in make_log().splitlines() if not x.startswith('FORCED_BATTLE_REFERENCE case=ordinary_melee ')))
    def test_duplicate_actual(self):
        text=make_log(); self.assert_bad(text+'\n'+next(x for x in text.splitlines() if x.startswith('FORCED_BATTLE_REFERENCE')))
    def test_duplicate_capture(self):
        text=make_log(); self.assert_bad(text+'\n'+text.splitlines()[0])
    def test_duplicate_sample(self):
        text=make_log(); self.assert_bad(text+'\n'+next(x for x in text.splitlines() if x.startswith('FORCED_BATTLE_SAMPLE')))
    def test_missing_admitted_sample(self):
        text=make_log(); self.assert_bad(text.replace(next(x for x in text.splitlines() if x.startswith('FORCED_BATTLE_SAMPLE')),''))
    def test_extra_sample_for_rejected_capture(self):
        self.assert_bad(make_log()+'\nFORCED_BATTLE_SAMPLE case=mixed_high oracle_seed=17041 sample_index=0 status=2 winner=0 reason=none')
    def test_smoke_rejected_even_when_all_real_battles_complete(self):
        text=make_log().replace('case=ordinary_melee seed=17041 admitted=1 reason=none','case=ordinary_melee seed=17041 admitted=0 reason=unsupported ordinary troops')
        self.assert_bad('\n'.join(x for x in text.splitlines() if not x.startswith('FORCED_BATTLE_SAMPLE case=ordinary_melee ')))
    def test_production_neutral_capture_cannot_all_be_unknown(self):
        text=make_log().replace('FORCED_NEUTRAL_CAPTURE upgrade=0 specialty=0 field=3 admitted=1 reason=none',
          'FORCED_NEUTRAL_CAPTURE upgrade=0 specialty=0 field=3 admitted=0 reason=visibility or owner unsupported')
        self.assert_bad('\n'.join(x for x in text.splitlines() if not x.startswith('FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 ')))

    def test_production_neutral_samples_must_finish(self):
        text=make_log().replace('FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=2 winner=1 reason=none',
          'FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=1 winner=-1 reason=budget exhausted')
        self.assert_bad(text)

    def test_smoke_samples_must_finish(self):
        text=make_log().replace('case=ordinary_melee oracle_seed=17041 sample_index=0 status=2 winner=0 reason=none bonus=-1 a_survivors=10 d_survivors=0 a_lost_value=60 d_lost_value=100 agrees=1',
          'case=ordinary_melee oracle_seed=17041 sample_index=0 status=1 winner=-1 reason=budget exhausted bonus=-1')
        self.assert_bad(text)
    def test_descriptive_summary_retains_failed_smoke_without_accepting_it(self):
        text=make_log().replace('FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=2 winner=1 reason=none',
          'FORCED_NEUTRAL_SAMPLE upgrade=0 specialty=0 sample_seed=17041 status=1 winner=-1 reason=budget exhausted')
        summary=summarize_evidence(text, require_smokes=False)
        self.assertEqual(summary['counts']['actual_terminal'],58)
        self.assertEqual(len(summary['supported_smoke_failures']),1)
        self.assertIn('neutral_0_0',summary['supported_smoke_failures'][0])
        self.assert_bad(text)

    def test_descriptive_mode_still_rejects_malformed_evidence(self):
        with self.assertRaises(ValueError):
            summarize_evidence(make_log().replace('status=2 winner=0','status=7 winner=0'), require_smokes=False)

    def test_neutral_specialty_is_required(self):
        self.assert_bad(make_log().replace('FORCED_NEUTRAL_REFERENCE upgrade=0 specialty=1','FORCED_NEUTRAL_REFERENCE upgrade=0'))
    def test_wrong_neutral_field(self):
        self.assert_bad(make_log().replace('battlefield=3','battlefield=99'))
    def test_wrong_seed(self): self.assert_bad(make_log().replace('case=ordinary_melee seed=17041 winner','case=ordinary_melee seed=9 winner'))
    def test_invalid_status(self): self.assert_bad(make_log().replace('status=2 winner=0','status=7 winner=0'))
    def test_nonterminal_winner(self): self.assert_bad(make_log().replace('status=2 winner=0','status=0 winner=0'))
    def test_negative_casualty(self): self.assert_bad(make_log().replace('loss_a_56=1','loss_a_56=-1'))
    def test_nan_loss(self): self.assert_bad(make_log().replace('lost_ai_a=60','lost_ai_a=nan'))
    def test_unknown_case(self): self.assert_bad(make_log().replace('case=mixed_low','case=not_declared'))
    def test_duplicate_key(self): self.assert_bad(make_log().replace('actions=9','actions=9 actions=9'))
    def test_false_agreement(self): self.assert_bad(make_log().replace('agrees=1','agrees=0'))
    def test_missing_rejection_reason(self): self.assert_bad(make_log().replace('reason=visibility or owner eligibility is unsupported','reason='))

if __name__=='__main__': unittest.main()
