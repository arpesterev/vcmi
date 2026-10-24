#!/usr/bin/env python3
"""Unexecuted public interaction drafts; no user saves or hidden information."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import zipfile


def generate(repo, output):
    spec = importlib.util.spec_from_file_location('interaction_source_scenarios', repo / 'CI/adventure_eval/scenarios.py')
    scenarios = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scenarios)
    source = repo / 'test/testdata/MiniTest'
    parts = {name: json.loads((source / name).read_text()) for name in ('header.json', 'objects.json', 'surface_terrain.json')}
    # Use only public templates, preserving normal player visibility/resources.
    scenarios.configure(repo, parts, 'urgent_two_town_defence')
    header, objects = parts['header.json'], parts['objects.json']
    # NewTurnProcessor::generateNewTurn skips player income on firstTurn.
    # Expert AI starts at 10000; preserve declared 2200 budget, not an assumed
    # day 1 payment from the two Village Halls. Shared scenario stays unchanged.
    header['events'][0]['resources']['gold'] = -7800
    for key, color, hero_type in (('hero_0', 'red', 'christian'), ('hero_1', 'blue', 'lordHaart')):
        header['players'][color]['mainHero'] = hero_type
        header['players'][color]['heroes'][key]['type'] = hero_type
    templates = json.loads((repo / 'test/testdata/ObjectPropertyTest/objects.json').read_text())
    header['mapLevels']['surface'].update(width=64, height=48)
    parts['surface_terrain.json'] = [['dt40_' for _ in range(64)] for _ in range(48)]
    for key, position in {'town_safe': (3, 3), 'town_threatened': (45, 7), 'hero_0': (44, 7), 'hero_1': (45, 10)}.items():
        objects[key].update(x=position[0], y=position[1])
    objects['safe_objective'] = copy.deepcopy(templates['mine_3'])
    # Omit owner for the native NEUTRAL default. 'neutral' is not a
    # registered playerColor identifier and leaves constructor UNFLAGGABLE.
    objects['safe_objective'].update(x=40, y=9, options={})
    # An ordinary owned ore mine, not a reveal cheat, keeps the scripted retreat
    # endpoint observable. The enemy must not step on/capture its visitable tile.
    objects['retreat_observer'] = copy.deepcopy(templates['mine_4'])
    objects['retreat_observer'].update(x=47, y=25, options={'owner': 'red'})
    objects['retreat_observer_far'] = copy.deepcopy(objects['retreat_observer'])
    objects['retreat_observer_far'].update(y=40)
    # Native radius3 sight at x46 covers retreat x44 over y±2. Fill both
    # observed gaps without granting vision or occupying blue's route.
    for y in (15, 20, 30, 35):
        objects[f'retreat_corridor_{y}'] = copy.deepcopy(objects['retreat_observer'])
        objects[f'retreat_corridor_{y}']['y'] = y
    header['triggeredEvents'] = {
        'objectiveVictory': {'condition': ['allOf', ['daysPassed', {'value': 3}],
            scenarios.control_object(objects, 'town_safe', 'town'),
            scenarios.control_object(objects, 'town_threatened', 'town'),
            scenarios.control_object(objects, 'safe_objective', 'mine')], 'effect': {'type': 'victory'}},
        'evaluationDeadline': {'condition': ['daysPassed', {'value': 4}], 'effect': {'type': 'defeat'}}}
    header['name'] = 'Public interaction draft: defend then resume economy'
    output.mkdir(parents=True, exist_ok=True)
    target = output / 'defence_economy_three_turns.vmap'
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, value in sorted(parts.items()):
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, json.dumps(value, sort_keys=True, separators=(',', ':')))
    manifest = {'status': 'draft_unexecuted', 'seed': 17041, 'difficulty': 3,
                'map_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
                'blue_scripted_retreat_anchors': [[45, 25, 0], [45, 40, 0]], 'known_visibility_required': True,
                'acceptance': 'Urgent actual town recruitment before nonurgent spending, conserved budget/stock, productive covered economy, then actual adequate town defence before a legally approaching enemy can attack.',
                'unsupported': ['battles', 'hero exchanges', 'blocking queries'],
                'source_sha256': {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in [*(source / name for name in parts), repo / 'test/testdata/ObjectPropertyTest/objects.json', repo / 'test/testdata/ObjectPropertyTest/surface_terrain.json', repo / 'CI/adventure_eval/scenarios.py']}}
    (output / 'interaction-provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.repo, args.output), sort_keys=True))
