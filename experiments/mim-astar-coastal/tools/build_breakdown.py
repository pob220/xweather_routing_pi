#!/usr/bin/env python3
"""Build shareable case matrices without exporting forecasts, charts or profiles.

Import retained measurements with --data-root and --earlier-headless, or rebuild
the presentation from the public --dataset. Matplotlib is needed only for PNGs.
No routing engine, installed application or input dataset is modified.
"""
import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
from statistics import mean, median

ENGINES = ['quick2', 'mim_coastal', 'atlas', 'mim_astar', 'original', 'quick', 'alternative', 'main']
TITLES = dict(zip(ENGINES, ['Quick2', 'MIM+coastal A*', 'Atlas', 'First MIM+A*', 'Quick', 'Standard', 'Alternative', 'Professional']))
GROUPS = ['Coastal / harbour', 'Island passages', 'Ocean / offshore', 'Artificial obstacles', 'Open-water controls', 'Geographic stress', 'Expected rejection']


@lru_cache(maxsize=None)
def digest(filename):
    checksum = hashlib.sha256()
    with Path(filename).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def read_json(path):
    return json.loads(path.read_text())


def read_rows(directory):
    return [json.loads(line) for line in (directory / 'results.jsonl').read_text().splitlines()]


def normalized(case):
    data = dict(case)
    for key in ('grib', 'polar', 'shoreline', 'guidance_shoreline'):
        if key in data:
            data[key] = digest(str(Path(data[key]).resolve()))
    return data


def direct_distance(case):
    if 'end' not in case:
        return math.hypot(case.get('east_nm', 20), case.get('north_nm', 0))
    lat1, lon1 = map(math.radians, case['start'])
    lat2, lon2 = map(math.radians, case['end'])
    haversine = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2
    return 2 * 3440.065 * math.asin(min(1, math.sqrt(haversine)))


def metadata(case):
    name = case['name']
    distance = direct_distance(case)
    if case.get('expected_negative'):
        group = 'Expected rejection'
    elif 'dateline' in name or 'high_latitude' in name:
        group = 'Geographic stress'
    elif case.get('rectangles_nm') or case.get('polygons') or case.get('gate'):
        group = 'Artificial obstacles'
    elif name.startswith('tonga_') or 'tonga_offshore' in name or any(token in name for token in ('hanalei', 'hao-rangiroa', 'rangiroa-hao')):
        group = 'Island passages'
    elif any(token in name for token in ('long_ocean', 'atlantic_offshore', 'pacific_', 'alcatraz-kaneohe')):
        group = 'Ocean / offshore'
    elif 'shoreline' in case:
        group = 'Coastal / harbour'
    else:
        group = 'Open-water controls'
    if distance < 50:
        length = 'Short (<50 NM)'
    elif distance < 300:
        length = 'Regional (50–<300 NM)'
    else:
        length = 'Long (≥300 NM)'
    label = name
    for prefix in ('headless_lab_', 'headless_real_', 'tonga_'):
        if label.startswith(prefix):
            label = label[len(prefix):]
            break
    return {'id': name, 'label': label.replace('_', ' '), 'group': group, 'length': length,
            'direct_nm': round(distance, 3), 'forecast': 'Stored GRIB' if 'grib' in case else 'Synthetic weather',
            'geography': 'GSHHG shoreline' if 'shoreline' in case else 'Artificial geography' if group == 'Artificial obstacles' or case.get('polygons') else 'Open-water / constraint fixture',
            'expected_rejection': bool(case.get('expected_negative'))}


def safe_result(row, source):
    seconds = row['seconds']
    if not math.isfinite(seconds) or seconds < 0:
        raise ValueError('Missing or invalid observed calculation time')
    reason = row.get('coastal_diagnostics', {}).get('stop_reason') or row.get('mim_diagnostics', {}).get('stop_reason') or row.get('message', '')
    if '/home/' in reason or '/tmp/' in reason:
        reason = row.get('status', 'See local raw result')
    return {'valid': bool(row['valid']), 'seconds': seconds, 'arrival_seconds': row.get('arrival_seconds'),
            'status': row.get('status', ''), 'reason': reason[:240], 'process_error': 'process_error' in row,
            'source': source}


def verify_earlier_controls(root, earlier):
    old = read_json(earlier / 'provenance.json')
    current = read_json(root / 'qualified/headless/provenance.json')['second_engine']
    if old['sources'] != current['sources']:
        raise ValueError('Shared comparison sources differ; do not pool these runs')
    for provenance in (old, current):
        command = provenance['command']
        for argument, expected in [('--timeout', '20'), ('--repeats', '1'), ('--cpu', '4')]:
            if command[command.index(argument) + 1] != expected:
                raise ValueError('Earlier control protocol differs')
    if old['shared_protocol_sha256'] != current['shared_protocol_sha256']:
        raise ValueError('Shared runner differs')
    source_root = root / 'source'
    checks = {}
    for relative in ('tests/HeadlessFixtures.h', 'tools/generate_runner.py', 'tests/BenchmarkBounds.h'):
        key = 'experiments/mim-astar/' + relative
        current_file = source_root / key
        if digest(str(current_file)) == old['prototype_sources'][key]:
            checks[relative] = 'identical'
        else:
            before = earlier.parent / 'code-review/source-before' / relative
            if digest(str(before)) != old['prototype_sources'][key]:
                raise ValueError('No matching earlier adapter snapshot')
            def non_include_content(path):
                return '\n'.join(line for line in path.read_text().splitlines() if line.strip() and not line.startswith('#include'))
            if non_include_content(before) != non_include_content(current_file):
                raise ValueError('Request or provider adapter changed; do not pool')
            checks[relative] = 'include additions only; other content identical'
    previous = {p.name: normalized(read_json(p)) for p in (earlier / 'scenarios').glob('*.json')}
    latest = {p.name: normalized(read_json(p)) for p in (root / 'qualified/headless/scenarios').glob('*.json')}
    if previous != latest:
        raise ValueError('Earlier control scenarios or input content differ')
    return {'case_settings_and_input_content_match': len(latest), 'shared_source_hashes_match': len(old['sources']),
            'adapter_checks': checks, 'shared_protocol_matches': True,
            'earlier_controls_binary_sha256': old['binary_sha256'], 'requested_deadline_seconds': 20,
            'cold_trials_per_case': 1, 'logical_cpu': 4,
            'note': 'Only the six existing controls are reused. Earlier first-prototype measurements are excluded.'}


def import_dataset(root, earlier):
    reuse = verify_earlier_controls(root, earlier)
    controls = [row for row in read_rows(earlier) if row['engine'] in {'quick2', 'atlas', 'original', 'quick', 'alternative', 'main'}]
    campaigns = []
    for key, title, deadline in [('synthetic', 'Mixed development suite', 5), ('headless', 'Archived headless suite', 20), ('tonga', 'Tonga passages', 20)]:
        directory = root / 'qualified' / key
        rows = [row for row in read_rows(directory) if row['engine'] in ENGINES]
        if key == 'headless':
            rows.extend(controls)
        indexed = {}
        for row in rows:
            if row['repeat'] != 0:
                raise ValueError('This report expects the recorded single cold trial')
            identity = (row['case'], row['engine'])
            if identity in indexed:
                raise ValueError('Duplicate result; do not choose a favourable repetition')
            source = 'earlier-headless-controls' if key == 'headless' and row['engine'] in {'quick2', 'atlas', 'original', 'quick', 'alternative', 'main'} else 'qualified-' + key
            indexed[identity] = safe_result(row, source)
        cases = []
        for file in sorted((directory / 'scenarios').glob('*.json')):
            case = metadata(read_json(file))
            case['results'] = {engine: indexed.get((case['id'], engine)) for engine in ENGINES}
            if key != 'tonga' and any(value is None for value in case['results'].values()):
                raise ValueError('Incomplete eight-engine matrix')
            cases.append(case)
        cases.sort(key=lambda case: (GROUPS.index(case['group']), case['direct_nm'], case['id']))
        provenance = read_json(directory / 'provenance.json')
        campaigns.append({'id': key, 'title': title, 'deadline_seconds': deadline, 'cases': cases,
                          'timed_controls_binary_sha256': provenance['controls']['binary_sha256'],
                          'timed_second_binary_sha256': provenance['second_engine']['binary_sha256']})
    return {'schema': 1, 'date': '2026-10-25', 'engines': ENGINES, 'titles': TITLES,
            'classification': {'route_setting': 'Reviewed scenario intent, not a measured percentage of sailing near land. Rules are in build_breakdown.py.',
                               'length': 'Great-circle endpoint distance; short <50 NM, regional 50 to <300 NM, long >=300 NM. This is not sailed length.',
                               'forecast': 'Presence of an actual stored GRIB input, independently of case-name prefix.'},
            'earlier_control_reuse': reuse, 'campaigns': campaigns}


def average(values, operation):
    return operation(values) if values else None


def statistics(cases):
    positive = [case for case in cases if not case['expected_rejection']]
    summaries = []
    for engine in ENGINES:
        records = [case['results'][engine] for case in positive if case['results'][engine] is not None]
        successes = [r['seconds'] for r in records if r['valid'] and not r['process_error']]
        failures = [r['seconds'] for r in records if not r['valid'] or r['process_error']]
        summaries.append({'engine': engine, 'eligible': len(positive), 'run': len(records), 'successes': len(successes),
                          'success_median': average(successes, median), 'success_mean': average(successes, mean),
                          'failure_median': average(failures, median), 'failure_mean': average(failures, mean),
                          'all_median': average(successes + failures, median), 'all_mean': average(successes + failures, mean)})
    shared = [case for case in positive if all(case['results'][engine] is not None and case['results'][engine]['valid'] and not case['results'][engine]['process_error'] for engine in ENGINES)]
    common = {engine: {'median_seconds': average([case['results'][engine]['seconds'] for case in shared], median),
                       'mean_seconds': average([case['results'][engine]['seconds'] for case in shared], mean)} for engine in ENGINES}
    return {'positive_cases': len(positive), 'negative_cases': len(cases) - len(positive), 'engines': summaries,
            'common_success_cases_all_eight': [case['id'] for case in shared], 'common_success_timings': common}


def number(value):
    return '—' if value is None else '<0.001' if value < .001 else f'{value:.3f}'


def cell(result, negative=False):
    if result is None:
        return '—'
    state = '!' if result['process_error'] or negative and result['valid'] else 'R' if negative else 'S' if result['valid'] else 'F'
    return f'{state} {number(result["seconds"])}'


def create_markdown(dataset, output):
    text = ['# Per-case engine timings', '',
            'S = independently valid completion; F = no valid completion; R = expected rejection; ! = error or unexpected acceptance; — = not run. All times are seconds.', '',
            'Each campaign is a separate set of measurements. Eight engines were measured for every mixed/archived case. Only four were measured on Tonga. Do not pool the campaigns.', '',
            '**Professional deadline qualification:** these cells record final returns, not every valid route discovered along the way. A shared hard deadline can cancel Professional after its internal Quick has found a valid route. All 20 archived Quick-success/Professional-noncompletion differences are deadline cancellations. See [the diagnostic explanation](README.md#why-professional-can-appear-worse-than-its-internal-quick).', '',
            'One cold observation per cell; this is descriptive evidence, not statistical significance. Expected rejections are excluded from positive-route statistics. R records no valid completion on a rejection case; it does not by itself prove the reason was correct. A small failure time is not a fast solution.', '',
            'Route settings describe the test intent; length bands use direct endpoint distance, not sailed length or measured coastal exposure. The dataset intentionally has many Holyhead departure variants, so it is not a representative sample of worldwide passages.', '',
            'For colours and filtering, download [matrix.html](matrix.html) and open it in a browser. No server or network access is needed. [measurements.json](measurements.json) contains the sanitized observations; [statistics.json](statistics.json) contains the grouped calculations.', '']
    all_stats = {}
    for campaign in dataset['campaigns']:
        cases = campaign['cases']
        aggregate = statistics(cases)
        all_stats[campaign['id']] = {'overall': aggregate, 'groups': {}}
        text += [f'## {campaign["title"]}', '', f'Requested solver deadline: {campaign["deadline_seconds"]} s. Positive cases: {aggregate["positive_cases"]}; expected rejections: {aggregate["negative_cases"]}.', '',
                 '| Engine | Valid / run | Success median | Success mean | Failure median | Failure mean | All-attempt median |',
                 '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
        for summary in aggregate['engines']:
            text.append(f'| {TITLES[summary["engine"]]} | {summary["successes"]}/{summary["run"]} | ' + ' | '.join(number(summary[key]) for key in ('success_median', 'success_mean', 'failure_median', 'failure_mean', 'all_median')) + ' |')
        text += ['', 'Success-only timings can compare different sets of cases. The same-case comparison below is restricted to cases all eight engines solved.', '',
                 f'Common successes across all eight: {len(aggregate["common_success_cases_all_eight"])}.', '']
        if aggregate['common_success_cases_all_eight']:
            text += ['| Engine | Same-case median | Same-case mean |', '| --- | ---: | ---: |']
            for engine, values in aggregate['common_success_timings'].items():
                text.append(f'| {TITLES[engine]} | {number(values["median_seconds"])} | {number(values["mean_seconds"])} |')
            text.append('')
        text += [f'![{campaign["title"]}: status and timing matrix]({campaign["id"]}-matrix.png)', '']
        for group in GROUPS:
            subset = [case for case in cases if case['group'] == group]
            if not subset:
                continue
            text += [f'### {group}', '', '| Case | Direct NM | Weather | ' + ' | '.join(TITLES[e] for e in ENGINES) + ' |', '| --- | ---: | --- | ' + ' | '.join('---:' for _ in ENGINES) + ' |']
            for case in subset:
                text.append(f'| `{case["id"]}` | {case["direct_nm"]:.1f} | {case["forecast"]} | ' + ' | '.join(cell(case['results'][e], case['expected_rejection']) for e in ENGINES) + ' |')
            text.append('')
        for dimension in ('group', 'length', 'forecast'):
            values = list(dict.fromkeys(case[dimension] for case in cases if not case['expected_rejection']))
            all_stats[campaign['id']]['groups'][dimension] = {value: statistics([case for case in cases if case[dimension] == value]) for value in values}
            text += [f'### Positive-route breakdown by {dimension}', '',
                     '| Group | Engine | Valid / run | Success median | Success mean | Failure median | Failure mean |',
                     '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
            for value in values:
                for summary in all_stats[campaign['id']]['groups'][dimension][value]['engines']:
                    text.append(f'| {value} | {TITLES[summary["engine"]]} | {summary["successes"]}/{summary["run"]} | ' + ' | '.join(number(summary[key]) for key in ('success_median', 'success_mean', 'failure_median', 'failure_mean')) + ' |')
            text.append('')
    text += ['## Measurement provenance', '',
             'The mixed and Tonga control observations, and both prototype observations in the archived suite, are the qualified second-experiment records. Six archived control columns reuse the earlier 20-second, CPU-4, single-cold-trial campaign after matching all 65 scenario settings and input-content hashes, all 51 shared source hashes, the common protocol, and the request/provider adapters. One adapter differs only by added include directives. Earlier first-prototype measurements are not reused.', '',
             'This extends the second experiment’s report with existing control measurements; it does not rerun or revise those engines. A different executable hash is recorded for each source campaign. The charts and forecasts themselves are not distributed here.', '',
             'The standalone clients use GSHHG and their supported weather adapters, not OpenCPN chart/depth services. These figures cannot measure OpenCPN UI overhead or establish chart-aware passage safety.', '']
    (output / 'MATRIX.md').write_text('\n'.join(text))
    (output / 'statistics.json').write_text(json.dumps(all_stats, indent=2, allow_nan=False) + '\n')


def create_plots(dataset, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    import numpy as np
    colours = {'S': '#d7efdc', 'F': '#f8d8d6', 'R': '#dbeaf7', '!': '#ffd08a', '—': '#ececec'}
    for campaign in dataset['campaigns']:
        cases = campaign['cases']
        figure, axis = plt.subplots(figsize=(18, max(5, .26 * len(cases) + 2.3)))
        pixels = np.zeros((len(cases), len(ENGINES), 4))
        for row, case in enumerate(cases):
            for column, engine in enumerate(ENGINES):
                label = cell(case['results'][engine], case['expected_rejection'])
                state = label.split()[0]
                pixels[row, column] = matplotlib.colors.to_rgba(colours[state])
                axis.text(column, row, label, ha='center', va='center', fontsize=9, color='#16212a')
        axis.imshow(pixels, aspect='auto', interpolation='nearest')
        axis.set_xticks(range(len(ENGINES)), [TITLES[e].replace('MIM+coastal A*', 'MIM+coastal\nA*').replace('First MIM+A*', 'First\nMIM+A*') for e in ENGINES], fontsize=10)
        axis.xaxis.tick_top()
        labels = [f'{case["label"]} ({case["direct_nm"]:.0f} NM)' for case in cases]
        axis.set_yticks(range(len(cases)), labels, fontsize=9)
        axis.tick_params(length=0, pad=7)
        for index in range(1, len(cases)):
            if cases[index]['group'] != cases[index-1]['group']:
                axis.axhline(index - .5, color='#ffffff', linewidth=5)
        axis.set_title(campaign['title'] + f' — requested deadline {campaign["deadline_seconds"]} s; one cold trial', fontsize=14, pad=36)
        axis.set_xticks(np.arange(-.5, len(ENGINES), 1), minor=True)
        axis.set_yticks(np.arange(-.5, len(cases), 1), minor=True)
        axis.grid(which='minor', color='white', linewidth=.6)
        axis.tick_params(which='minor', length=0)
        for spine in axis.spines.values():
            spine.set_visible(False)
        figure.legend(handles=[Patch(facecolor=colours[state], label=label) for state,label in [('S','S: valid'),('F','F: failed'),('R','R: expected rejection'),('—','—: not run')]], loc='lower center', ncol=4, frameon=False, fontsize=11)
        figure.subplots_adjust(left=.32, right=.99, top=1 - 1.1 / figure.get_figheight(), bottom=.65 / figure.get_figheight())
        figure.savefig(output / (campaign['id'] + '-matrix.png'), dpi=125)
        plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path)
    parser.add_argument('--earlier-headless', type=Path)
    parser.add_argument('--dataset', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--no-plots', action='store_true')
    args = parser.parse_args()
    if args.dataset:
        dataset = read_json(args.dataset)
    else:
        if not args.data_root or not args.earlier_headless:
            parser.error('Supply --dataset, or both local measurement directories')
        dataset = import_dataset(args.data_root.resolve(), args.earlier_headless.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'measurements.json').write_text(json.dumps(dataset, indent=2, allow_nan=False) + '\n')
    create_markdown(dataset, args.output)
    template = Path(__file__).with_name('matrix-template.html').read_text()
    encoded = json.dumps(dataset, separators=(',', ':'), allow_nan=False).replace('</', '<\\/')
    (args.output / 'matrix.html').write_text(template.replace('/* MEASUREMENT_DATA */', encoded))
    if not args.no_plots:
        create_plots(dataset, args.output)
    print(json.dumps({'cases': sum(len(c['cases']) for c in dataset['campaigns']),
                      'measurements': sum(r is not None for c in dataset['campaigns'] for case in c['cases'] for r in case['results'].values()),
                      'output': str(args.output)}))


if __name__ == '__main__':
    main()
