#!/usr/bin/env python3
"""Run the shared serial six-engine protocol with additional MIM/A* variants.

Example: run_comparison.py --output output/mim-astar/comparison --generated 32
         --engines original quick2 atlas quick alternative main mim astar mim_astar
         --timeout 10 --repeats 2 --cpu 4

--holdout perturbs generated cases with a separate fixed seed after tuning.
All other arguments and their meanings come from the existing benchmark client.
"""
import argparse
import hashlib
import importlib.util
import json
import random
import subprocess
import sys
from pathlib import Path

EXPERIMENT = Path(__file__).resolve().parents[1]
WORKSPACE = EXPERIMENT.parents[1]
DEFAULT_BASELINE = WORKSPACE if (WORKSPACE / 'vendor/weather-routing-engine').is_dir() else WORKSPACE / '.worktrees/xweather-1.34'


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def main():
    wrapper = argparse.ArgumentParser(add_help=False)
    wrapper.add_argument('--baseline', type=Path,
                         default=DEFAULT_BASELINE)
    wrapper.add_argument('--holdout', action='store_true')
    options, remaining = wrapper.parse_known_args()
    baseline = options.baseline.resolve()
    prototype_sources = {
        str(path.relative_to(WORKSPACE)): digest(path)
        for path in EXPERIMENT.rglob('*') if path.is_file()
        and path.suffix in {'.cpp', '.h', '.py', '.txt'}
    }
    git_revision = subprocess.run(['git', '-C', str(baseline), 'rev-parse', 'HEAD'],
                                  text=True, capture_output=True) if (baseline / '.git').exists() else None
    if git_revision is not None and git_revision.returncode == 0:
        baseline_commit = git_revision.stdout.strip()
        baseline_status = subprocess.check_output(
            ['git', '-C', str(baseline), 'status', '--short', '--untracked-files=no'], text=True)
    else:
        archive_baseline = json.loads((EXPERIMENT / 'baseline.json').read_text())
        baseline_commit = archive_baseline['baseline_commit']
        baseline_status = 'Source archive; verify baseline.json hashes'
    script = baseline / 'research/sixth/run_benchmark.py'
    spec = importlib.util.spec_from_file_location('existing_comparison', script)
    protocol = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(protocol)
    protocol.ENGINES += ['mim', 'astar', 'mim_astar', 'mim_astar_zero', 'astar_zero']
    if options.holdout:
        original_generate = protocol.generate

        def holdout_generate(count, shoreline):
            cases = original_generate(count, shoreline)
            generator = random.Random(20261023)
            for case in cases:
                if not case['name'].startswith('generated_'):
                    continue
                case['name'] = case['name'].replace('generated_', 'holdout_')
                case['wind_from'] = generator.uniform(0, 360)
                if case.get('oracle') == 'upwind_envelope':
                    case['wind_from'] = 0
                case['east_nm'] *= generator.uniform(0.75, 1.4)
                if case.get('north_nm'):
                    case['north_nm'] *= generator.uniform(0.75, 1.4)
                if case.get('tidal'):
                    case['tidal_phase'] = generator.uniform(-3.14, 3.14)
                if case.get('gate'):
                    case['gate_opens_hours'] = generator.uniform(1, 5)
            return [case for case in cases if case['name'].startswith('holdout_')]

        protocol.generate = holdout_generate
    if '--binary' not in remaining:
        remaining += ['--binary', str(WORKSPACE / 'output/mim-astar/build/mim_benchmark')]
    sys.argv = [str(Path(__file__)), *remaining]
    protocol.main()
    output = Path(remaining[remaining.index('--output') + 1])
    provenance_path = output / 'provenance.json'
    provenance = json.loads(provenance_path.read_text())
    provenance['prototype_sources'] = prototype_sources
    provenance['baseline_commit'] = baseline_commit
    provenance['baseline_tracked_status'] = baseline_status
    provenance['shared_protocol_sha256'] = digest(script)
    provenance['holdout_seed'] = 20261023 if options.holdout else None
    binary = Path(remaining[remaining.index('--binary') + 1])
    cache = binary.parent / 'CMakeCache.txt'
    if cache.exists():
        provenance['build_settings'] = [line for line in cache.read_text().splitlines()
            if line.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:', 'CMAKE_CXX_FLAGS_RELEASE:'))]
    commands = binary.parent / 'compile_commands.json'
    if commands.exists():
        provenance['compile_commands_sha256'] = digest(commands)
    provenance_path.write_text(json.dumps(provenance, indent=2) + '\n')


if __name__ == '__main__':
    main()
