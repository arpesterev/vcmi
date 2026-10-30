#!/usr/bin/env python3
"""Run the real headless client on a generated map and validate applied game events."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import time
from typing import Any


class MatchEvidence:
    def __init__(self) -> None:
        self.initial: dict[int, dict[str, Any]] = {}
        self.last: dict[str, Any] = {}
        self.events: dict[str, int] = {}
        self.resources: set[int] = set()
        self.artifacts: set[int] = set()
        self.guards: set[int] = set()
        self.fought_guards: set[int] = set()
        self.damaging_battles: set[int] = set()
        self.finished_battles: set[int] = set()
        self.guard_battles: dict[int, int] = {}
        self.guarded_artifacts: set[int] = set()
        self.towns_built: set[int] = set()
        self.towns_recruited: set[int] = set()
        self.towns_captured: set[int] = set()
        self.winners: set[int] = set()
        self.ended: set[int] = set()
        self.turns: dict[int, int] = {}
        self.last_seq = -1
        self.damage = 0
        self.errors: list[str] = []
        self.previous: dict[int, dict[str, Any]] = {}

    def accept(self, event: dict[str, Any]) -> None:
        if event['seq'] <= self.last_seq:
            raise ValueError('Trace sequence is not strictly increasing')
        self.last_seq = event['seq']
        name = event['event']
        self.events[name] = self.events.get(name, 0) + 1
        objects = {obj['id']: obj for obj in event['objects']}
        if not self.initial:
            self.initial = objects.copy()
            self.guards = {i for i, obj in objects.items() if obj['kind'] == 'monster'}
        if name == 'RemoveObject':
            object_id = event['object']
            old = self.initial.get(object_id, {})
            if object_id not in objects:
                if old.get('kind') == 'resource':
                    self.resources.add(object_id)
                if old.get('kind') == 'artifact':
                    self.artifacts.add(object_id)
                    # The fixture places its sole artifact beside the corridor guard.
                    if any(guard not in objects and
                           abs(self.initial[guard]['x'] - old['x']) <= 1 and
                           abs(self.initial[guard]['y'] - old['y']) <= 1 and
                           self.initial[guard]['z'] == old['z']
                           for guard in self.fought_guards):
                        self.guarded_artifacts.add(object_id)
        if name == 'BattleStart' and event.get('defender') in self.guards:
            self.guard_battles[event['battle']] = event['defender']
        if name == 'BattleAttack':
            self.damage += event['damage']
            if event['damage'] > 0:
                self.damaging_battles.add(event['battle'])
        if name == 'BattleResultAccepted':
            self.finished_battles.add(event['battle'])
            if (event['battle'] in self.guard_battles and event['battle'] in self.damaging_battles
                    and event.get('attacker_won') is True):
                self.fought_guards.add(self.guard_battles[event['battle']])
        if name == 'NewStructures':
            town_id = event['town']
            if objects.get(town_id, {}).get('buildings', 0) > self.initial.get(town_id, {}).get('buildings', 0):
                self.towns_built.add(town_id)
        if name == 'SetAvailableCreatures':
            town_id = event['town']
            now = objects.get(town_id, {})
            before = self.previous.get(town_id, {})
            if now.get('kind') == 'town' and now.get('stock', 0) < before.get('stock', 0):
                self.towns_recruited.add(town_id)
        for i, obj in objects.items():
            old = self.initial.get(i, {})
            if obj['kind'] == 'town' and old and obj['owner'] != old['owner']:
                self.towns_captured.add(i)
        if name == 'PlayerEndsTurn':
            player = event['player']
            self.turns[player] = self.turns.get(player, 0) + 1
        if name == 'PlayerEndsGame':
            self.ended.add(event['player'])
            if event['victory']:
                self.winners.add(event['player'])
        if name == 'PackageApplied' and not event['accepted']:
            # Preserve diagnostics; an occasional rejected stale action is not a match failure.
            self.errors.append(f"day {event['day']}: rejected request {event['request']}")
        self.previous = objects
        self.last = event

    def failures(self, returncode: int | None, stop_reason: str | None) -> list[str]:
        checks = {
            'gameplay never started': bool(self.initial),
            'both players must complete a turn': len(self.turns) == 2 and min(self.turns.values()) >= 1,
            'no resource pile collected': bool(self.resources),
            'no town construction': bool(self.towns_built),
            'no creature recruitment': bool(self.towns_recruited),
            'no real combat damage': self.damage > 0,
            'no confirmed battle result': bool(self.finished_battles),
            'guarded artifact not collected after a real guard battle': bool(self.guarded_artifacts),
            'no town captured': bool(self.towns_captured),
            'no unique winner and two completed players': len(self.winners) == 1 and len(self.ended) == 2,
            'client did not exit normally': returncode == 0 and stop_reason is None,
        }
        return [label for label, passed in checks.items() if not passed]

    def summary(self) -> dict[str, Any]:
        return {
            'last_day': self.last.get('day'), 'turns': self.turns,
            'resource_pickups': sorted(self.resources),
            'guarded_artifact_pickups': sorted(self.guarded_artifacts),
            'towns_built': sorted(self.towns_built),
            'towns_recruited': sorted(self.towns_recruited),
            'towns_captured': sorted(self.towns_captured),
            'battles_completed': sorted(self.finished_battles),
            'battle_damage': self.damage, 'winners': sorted(self.winners),
            'events': self.events, 'rejected_requests': self.errors[-20:],
            'last_state': self.last,
        }


def stop_process_group(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def run_case(client: Path, map_path: Path, data: Path, output: Path, ai: str,
             max_days: int, timeout: int, seed: int) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    home = output / 'home'
    config = home / '.config' / 'vcmi'
    user_data = home / '.local' / 'share' / 'vcmi'
    config.mkdir(parents=True)
    user_data.mkdir(parents=True)
    # Share only read-only game resources; saves/config/cache stay inside this run.
    for entry in data.iterdir():
        if entry.name.lower() not in {'saves', 'config', 'cache', 'logs', 'maps'}:
            (user_data / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    maps = user_data / 'Maps'
    maps.mkdir()
    shutil.copy2(map_path, maps / 'ai-regression.h3m')
    (config / 'settings.json').write_text(json.dumps({
        'ai': {'adventureEnemyAI': ai, 'adventureAlliedAI': ai,
               'combatAI': 'BattleAI', 'combatNeutralAI': 'BattleAI'},
        'server': {'useProcess': False},
        'general': {'lastDifficulty': 1, 'saveFrequency': 1},
        'session': {'headless': True, 'onlyai': True},
    }), encoding='utf-8')
    trace = output / 'events.jsonl'
    log = output / 'client.log'
    env = os.environ.copy()
    env.update(HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
               XDG_CACHE_HOME=str(home / '.cache'),
               XDG_DATA_HOME=str(home / '.local' / 'share'),
               VCMI_AI_MATCH_TRACE=str(trace), VCMI_TEST_RNG_SEED=str(seed),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    evidence = MatchEvidence()
    pending = ''
    offset = 0
    reason = None
    start = time.monotonic()
    with log.open('wb') as stream:
        process = subprocess.Popen([str(client), '--headless', '--testmap', 'Maps/ai-regression.h3m',
                                    '--savefrequency', '1'], cwd=client.parent,
                                   env=env, stdin=subprocess.DEVNULL, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while True:
                if trace.exists():
                    with trace.open('r', encoding='utf-8') as source:
                        source.seek(offset)
                        pending += source.read()
                        offset = source.tell()
                    lines = pending.split('\n')
                    pending = lines.pop()
                    for line in lines:
                        if line:
                            evidence.accept(json.loads(line))
                if process.poll() is not None:
                    # Read any records flushed between the preceding read and process exit.
                    if trace.exists():
                        with trace.open('r', encoding='utf-8') as source:
                            source.seek(offset)
                            pending += source.read()
                        for line in pending.splitlines():
                            if line:
                                evidence.accept(json.loads(line))
                        pending = ''
                    break
                if evidence.last.get('day', 0) > max_days:
                    reason = 'day-limit'
                    break
                if time.monotonic() - start > timeout:
                    reason = 'wall-timeout'
                    break
                time.sleep(0.05)
        except (OSError, ValueError, KeyError, TypeError) as error:
            reason = f'invalid-trace: {error}'
        finally:
            stop_process_group(process)
    failures = evidence.failures(process.returncode, reason)
    result = {'ai': ai, 'seed': seed, 'returncode': process.returncode,
              'stop_reason': reason, 'elapsed_seconds': round(time.monotonic() - start, 2),
              'failures': failures, **evidence.summary()}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'last_state'}, indent=2), flush=True)
    if failures:
        print('Last client log lines:\n' + '\n'.join(log.read_text(errors='replace').splitlines()[-80:]), flush=True)
        print('Last applied state:\n' + json.dumps(evidence.last, indent=2), flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--map', dest='map_path', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=1337)
    parser.add_argument('--max-days', type=int, default=28)
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('The CI harness currently requires Linux/XDG and process groups')
    if not -(2**31) <= args.seed < 2**31 or args.max_days <= 0 or args.timeout <= 0:
        parser.error('Seed must fit int32; day and wall-time limits must be positive')
    for name in ('client', 'map_path', 'data', 'output'):
        setattr(args, name, getattr(args, name).resolve())
    if not args.client.is_file() or not args.map_path.is_file() or not args.data.is_dir():
        parser.error('Client, generated map and installed game data must exist')
    negative = run_case(args.client, args.map_path, args.data, args.output / 'empty-ai',
                        'EmptyAI', 4, min(args.timeout, 120), args.seed)
    negative_ok = (negative['stop_reason'] == 'day-limit' and len(negative['turns']) == 2
                   and negative['last_day'] >= 5 and bool(negative['failures'])
                   and not negative['winners'] and not negative['resource_pickups'])
    positive = run_case(args.client, args.map_path, args.data, args.output / 'nullkiller2',
                        'Nullkiller2', args.max_days, args.timeout, args.seed)
    passed = negative_ok and not positive['failures']
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as stream:
            stream.write('\n## Full-map AI regression\n\n')
            stream.write(f"EmptyAI negative control: {'PASS' if negative_ok else 'FAIL'}\n\n")
            stream.write(f"Nullkiller2 real match: {'PASS' if not positive['failures'] else 'FAIL'}\n\n")
            stream.write('```json\n' + json.dumps({k: v for k, v in positive.items()
                         if k not in {'last_state', 'events'}}, indent=2) + '\n```\n')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
