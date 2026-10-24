#!/usr/bin/env python3
"""Public synthetic corridor; never reads user saves or private map data."""
import argparse
import copy
import json
from pathlib import Path
import zipfile
p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
source = a.output / 'blocked_goal_alternative.vmap'
with zipfile.ZipFile(source) as z:
    parts = {name: json.loads(z.read(name)) for name in z.namelist()}
objects = parts['objects.json']
objects['hero_0']['x'] = 6
objects['guarded_reward']['x'] = 10
objects['blocked_reward']['x'] = 11
weak = copy.deepcopy(objects['guarded_reward'])
weak['x'] = 7
weak['options']['amount'] = 1
objects['earlier_guard'] = weak
terrain = parts['surface_terrain.json']
reference = json.loads((a.repo / 'test/testdata/ObjectPropertyTest/surface_terrain.json').read_text())
water = next(tile for row in reference for tile in row if tile.startswith('wt'))
for x in range(3, 13):
    terrain[4][x] = terrain[6][x] = water
terrain[5][3] = terrain[5][12] = water
# Exact CONTROL hero location follows moved hero. Other objectives unchanged.
def fix_condition(value):
    if isinstance(value, list):
        if len(value) == 2 and value[0] == 'control' and isinstance(value[1], dict) and value[1].get('object') == 'hero_0':
            value[1]['position'][0] += 1
        for child in value: fix_condition(child)
    elif isinstance(value, dict):
        for child in value.values(): fix_condition(child)
fix_condition(parts['header.json'])
with zipfile.ZipFile(a.output / 'risk_earlier_guard.vmap', 'w', zipfile.ZIP_DEFLATED) as z:
    for name, data in sorted(parts.items()):
        info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
        z.writestr(info, json.dumps(data, sort_keys=True, separators=(',', ':')))

# Independent declared control: actual pathfinder must round this prior fight's
# loss to zero while retaining positive encounter danger. No production bonuses.
parts['objects.json']['earlier_guard']['subtype'] = 'peasant'
with zipfile.ZipFile(a.output / 'risk_zero_loss_earlier_guard.vmap', 'w', zipfile.ZIP_DEFLATED) as z:
    for name, data in sorted(parts.items()):
        info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
        z.writestr(info, json.dumps(data, sort_keys=True, separators=(',', ':')))
