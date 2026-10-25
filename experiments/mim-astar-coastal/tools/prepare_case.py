#!/usr/bin/env python3
"""Resolve an offline case, normalize its polar and certify decoded currents.

This is the standalone JSON schema, not the OpenCPN app/Boat.xml schema. GRIB1
parameters 49/50 are exactly the current fields decoded by the benchmark reader.
Convex spatial/time interpolation cannot exceed their global component envelope.
"""
import argparse
import gzip
import shutil
import hashlib
import json
import math
from pathlib import Path
import subprocess
from shoreline_files import candidates, select


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.input = args.input.resolve()
    args.output = args.output.resolve()
    case = json.loads(args.input.read_text())
    if any(key in case for key in ('route', 'safety', 'environment', 'startTime')):
        parser.error('Use the standalone schema; app scenarios need their original providers')
    planned = [args.output, args.output.with_suffix('.pol'), args.output.with_suffix('.inputs.json'),
               args.output.with_suffix('.suite.json'), args.output.with_suffix('.shoreline.dat'),
               args.output.with_suffix('.guidance_shoreline.dat')]
    if any(path.exists() for path in planned):
        parser.error('Choose new output names to preserve earlier inputs')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    receipt = dict(source_case_sha256=digest(args.input), inputs={})
    for key in ('grib', 'polar', 'shoreline', 'guidance_shoreline'):
        if key not in case:
            continue
        if key in ('shoreline', 'guidance_shoreline') and case[key] == 'auto':
            case[key] = str(select(candidates(), coarse=key == 'guidance_shoreline'))
        path = Path(case[key]).expanduser()
        path = path if path.is_absolute() else args.input.parent / path
        path = path.resolve(strict=True)
        if key == 'polar' and path.suffix.lower() == '.xml':
            parser.error('Supply a TWA/TWS polar table; Boat.xml profiles are not decoded')
        case[key] = str(path)
        receipt['inputs'][key] = dict(path=str(path), sha256=digest(path))
        if key in ('shoreline', 'guidance_shoreline') and path.suffix == '.gz':
            decoded = args.output.with_suffix('.' + key + '.dat')
            with gzip.open(path, 'rb') as source, decoded.open('xb') as target:
                shutil.copyfileobj(source, target)
            case[key] = str(decoded)
            receipt['inputs'][key]['decoded_path'] = str(decoded)
            receipt['inputs'][key]['decoded_sha256'] = digest(decoded)
    polar_output = args.output.with_suffix('.pol')
    receipt_output = args.output.with_suffix('.inputs.json')
    suite_output = args.output.with_suffix('.suite.json')
    if any(path.exists() for path in (args.output, polar_output, receipt_output, suite_output)):
        parser.error('Choose new output names to preserve earlier inputs')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    normalized = None
    if 'polar' in case:
        original = Path(case['polar']).read_text()
        normalized = original.replace(';', ' ')
        assert original.replace(';', ' ').split() == normalized.split()
        case['polar'] = str(polar_output)
        receipt['normalized_polar_sha256'] = hashlib.sha256(normalized.encode('utf-8')).hexdigest()
    if case.get('currents') and 'grib' in case:
        components = []
        for parameter in (49, 50):
            result = subprocess.run(['grib_get', '-w', f'edition=1,indicatorOfParameter={parameter}',
                '-F', '%.17g', '-p', 'min:d,max:d', case['grib']],
                check=True, capture_output=True, text=True)
            values = [abs(float(value)) for value in result.stdout.split()]
            if not all(math.isfinite(value) for value in values):
                raise ValueError('Nonfinite current statistics')
            components.append(max(values, default=0.))
        bound = math.nextafter(math.hypot(*components) * 1.9438444924406, math.inf)
        case['mim_current_bound_knots'] = bound
        receipt['current_bound'] = dict(maximum_absolute_components_metres_per_second=components,
            maximum_current_knots=bound, method='Global envelope of all decoded GRIB1 49/50 frames')
    if normalized is not None:
        polar_output.write_text(normalized, encoding='utf-8')
    args.output.write_text(json.dumps(case, indent=2) + '\n')
    suite_output.write_text(json.dumps([case], indent=2) + '\n')
    receipt_output.write_text(json.dumps(receipt, indent=2) + '\n')
    print(args.output)
    print(suite_output)


if __name__ == '__main__':
    main()
