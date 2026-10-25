#!/usr/bin/env python3
"""Summarise complete matrices, preserving failures and timing scope."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
from statistics import median

TITLES = {'mim': 'First engine: MIM only', 'astar': 'First engine: A* only',
          'mim_astar': 'First MIM+A*', 'mim_coastal': 'Second MIM+coastal A*',
          'coastal_astar': 'Coastal A* everywhere', 'quick2': 'Quick2',
          'atlas': 'Atlas', 'original': 'Quick', 'quick': 'Standard',
          'alternative': 'Alternative', 'main': 'Professional'}
CONTROLS = {'quick2', 'atlas', 'original', 'quick', 'alternative', 'main'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    directory = args.directory
    rows = [json.loads(line) for line in (directory / 'results.jsonl').read_text().splitlines()]
    cases = {case['name']: case for case in
             (json.loads(file.read_text()) for file in (directory / 'scenarios').glob('*.json'))}
    engines = list(dict.fromkeys(row['engine'] for row in rows))
    groups = defaultdict(list)
    for row in rows:
        groups[row['case'], row['engine']].append(row)
    if any((name, engine) not in groups for name in cases for engine in engines):
        raise RuntimeError('Incomplete matrix: do not claim missing runs are failures')
    positive = {name for name, case in cases.items() if not case.get('expected_negative')}
    real = {name for name in positive if 'grib' in cases[name]}
    summaries = []
    for engine in engines:
        cold = [r for r in rows if r['engine'] == engine and r['repeat'] == 0 and r['case'] in positive]
        summaries.append({'engine': engine, 'title': TITLES.get(engine, engine),
                          'valid': sum(r['valid'] for r in cold), 'cases': len(positive),
                          'real_valid': sum(r['valid'] and r['case'] in real for r in cold),
                          'real_cases': len(real), 'median_seconds': median(r['seconds'] for r in cold),
                          'negative_acceptances': sum(r['valid'] for r in rows if r['engine'] == engine and r['case'] not in positive),
                          'process_errors': sum('process_error' in r for r in rows if r['engine'] == engine)})
    second = 'mim_coastal'
    def passed(name, engine):
        return all(r['valid'] and 'process_error' not in r for r in groups[name, engine])
    paired = {}
    if second in engines and 'mim_astar' in engines:
        paired['second_only_vs_first'] = sorted(n for n in positive if passed(n, second) and not passed(n, 'mim_astar'))
        paired['first_only_vs_second'] = sorted(n for n in positive if passed(n, 'mim_astar') and not passed(n, second))
        shared = [n for n in positive if passed(n, second) and passed(n, 'mim_astar')]
        delta = {n: groups[n, second][0]['arrival_seconds'] - groups[n, 'mim_astar'][0]['arrival_seconds'] for n in shared}
        paired['shared_completions'] = len(shared)
        paired['second_earlier_by_over_one_second'] = sorted(n for n, value in delta.items() if value < -1)
        paired['second_later_by_over_one_second'] = sorted(n for n, value in delta.items() if value > 1)
    if second in engines and CONTROLS <= set(engines):
        paired['second_exclusive_vs_all_six_controls'] = sorted(
            n for n in positive if passed(n, second) and
            all(all(not r['valid'] and 'process_error' not in r for r in groups[n, e]) for e in CONTROLS))
    analysis = {'positive_cases': len(positive), 'negative_cases': len(cases) - len(positive),
                'trials': len(rows), 'engines': summaries, 'paired': paired}
    (directory / 'analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
    if real:
        lines = ['| Engine | Valid routes | Valid real-GRIB routes | Median calculation |',
                 '| --- | ---: | ---: | ---: |']
    else:
        lines = ['| Engine | Valid routes | Median calculation |', '| --- | ---: | ---: |']
    for summary in summaries:
        values = f"| {summary['title']} | {summary['valid']}/{summary['cases']} | "
        if real:
            values += f"{summary['real_valid']}/{summary['real_cases']} | "
        lines.append(values + f"{summary['median_seconds']:.3f} s |")
    lines += ['', 'One cold trial per case is used in this headline table. Failed solves remain in medians.',
              'Engine times exclude forecast preparation and the fresh external replay. Count only independently valid completions.',
              'A finite-budget failure is not proof that a route is impossible. See provenance.json for exact inputs, settings and binary hashes.', '']
    (directory / 'comparison.md').write_text('\n'.join(lines))
    print(json.dumps(analysis, indent=2))


if __name__ == '__main__':
    main()
