"""Unexecuted, public-source gameplay fixture drafts; never candidate-tuned."""
import copy
import json

NAMES = ('productive_move', 'stable_goal_progress', 'blocked_goal_alternative', 'urgent_two_town_defence')


def configure(repo, parts, name):
    templates = json.loads((repo / 'test/testdata/ObjectPropertyTest/objects.json').read_text())
    terrain_source = json.loads((repo / 'test/testdata/ObjectPropertyTest/surface_terrain.json').read_text())
    water = next(tile for row in terrain_source for tile in row if tile.startswith('wt'))
    header = parts['header.json']
    original = parts['objects.json']
    width, height = 30, 20
    terrain = [['dt40_' for _ in range(width)] for _ in range(height)]
    header['mapLevels']['surface'].update(width=width, height=height)
    objects = {}

    def hero(key, owner, hero_type, x, y, amount):
        obj = copy.deepcopy(original['hero_0'])
        obj.update(x=x, y=y)
        obj['options'] = {'owner': owner, 'type': hero_type,
                          'army': [{'type': 'core:pikeman', 'amount': amount}]}
        objects[key] = obj
        return obj

    def clone(key, template, x, y, options):
        obj = copy.deepcopy(templates[template])
        obj.update(x=x, y=y, options=options)
        objects[key] = obj
        return obj

    def control(key, kind):
        return ['control', {'object': key, 'type': kind}]

    # Red and blue have fixed distinct heroes; blue is isolated by water in the
    # non-combat scenarios, not by hidden bonuses or altered AI visibility.
    hero('hero_0', 'red', 'christian', 11, 8, 20)
    hero('hero_1', 'blue', 'lordHaart', 28, 18, 1)
    for y in range(16, 20):
        for x in range(25, 30):
            terrain[y][x] = water
    terrain[18][27] = terrain[18][28] = 'dt40_'
    manifest = {'scenario': name, 'status': 'draft_unexecuted', 'size': [width, height, 1],
                'player_ai': ['Nullkiller2', 'EmptyAI'], 'seed_set': [17041, 17047, 17053],
                'candidate_tuned': False, 'precondition_validation_required': True}
    if name == 'productive_move':
        hero('hero_0', 'red', 'christian', 5, 5, 20)
        clone('safe_objective', 'mine_3', 9, 5, {'owner': 'neutral'})
        victory = ['allOf', control('safe_objective', 'mine'), control('hero_0', 'hero')]
        deadline = 2
        manifest.update(objectives=['safe_objective'],
                        acceptance='Real callback move consumes movement; production planner captures visible mine by day 2.',
                        qualification_gate='Verify initial visibility, legal adjacent move, real server ownership delta and victory.')
    elif name == 'stable_goal_progress':
        # Objectives are visible across water but need a north/south detour.
        # A capture win proves post-action ownership, not just a planned visit.
        for y in range(1, 16):
            terrain[y][12] = water
        clone('objective_north', 'mine_3', 15, 6, {'owner': 'neutral'})
        clone('objective_south', 'mine_3', 15, 10, {'owner': 'neutral'})
        victory = ['allOf', control('objective_north', 'mine'), control('objective_south', 'mine')]
        deadline = 6
        manifest.update(objectives=['objective_north', 'objective_south'],
                        acceptance='Own both mines by day 6; compare movement reversals and completion day.',
                        qualification_gate='Both mines initially visible, no boat/spell bypass, each initial route exceeds one turn; verify real pathfinder costs before interpreting oscillation.')
    elif name == 'blocked_goal_alternative':
        hero('hero_0', 'red', 'christian', 5, 5, 20)
        hero('hero_helper', 'red', 'catherine', 13, 5, 20)
        header['players']['red']['heroes']['hero_helper'] = {'type': 'catherine'}
        clone('guarded_reward', 'resource_145', 7, 5,
              {'amount': 600, 'guards': [{'type': 'core:pikeman', 'amount': 999}]})
        clone('safe_objective', 'mine_3', 17, 5, {'owner': 'neutral'})
        victory = ['allOf', control('safe_objective', 'mine'), control('hero_0', 'hero'),
                   control('hero_helper', 'hero')]
        deadline = 2
        manifest.update(objectives=['safe_objective'], blocked_objective='guarded_reward',
                        acceptance='Capture safe mine by day 2 with both red heroes alive; guarded reward remains uncollected; record safe progress versus total refusal.',
                        qualification_gate='Confirm both targets visible to red and safe mine reachable by helper; guarded reward must be evaluated as unsafe. This is NOT an injected executeTask failure test.')
    elif name == 'urgent_two_town_defence':
        terrain = [['dt40_' for _ in range(width)] for _ in range(height)]
        # Castle anchor has visitable offset (-2,0); hero anchor has (-1,0).
        hero('hero_0', 'red', 'christian', 22, 7, 5)
        hero('hero_1', 'blue', 'lordHaart', 23, 10, 30)
        buildings = {'allOf': ['fort', 'dwellingLvl1', 'dwellingLvl2']}
        for key, x in [('town_safe', 8), ('town_threatened', 23)]:
            clone(key, 'town_188', x, 7,
                  {'owner': 'red', 'army': [{'type': 'core:pikeman', 'amount': 5}],
                   'buildings': copy.deepcopy(buildings)})
        header['events'] = [{'name': 'Public fixture constrained recruitment budget',
                             'players': ['red'], 'humanAffected': 1, 'computerAffected': 1,
                             'firstOccurrence': 0, 'nextOccurrence': 0,
                             'resources': {'gold': -8800}}]
        victory = ['allOf', ['daysPassed', {'value': 3}], control('town_safe', 'town'),
                   control('town_threatened', 'town')]
        deadline = 4
        manifest.update(player_ai=['Nullkiller2', 'Nullkiller2'], objectives=['town_safe', 'town_threatened'],
                        acceptance='Retain both towns through day 3, execute urgent recruitment before safe-town spending; no duplicate hero/garrison strength or overspend.',
                        qualification_gate='Verify red initial post-event gold=2200 (10000 base + 2x500 income - 8800), visitor/garrison separation, recruit availability, visible reachable turn-0/1 threat. Reject fixture qualification if timing differs; do not silently tune expectations.',
                        additional_evidence='Town survival alone cannot prove defence: require executed per-town recruitment/resource deltas and threat reachability. Blue abstaining without a threat is inconclusive.')
    else:
        raise ValueError(name)
    header['triggeredEvents'] = {
        'objectiveVictory': {'condition': victory, 'effect': {'type': 'victory'}},
        'evaluationDeadline': {'condition': ['daysPassed', {'value': deadline}], 'effect': {'type': 'defeat'}}}
    header['name'] = 'Public gameplay draft: ' + name
    parts['objects.json'], parts['surface_terrain.json'] = objects, terrain
    manifest['deadline_day'] = deadline
    manifest['victory_condition'] = victory
    manifest['object_anchors'] = {key: [obj['x'], obj['y'], obj['l']] for key, obj in objects.items()}
    return manifest
