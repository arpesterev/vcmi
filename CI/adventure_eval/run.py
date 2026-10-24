#!/usr/bin/env python3
"""Draft software-rendered public-fixture qualification, not a gameplay-quality benchmark."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import sys
import subprocess
import time
import zipfile

try:
    from . import scenarios
except ImportError:
    import importlib.util
    _spec = importlib.util.spec_from_file_location('adventure_scenarios', Path(__file__).with_name('scenarios.py'))
    scenarios = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(scenarios)

BASELINE = '8fda7bdc69887e8c86d27a149681fc598c3cd8d1'
DATA_URL = 'https://github.com/vcmi-mods/vcmi-test-data/releases/download/v2.0/heroes3.7z'
DATA_SHA256 = '64247aeee0342c6f349576ba700a81e2de46de7b9aa19ca4881b8d59f5e0ced8'
SEEDS = (17041, 17047, 17053)


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def generate(repo, destination, scenario):
    source = repo / 'test/testdata/MiniTest'
    parts = {name: json.loads((source / name).read_text()) for name in
             ('header.json', 'objects.json', 'surface_terrain.json')}
    header, objects = parts['header.json'], parts['objects.json']
    header['name'] = 'Public adventure evaluation: ' + scenario
    # DAYS_PASSED is day > value, so value=2 ends on day 3.
    header['triggeredEvents'] = {
        'evaluationDeadline': {'condition': ['daysPassed', {'value': 2}],
                               'effect': {'type': 'defeat'}},
        'standardVictory': {'condition': ['standardWin'], 'effect': {'type': 'victory'}}}
    # Fix hero types (including header metadata), armies and terrain: no random heroes.
    for key, color in [('hero_0', 'red'), ('hero_1', 'blue')]:
        hero_type = 'christian' if color == 'red' else 'lordHaart'
        header['players'][color]['mainHero'] = hero_type
        header['players'][color]['heroes'][key]['type'] = hero_type
        objects[key]['options']['type'] = hero_type
        objects[key]['options']['army'] = [{'type': 'pikeman', 'amount': 1}]
    if scenario == 'battle_smoke':
        objects['hero_0']['options']['army'][0]['amount'] = 200
        objects['hero_1']['x'] = 4  # Close, visible target; distinct non-overlapping masks.
    gameplay_manifest = scenarios.configure(repo, parts, scenario) if scenario in scenarios.NAMES else None
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, value in sorted(parts.items()):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, json.dumps(value, sort_keys=True, separators=(',', ':')))
    evidence = {'map_sha256': digest(destination), 'source_sha256': {
        name: digest(source / name) for name in parts}}
    if gameplay_manifest:
        evidence['gameplay_manifest'] = gameplay_manifest
        for name in ('objects.json', 'surface_terrain.json'):
            evidence['source_sha256']['ObjectPropertyTest/' + name] = digest(repo / 'test/testdata/ObjectPropertyTest' / name)
    return evidence



def read_logs(home, stdout_path):
    paths = [stdout_path, *sorted((home / 'Library/Logs/vcmi').glob('*.txt')),
             *sorted((home / 'Library/Logs/vcmi').glob('*.log'))]
    return '\n'.join(p.read_text(errors='replace') for p in paths if p.is_file())


def observed_outcomes(text, scenario):
    terminal = re.findall(r'Red player (won|lost)\. Ending game\.', text)
    battles = re.findall(r'Player 0 \([^)]*\): I (won|lost) the Starting battle[^\n]*', text)
    moves = re.findall(r'Hero [^\n]* moved from [^\n]*', text)
    errors = re.findall(r'[^\n]*(?:FATAL ERROR ENCOUNTERED|terminate called|Segmentation fault|Assertion.*failed)[^\n]*', text)
    days = [int(day) for day in re.findall(r'Info about turn (\d+) has been sent!', text)]
    consistent = len(set(terminal)) == 1 and not errors
    completed = consistent and ((scenario == 'termination_smoke' and terminal[-1] == 'lost' and 3 in days) or
                    (scenario == 'battle_smoke' and terminal[-1] == 'won' and 'won' in battles and bool(moves)))
    return {'outcomes_verified': bool(completed), 'terminal': terminal,
            'red_battle_outcomes': battles, 'hero_move_records': moves, 'fatal_records': errors, 'observed_days': sorted(set(days))}


def classify(text, code, timed_out, scenario, controlled_stop=False):
    result = observed_outcomes(text, scenario)
    # Crash after a terminal log is not a pass. A signal return is accepted only
    # when this supervisor deliberately sent that signal AFTER verifying outcomes.
    process_ok = code == 0 or (controlled_stop and code in (-signal.SIGTERM, -signal.SIGKILL))
    passed = result['outcomes_verified'] and not timed_out and process_ok
    result.update(status=('game_complete_controlled_teardown' if controlled_stop else
                          'game_complete_graceful_exit') if passed else 'incomplete',
                  controlled_teardown=controlled_stop,
                  meaning='Engine smoke only; no strategic-quality or win-rate conclusion.')
    return result


def supervise(command, env, stdout_path, home, seconds, scenario):
    start = time.monotonic()
    controlled = timed_out = False
    with stdout_path.open('w') as output:
        proc = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT,
                                start_new_session=True)
        while proc.poll() is None:
            text = read_logs(home, stdout_path)
            if observed_outcomes(text, scenario)['outcomes_verified']:
                # Reject a crash already observed before requesting termination.
                if proc.poll() is not None:
                    break
                controlled = True
                os.killpg(proc.pid, signal.SIGTERM)
                break
            if time.monotonic() - start >= seconds:
                timed_out = True
                os.killpg(proc.pid, signal.SIGTERM)
                break
            time.sleep(0.1)
        try:
            code = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            code = proc.wait()
        finally:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
    return code, timed_out, controlled, time.monotonic() - start


def main(argv=None):
    started = time.monotonic()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--app', type=Path, help='Already built/signed VCMI.app; never rebuilt or modified')
    parser.add_argument('--data-archive', type=Path, help='Only checksum-pinned public heroes3.7z')
    parser.add_argument('--output', type=Path, required=True, help='New, nonexistent evidence directory')
    parser.add_argument('--source-commit', required=True, help='Exact source commit for this app, supplied by CI')
    parser.add_argument('--scenario', choices=['termination_smoke', 'battle_smoke', *scenarios.NAMES], default='termination_smoke')
    parser.add_argument('--seed', type=int, choices=SEEDS, default=SEEDS[0])
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--generate-only', action='store_true')
    parser.add_argument('--budget-seconds', type=float, default=600, help='Remaining qualification budget, including setup')
    args = parser.parse_args(argv)
    if not 10 <= args.budget_seconds <= 600:
        parser.error('budget must be 10..600 seconds')
    if not 1 <= args.timeout <= 180:
        parser.error('timeout must be 1..180 seconds')
    if args.output.exists():
        parser.error('output must be nonexistent; do not mix old evidence')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True)
    home = args.output / 'isolated-home'
    data = home / 'Library/Application Support/vcmi'
    data.mkdir(parents=True)
    map_path = data / 'Maps' / (args.scenario + '.vmap')
    evidence = generate(args.repo.resolve(), map_path, args.scenario)
    evidence.update(baseline=BASELINE, source_commit=args.source_commit, seed=args.seed,
                    scenario=args.scenario, data_url=DATA_URL, data_sha256=DATA_SHA256,
                    status='generated_only', engine_executed=False)
    result = args.output / 'result.json'
    result.write_text(json.dumps(evidence, indent=2))
    if args.generate_only:
        return 0
    if args.scenario in scenarios.NAMES:
        evidence.update(status='draft_requires_fixture_qualification', reason='Gameplay maps currently support --generate-only; preconditions/post-state trace must be qualified before acceptance runs')
        result.write_text(json.dumps(evidence, indent=2))
        return 2
    if not args.app or not args.data_archive:
        parser.error('--app and --data-archive are required unless --generate-only')
    if digest(args.data_archive) != DATA_SHA256:
        raise SystemExit('Public data archive checksum mismatch; refusing extraction')
    extractor = shutil.which('7zz') or shutil.which('7za') or shutil.which('7z')
    if not extractor:
        raise SystemExit('No 7zz/7za/7z available; no installation attempted')
    with (args.output / 'data-extraction.log').open('w') as output:
        subprocess.run([extractor, 'x', '-y', '-o' + str(data), str(args.data_archive.resolve())],
                       check=True, stdout=output, stderr=subprocess.STDOUT, timeout=min(60, max(1, args.budget_seconds - (time.monotonic() - started) - 5)))
    asset_spec = importlib.util.spec_from_file_location('adventure_asset_inventory', Path(__file__).with_name('asset_inventory.py'))
    asset_module = importlib.util.module_from_spec(asset_spec)
    asset_spec.loader.exec_module(asset_module)
    inventory = asset_module.inspect([data, args.app.resolve() / 'Contents/Resources'])
    (args.output / 'asset-inventory.json').write_text(json.dumps(inventory, indent=2))
    if not inventory['preflight_passed']:
        evidence.update(status='blocked_render_assets', missing_assets=inventory['missing'],
                        reason='Pinned public test data plus packaged resources lack required render sentinels; no SDL/client launch attempted')
        result.write_text(json.dumps(evidence, indent=2))
        return 2
    (data / 'config').mkdir(exist_ok=True)
    settings = {'video': {'driver': 'software', 'fullscreen': False, 'vsync': False,
                          'cursor': 'software', 'resolution': {'width': 800, 'height': 600, 'scaling': 100}},
                'server': {'seed': args.seed}, 'general': {'lastDifficulty': 3},
                'logging': {'console': {'threshold': 'debug', 'coloredOutputEnabled': False},
                            'loggers': [{'domain': 'global', 'level': 'trace'},
                                        {'domain': 'ai', 'level': 'debug'}]}}
    (data / 'config/settings.json').write_text(json.dumps(settings, indent=2))
    evidence['initial_settings_sha256'] = digest(data / 'config/settings.json')
    (args.output / 'initial-settings.json').write_text(json.dumps(settings, indent=2))
    client = args.app.resolve() / 'Contents/MacOS/vcmiclient'
    if not client.is_file():
        raise SystemExit('Packaged vcmiclient not found')
    ai = 'EmptyAI' if args.scenario == 'termination_smoke' else 'Nullkiller2'
    command = [str(client), '--spectate', '--spectate-ignore-hero', '--spectate-skip-battle',
               '--spectate-skip-battle-result', '--testmap', 'Maps/' + map_path.stem,
               '--ai', ai, '--ai', ai, '--nointro', '--disable-video', '--savefrequency', '1']
    # No offscreen backend is assumed: probe the actual packaged SDL first.
    probe_command = [sys.executable, str(Path(__file__).with_name('probe_sdl.py')),
                     '--app', str(args.app.resolve())]
    probe_run = subprocess.run(probe_command, capture_output=True, text=True,
                               timeout=min(20, max(1, args.budget_seconds - (time.monotonic() - started) - 5)))
    (args.output / 'sdl-probe.stdout.json').write_text(probe_run.stdout)
    (args.output / 'sdl-probe.stderr.log').write_text(probe_run.stderr)
    probe = json.loads(probe_run.stdout) if probe_run.stdout.strip() else {}
    evidence['sdl_probe'] = probe
    if probe.get('library'):
        evidence['sdl_library_sha256'] = digest(Path(probe['library']))
    if probe_run.returncode or not probe.get('supported'):
        evidence.update(status='incomplete', reason='Packaged SDL offscreen/software capability probe failed')
        result.write_text(json.dumps(evidence, indent=2))
        return 2
    env = dict(os.environ, HOME=str(home), SDL_AUDIODRIVER=probe['selected_audio'],
               SDL_VIDEODRIVER=probe['selected_video'])
    remaining = args.budget_seconds - (time.monotonic() - started) - 5
    if remaining <= 0:
        raise SystemExit('Qualification budget exhausted during setup; incomplete')
    code, timed_out, controlled, elapsed = supervise(command, env, args.output / 'console.log', home,
                                                    min(args.timeout, remaining), args.scenario)
    text = read_logs(home, args.output / 'console.log')
    evidence.update(classify(text, code, timed_out, args.scenario, controlled))
    # Capability probe success is not proof the actual game selected that renderer.
    if 'Created renderer software' not in text:
        evidence.update(status='incomplete', reason='Actual client software renderer not confirmed')
    evidence.update(engine_executed=True, returncode=code, timed_out=timed_out,
                    elapsed_seconds=elapsed, command=command, executable_sha256=digest(client),
                    saves=[str(p.relative_to(args.output)) for p in home.rglob('*.vsgm1')])
    result.write_text(json.dumps(evidence, indent=2))
    return 0 if evidence['status'].startswith('game_complete_') else 2


if __name__ == '__main__':
    raise SystemExit(main())
