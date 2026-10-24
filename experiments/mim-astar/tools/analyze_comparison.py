#!/usr/bin/env python3
"""Validate a complete matrix, summarize failures and compare validated arrivals."""
import argparse
import collections
import hashlib
import json
import math
import statistics
from pathlib import Path

TITLES = {'original': 'Quick', 'quick2': 'Quick2', 'atlas': 'Atlas', 'quick': 'Standard',
          'alternative': 'Alternative', 'main': 'Professional', 'mim': 'MIM only',
          'astar': 'A* only', 'mim_astar': 'MIM + A*',
          'mim_astar_zero': 'MIM + Dijkstra', 'astar_zero': 'Dijkstra only'}


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    index = (len(values) - 1) * fraction
    lower = math.floor(index)
    upper = math.ceil(index)
    return values[lower] + (values[upper] - values[lower]) * (index - lower)


def median(values):
    return statistics.median(values) if values else None


def analyze(root, references=()):
    rows = [json.loads(line) for line in (root / 'results.jsonl').read_text().splitlines()]
    scenarios = {path.stem: json.loads(path.read_text()) for path in (root / 'scenarios').glob('*.json')}
    engines = list(dict.fromkeys(row['engine'] for row in rows))
    repeats = sorted(set(row['repeat'] for row in rows))
    counts = collections.Counter((row['case'], row['engine'], row['repeat']) for row in rows)
    expected = {(name, engine, repeat) for name in scenarios for engine in engines for repeat in repeats}
    missing = expected - counts.keys()
    duplicates = [key for key, count in counts.items() if count != 1]
    errors = [dict(case=row['case'], engine=row['engine'], error=row['process_error'])
              for row in rows if 'process_error' in row]
    if missing or duplicates:
        raise RuntimeError(f'Matrix incomplete: missing={sorted(missing)} duplicates={duplicates}')
    positive = {name for name, case in scenarios.items() if not case.get('expected_negative')}
    negative = set(scenarios) - positive
    best = {}
    for row in rows:
        if row['valid']:
            if not row['complete'] or not row['arrival_seconds'] or row.get('independent_failure'):
                raise RuntimeError(f'Inconsistent valid result: {row["case"]} {row["engine"]}')
            best[row['case']] = min(best.get(row['case'], math.inf), row['arrival_seconds'])
    reference_receipts = []
    for reference in references:
        accepted = 0
        for line in (reference / 'results.jsonl').read_text().splitlines():
            row = json.loads(line)
            if not row['valid'] or row['case'] not in scenarios:
                continue
            previous = json.loads((reference / 'scenarios' / f'{row["case"]}.json').read_text())
            if previous != scenarios[row['case']]:
                raise RuntimeError(f'Reference physical scenario differs: {row["case"]}')
            best[row['case']] = min(best.get(row['case'], math.inf), row['arrival_seconds'])
            accepted += 1
        reference_receipts.append(dict(directory=str(reference), accepted=accepted,
            records_sha256=hashlib.sha256((reference / 'results.jsonl').read_bytes()).hexdigest()))
    output = dict(cases=len(scenarios), positive=len(positive), negative=len(negative),
                  records=len(rows), process_errors=errors, engines=[], pairs={},
                  best_known_arrival_seconds=best, negative_acceptances=[], determinism={})
    output['reference_suites'] = reference_receipts
    for engine in engines:
        runs = {}
        for repeat in repeats:
            current = [row for row in rows if row['engine'] == engine and row['repeat'] == repeat]
            trials = [row for row in current if row['case'] in positive]
            valid = [row for row in trials if row['valid']]
            ratios = [row['arrival_seconds'] / best[row['case']] for row in valid]
            negatives = [row['case'] for row in current if row['case'] in negative and row['valid']]
            output['negative_acceptances'].extend(dict(engine=engine, repeat=repeat, case=name)
                                                    for name in negatives)
            real = [row for row in trials if row['case'].startswith('real_')]
            runs[str(repeat)] = dict(cases=len(trials), valid=len(valid),
                median_seconds=median([row['seconds'] for row in trials]),
                p95_seconds=percentile([row['seconds'] for row in trials], 0.95),
                total_seconds=sum(row['seconds'] for row in trials),
                median_success_seconds=median([row['seconds'] for row in valid]),
                p95_arrival_ratio=percentile(ratios, 0.95),
                median_arrival_ratio=median(ratios), worst_arrival_ratio=max(ratios, default=None),
                within_one_percent=sum(row['arrival_seconds'] <= 1.01 * best[row['case']] for row in valid),
                median_rss_mib=median([row['peak_rss_kib'] / 1024 for row in trials]),
                median_weather_queries=median([row.get('weather_queries', 0) for row in trials]),
                median_geometry_queries=median([row.get('land_queries', 0) for row in trials]),
                real_cases=len(real), real_valid=sum(row['valid'] for row in real),
                failures=[dict(case=row['case'], status=row.get('status', 'process_error'))
                          for row in trials if not row['valid']],
                first_incumbent_but_not_delivered=[row['case'] for row in trials if
                    not row['valid'] and row.get('mim_diagnostics', {}).get('first_incumbent_seconds', 0) > 0])
        output['engines'].append(dict(engine=engine, title=TITLES[engine], runs=runs))
        if len(repeats) == 2:
            engine_rows = {(row['case'], row['repeat']): row for row in rows if row['engine'] == engine}
            differences = []
            for name in scenarios:
                cold, warm = engine_rows[(name, repeats[0])], engine_rows[(name, repeats[1])]
                if (cold['valid'], cold['arrival_seconds'], cold.get('track')) != (
                        warm['valid'], warm['arrival_seconds'], warm.get('track')):
                    differences.append(name)
            output['determinism'][engine] = dict(identical=len(scenarios) - len(differences),
                                                  differences=differences)
    indexed = {(row['case'], row['engine'], row['repeat']): row for row in rows}
    for other in engines:
        if other == 'mim_astar' or 'mim_astar' not in engines:
            continue
        common = [(indexed[(name, other, 0)], indexed[(name, 'mim_astar', 0)])
                  for name in sorted(positive) if indexed[(name, other, 0)]['valid']
                  and indexed[(name, 'mim_astar', 0)]['valid']]
        output['pairs'][other] = dict(common_valid=len(common),
            hybrid_earlier=sum(new['arrival_seconds'] < old['arrival_seconds'] - 1 for old, new in common),
            hybrid_later=sum(new['arrival_seconds'] > old['arrival_seconds'] + 1 for old, new in common),
            tied_within_one_second=sum(abs(new['arrival_seconds'] - old['arrival_seconds']) <= 1 for old, new in common),
            median_hybrid_over_other_calculation=median([new['seconds'] / max(old['seconds'], 1e-9)
                                                       for old, new in common]),
            earlier_cases=[dict(case=old['case'], other_seconds=old['arrival_seconds'],
                                 hybrid_seconds=new['arrival_seconds'])
                           for old, new in common if new['arrival_seconds'] < old['arrival_seconds'] - 1])
    output['analysis_script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    return output, rows, scenarios


def write_table(output, path):
    table = ['| Engine | Valid cold / warm | Median cold s | Median warm s | ETA excess p95 | Median RSS MiB |',
             '|---|---:|---:|---:|---:|---:|']
    for engine in output['engines']:
        cold = engine['runs']['0']
        warm = engine['runs'].get('1', cold)
        excess = 100 * (cold['p95_arrival_ratio'] - 1) if cold['p95_arrival_ratio'] else None
        quality = f'{excess:.2f}%' if excess is not None else '—'
        table.append(f"| {engine['title']} | {cold['valid']} / {warm['valid']} | "
                     f"{cold['median_seconds']:.4f} | {warm['median_seconds']:.4f} | "
                     f"{quality} | {cold['median_rss_mib']:.1f} |")
    table.extend(['', f"Denominator: {output['positive']} positive scenarios per engine/repeat. "
        'Calculation medians include failed solves. ETA p95 is conditional on successful '
        'routes and compares against the best validated arrival in this matrix and any explicitly '
        'recorded reference runs, not a proven optimum. '
        'RSS includes providers and the external audit.', '',
        f"Records: {output['records']}; process errors: {len(output['process_errors'])}; "
        f"negative acceptances: {len(output['negative_acceptances'])}.", ''])
    path.write_text('\n'.join(table))


def plot(output, rows, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    figure, axes = plt.subplots(1, 3, figsize=(15, 5), layout='constrained')
    titles = [engine['title'] for engine in output['engines']]
    colors = plt.get_cmap('tab10').colors
    for index, engine in enumerate(output['engines']):
        cold = engine['runs']['0']
        color = colors[index % len(colors)]
        axes[0].barh(index, cold['valid'], color=color)
        axes[1].barh(index, cold['median_seconds'], color=color)
        positive_trials = [row for row in rows if row['engine'] == engine['engine']
                           and row['repeat'] == 0 and not row['expected_negative']]
        excesses = sorted(100 * (row['arrival_seconds'] /
            output['best_known_arrival_seconds'][row['case']] - 1)
            for row in positive_trials if row['valid'])
        x = [0] + [max(0, value) for value in excesses]
        y = [0] + [100 * (count + 1) / output['positive'] for count in range(len(excesses))]
        x.append(max(25, x[-1]))
        y.append(y[-1])
        axes[2].step(x, y, where='post', color=color, label=engine['title'])
    axes[0].set_yticks(range(len(titles)), titles)
    axes[0].invert_yaxis()
    axes[0].set_xlim(0, output['positive'])
    axes[0].set_xlabel('Validated completions')
    axes[0].set_title('Coverage under the same solve deadline')
    axes[1].set_yticks(range(len(titles)), titles)
    axes[1].invert_yaxis()
    axes[1].set_xlabel('Seconds, including failed solves')
    axes[1].set_title('Median cold solver time')
    axes[2].set_xlim(0, 25)
    axes[2].set_ylim(0, 102)
    axes[2].set_xlabel('Extra passage time vs validated reference (%)')
    axes[2].set_ylabel('All positive scenarios (%)')
    axes[2].set_title('Quality and failures together')
    axes[2].legend(fontsize=8, loc='lower right')
    for axis in axes:
        axis.grid(axis='x', alpha=0.2)
        axis.set_axisbelow(True)
    figure.suptitle(f"{output['positive']} intended-positive scenarios, cold runs", fontsize=14)
    figure.savefig(path.with_suffix('.png'), dpi=160)
    figure.savefig(path.with_suffix('.svg'))
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--plot', action='store_true')
    parser.add_argument('--reference', action='append', type=Path, default=[])
    args = parser.parse_args()
    output, rows, scenarios = analyze(args.directory, args.reference)
    (args.directory / 'analysis.json').write_text(json.dumps(output, indent=2) + '\n')
    write_table(output, args.directory / 'comparison.md')
    if args.plot:
        plot(output, rows, args.directory / 'comparison')
    print((args.directory / 'comparison.md').read_text())


if __name__ == '__main__':
    main()
