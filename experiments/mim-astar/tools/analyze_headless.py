#!/usr/bin/env python3
"""Audit a complete seven-engine matrix for exclusive validated completions."""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path

from analyze_comparison import analyze, TITLES

EXISTING = ('original', 'quick2', 'atlas', 'quick', 'alternative', 'main')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    analysis, rows, scenarios = analyze(args.directory)
    scenarios = dict(sorted(scenarios.items()))
    expected = set(EXISTING) | {'mim_astar'}
    if {row['engine'] for row in rows} != expected:
        raise RuntimeError('Exclusive completion needs all six current controls')
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['case'], row['engine']].append(row)
    # A repeat disagreement is inconclusive; exclusive claims require every
    # prototype repetition to validate and every baseline repetition to fail.
    def passed(name, engine):
        return all(row['valid'] for row in grouped[name, engine])

    exclusive = {engine: [] for engine in expected}
    errors, all_failed, mixed, earlier, histogram = [], [], [], [], Counter()
    for name, scenario in scenarios.items():
        if scenario.get('expected_negative'):
            continue
        trials = [row for engine in expected for row in grouped[name, engine]]
        if any('process_error' in row for row in trials):
            errors.append(name)
            continue
        winners = [engine for engine in expected if passed(name, engine)]
        histogram[len(winners)] += 1
        if not any(row['valid'] for row in trials):
            all_failed.append(name)
        if len(winners) == 1:
            winner = winners[0]
            if all(not row['valid'] for row in trials if row['engine'] != winner):
                exclusive[winner].append(name)
        if passed(name, 'mim_astar') and any(not passed(name, engine) for engine in EXISTING):
            mixed.append(dict(case=name,
                successful_controls=[TITLES[e] for e in EXISTING if passed(name, e)],
                failed_controls=[TITLES[e] for e in EXISTING if not passed(name, e)]))
        controls = [row for row in trials if row['engine'] in EXISTING and row['valid']]
        if passed(name, 'mim_astar') and controls:
            prototype_arrival = max(row['arrival_seconds'] for row in grouped[name, 'mim_astar'])
            reference_arrival = min(row['arrival_seconds'] for row in controls)
            if prototype_arrival < reference_arrival - 1:
                earlier.append(dict(case=name, prototype_arrival_seconds=prototype_arrival,
                    best_existing_arrival_seconds=reference_arrival,
                    seconds_saved=reference_arrival - prototype_arrival))
    details = dict(exclusive_validated_completions=exclusive,
        prototype_completions_with_some_controls_failing=mixed,
        prototype_earlier_than_best_validated_current_control=earlier,
        all_engines_failed=sorted(all_failed), inconclusive_process_errors=errors,
        positive_cases_by_number_of_successful_engines=dict(sorted(histogram.items())),
        by_source={}, analysis_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        results_sha256=hashlib.sha256((args.directory / 'results.jsonl').read_bytes()).hexdigest())
    for source in sorted({case.get('headless_suite', 'other') for case in scenarios.values()}):
        selected = {name for name, case in scenarios.items()
                    if case.get('headless_suite', 'other') == source and not case.get('expected_negative')}
        details['by_source'][source] = dict(cases=len(selected),
            valid={TITLES[e]: sum(passed(name, e) for name in selected) for e in EXISTING + ('mim_astar',)})
    (args.directory / 'exclusive-analysis.json').write_text(json.dumps(details, indent=2) + '\n')
    (args.directory / 'analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
    table = ['| Engine | Valid intended positives | Valid stored GRIB cases | Median solver s | Exclusive completions |',
             '|---|---:|---:|---:|---:|']
    real = {name for name, case in scenarios.items()
            if 'grib' in case and not case.get('expected_negative')}
    for engine in analysis['engines']:
        identifier = engine['engine']
        cold = engine['runs']['0']
        table.append(f"| {engine['title']} | {cold['valid']}/{analysis['positive']} | "
                     f"{sum(passed(name, identifier) for name in real)}/{len(real)} | "
                     f"{cold['median_seconds']:.3f} | {len(exclusive[identifier])} |")
    table += ['', 'Each engine/case starts in a fresh process. Medians include failed solves. '
              'Exclusive completion requires a validated result and observed failure of all six '
              'other engines under this finite budget, with no process error. '
              'A failure does not prove that an engine can never solve that route.', '',
              f"Records: {len(rows)}; process errors: {len(analysis['process_errors'])}; "
              f"negative acceptances: {len(analysis['negative_acceptances'])}.", '']
    (args.directory / 'comparison.md').write_text('\n'.join(table))
    if args.plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        from matplotlib.colors import ListedColormap
        from matplotlib.patches import Patch
        names = [name for name, case in scenarios.items() if not case.get('expected_negative')]
        engines = EXISTING + ('mim_astar',)
        values = [[int(passed(name, engine)) for engine in engines] for name in names]
        figure, axis = plt.subplots(figsize=(11, len(names) * .24 + 2), layout='constrained')
        axis.imshow(values, aspect='auto', cmap=ListedColormap(['#e4e6e8', '#147d72']),
                    vmin=0, vmax=1)
        axis.set_xticks(range(len(engines)), [TITLES[e] for e in engines], rotation=25, ha='left')
        axis.xaxis.tick_top()
        axis.set_yticks(range(len(names)), [name[len('headless_'):] if name.startswith('headless_') else name for name in names], fontsize=8)
        axis.set_xticks([x - .5 for x in range(1, len(engines))], minor=True)
        axis.set_yticks([y - .5 for y in range(1, len(names))], minor=True)
        axis.grid(which='minor', color='white', linewidth=.6)
        for index in range(1, len(names)):
            if scenarios[names[index]].get('headless_suite') != scenarios[names[index - 1]].get('headless_suite'):
                axis.axhline(index - .5, color='#343a40', linewidth=1.4)
        axis.tick_params(which='minor', bottom=False, left=False)
        provenance = json.loads((args.directory / 'provenance.json').read_text())
        command = provenance['command']
        timeout = command[command.index('--timeout') + 1] if '--timeout' in command else '10'
        axis.set_title(f"{len(names)} additional intended-positive scenarios · {timeout}-second solve allowance\n"
                       "Common offline weather and shoreline providers; chart/depth app cases excluded", pad=50)
        axis.legend(handles=[Patch(color='#147d72', label='Validated completion'),
                             Patch(color='#e4e6e8', label='No validated completion within the tested limits')],
                    loc='lower center', bbox_to_anchor=(.5, -0.045), fontsize=9)
        figure.savefig(args.directory / 'route-matrix.png', dpi=160)
        figure.savefig(args.directory / 'route-matrix.svg')
        plt.close(figure)
    with (args.directory / 'route-matrix.csv').open('w') as output:
        writer = csv.writer(output)
        writer.writerow(['case', 'source', 'real_grib', 'expected_negative',
                         *[TITLES[e] for e in EXISTING + ('mim_astar',)]])
        for name, case in scenarios.items():
            writer.writerow([name, case.get('headless_suite'), 'grib' in case,
                case.get('expected_negative', False), *[
                    ' | '.join(f"{row.get('status', 'process_error')};valid={row['valid']};"
                               f"seconds={row['seconds']:.6f};eta_s={row['arrival_seconds']}"
                               for row in grouped[name, e]) for e in EXISTING + ('mim_astar',)]])
    print(json.dumps(details, indent=2))


if __name__ == '__main__':
    main()
