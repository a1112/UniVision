#!/usr/bin/env python3
"""Validate local design examples only. No network, SDK, devices, or arbitrary graph execution.

Requires Python 3.10+ and jsonschema 4.x (with referencing). Run from any working directory.
This script is not a production parser, a camera acceptance test, or a metrology certification.
"""
from __future__ import annotations
import argparse
import copy
import csv
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
from typing import Any

try:
    from jsonschema import Draft202012Validator, ValidationError
    from referencing import Registry, Resource
except ImportError as exc:
    raise SystemExit('Missing jsonschema 4.x and referencing. Use a prepared Python environment; this script does not install dependencies.') from exc

BASE = Path(__file__).resolve().parents[1]
EXAMPLES = BASE / 'examples'
MAX_FILE_BYTES = 8 * 1024 * 1024
KINDS = ['artifact-ref', 'frame-envelope', 'recording-manifest', 'calibration-profile',
         'measurement-result', 'flow-definition', 'run-manifest', 'stream-receipt']


def load(path: Path) -> dict[str, Any]:
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError(f'Oversize design file: {path.name}')
    value = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise ValueError('Root must be an object')
    return value


def safe_path(root: Path, name: str) -> Path:
    if not name or '\\' in name or ':' in name or name.startswith('/'):
        raise ValueError('Unsupported or absolute artifact path')
    parts = PurePosixPath(name).parts
    if '..' in parts or not parts:
        raise ValueError('Path traversal')
    resolved_root = root.resolve()
    candidate = (root / name).resolve()
    if not candidate.is_relative_to(resolved_root):
        raise ValueError('Artifact escaped the example root')
    # Deny symlinks in the example contract, even when their targets remain local.
    current = root
    for part in parts:
        current = current / part
        if current.is_symlink():
            raise ValueError('Symlink artifact is not allowed')
    return candidate


def check_artifact(a: dict[str, Any], root: Path) -> None:
    path = safe_path(root, a['path'])
    if not path.is_file():
        raise ValueError('Missing artifact: ' + a['path'])
    size = path.stat().st_size
    if size > MAX_FILE_BYTES:
        raise ValueError('Design example file exceeds verifier budget')
    if int(a['bytes']) != size:
        raise ValueError('Artifact byte count mismatch')
    if hashlib.sha256(path.read_bytes()).hexdigest() != a['sha256']:
        raise ValueError('Artifact digest mismatch')


def walk(value: Any, root: Path) -> None:
    if isinstance(value, dict):
        if {'artifact_id', 'path', 'bytes', 'sha256', 'media_type'}.issubset(value):
            check_artifact(value, root)
        for key, val in value.items():
            if key in {'sequence', 'bytes', 'capture_ticks', 'tick_frequency_hz',
                       'capture_utc_ns', 'first_line_index', 'frame_count', 'decoded_bytes',
                       'max_owned_bytes', 'first', 'last', 'offset', 'byte_length'} and val is not None:
                if not isinstance(val, str) or not (0 <= int(val) <= 2**64 - 1):
                    raise ValueError('uint64 must be a bounded decimal string: ' + key)
            if isinstance(val, float) and not math.isfinite(val):
                raise ValueError('Non-finite numbers are forbidden')
            walk(val, root)
    elif isinstance(value, list):
        for val in value:
            walk(val, root)


# This is a design-test registry, not a node execution runtime.
NODE_TYPES = {
    'source.profile_file': (None, 'MetricProfile', {'artifact_path'}),
    'measure.circle_fit': ('MetricProfile', 'MeasurementResult', {'calibration_id'}),
    'judge.verification': ('MeasurementResult', 'Decision', {'error_limit_mm'}),
    'sink.result_archive': ('Decision', None, {'output_namespace'}),
}


def check_graph(d: dict[str, Any], root: Path) -> None:
    nodes = d['nodes']
    ids = [n['id'] for n in nodes]
    if len(set(ids)) != len(ids):
        raise ValueError('Duplicate node ID')
    by_id = {n['id']: n for n in nodes}
    incoming = {i: 0 for i in ids}
    children = {i: [] for i in ids}
    for n in nodes:
        if n['type'] not in NODE_TYPES or n['version'] != '0.1.0':
            raise ValueError('Unknown or unapproved offline node/version')
        allowed = NODE_TYPES[n['type']][2]
        if set(n['parameters']) != allowed:
            raise ValueError('Node parameters do not match registry')
        if n['type'] == 'source.profile_file':
            if not safe_path(root, n['parameters']['artifact_path']).is_file():
                raise ValueError('Missing source fixture')
        if n['type'] == 'judge.verification':
            v = n['parameters']['error_limit_mm']
            if isinstance(v, bool) or not isinstance(v, (float, int)) or not 0 < v <= 1:
                raise ValueError('Invalid example error limit')
        if n['type'] == 'sink.result_archive':
            safe_path(root, n['parameters']['output_namespace'])
    seen_edges = set()
    for e in d['edges']:
        a, b = e['from'], e['to']
        if a not in by_id or b not in by_id:
            raise ValueError('Dangling edge')
        edge_key = (a, e['from_port'], b, e['to_port'])
        if edge_key in seen_edges:
            raise ValueError('Duplicate edge')
        seen_edges.add(edge_key)
        out_t = NODE_TYPES[by_id[a]['type']][1]
        in_t = NODE_TYPES[by_id[b]['type']][0]
        if out_t is None or in_t is None or out_t != in_t:
            raise ValueError('Port type mismatch')
        incoming[b] += 1
        children[a].append(b)
    for i in ids:
        expected_input = NODE_TYPES[by_id[i]['type']][0]
        if expected_input is not None and incoming[i] != 1:
            raise ValueError('Required input missing or multiply connected')
    queue = [i for i in ids if incoming[i] == 0]
    if not queue:
        raise ValueError('Graph has no source')
    reached = []
    degrees = dict(incoming)
    while queue:
        i = queue.pop(0)
        reached.append(i)
        for child in children[i]:
            degrees[child] -= 1
            if degrees[child] == 0:
                queue.append(child)
    if len(reached) != len(ids):
        raise ValueError('Cycle detected')
    for sink in d['required_sinks']:
        if sink not in by_id or by_id[sink]['type'] != 'sink.result_archive':
            raise ValueError('Invalid required sink')


def semantic(kind: str, d: dict[str, Any], root: Path) -> None:
    walk(d, root)
    if kind == 'frame-envelope':
        layout = d['layout']
        width, height = layout['width'], layout['height']
        bpp = {'Mono8': 1, 'Mono16': 2, 'Depth16': 2, 'Depth32F': 4}[layout['pixel_format']]
        if int(d['decoded_bytes']) > 256 * 1024 * 1024:
            raise ValueError('Decoded frame exceeds hard budget')
        # Only a single tightly interpretable plane is implemented in this design checker.
        if len(layout['planes']) != 1:
            raise ValueError('Example checker supports one plane; production must validate all planes')
        plane = layout['planes'][0]
        if int(plane['offset']) != 0 or plane['row_stride'] < width * bpp:
            raise ValueError('Invalid plane offset or stride')
        expected = plane['row_stride'] * height
        if int(plane['byte_length']) != expected or int(d['decoded_bytes']) != expected:
            raise ValueError('Invalid decoded layout')
        if d['encoding'] == 'none' and int(d['payload']['bytes']) != expected:
            raise ValueError('Raw payload does not match layout')
        if d['frame_kind'] == 'depth_raw' and d['unit'] != 'raw-device-code':
            raise ValueError('Raw depth cannot claim metric units')
        if d['frame_kind'] == 'depth_metric' and (d['unit'] not in {'mm', 'm'} or not d['calibration_id']):
            raise ValueError('Metric depth requires calibration')
        if d['frame_kind'].startswith('depth') and d['encoding'] == 'jpeg':
            raise ValueError('JPEG is not an approved depth representation')
        if d['capture_time']['quality'] == 'mapped-verified' and (d['capture_time']['capture_utc_ns'] is None or not d['capture_time']['mapping_id']):
            raise ValueError('Verified mapping requires evidence reference')
        if int(d['capture_time']['tick_frequency_hz']) == 0:
            raise ValueError('Zero clock frequency')
        if d['line_range'] and d['line_range']['line_count'] != height:
            raise ValueError('Line count mismatch')
        if d['source_kind'] == 'synthetic' and d['validity'] != 'synthetic':
            raise ValueError('Synthetic frame must be identified')
    elif kind == 'recording-manifest':
        if d['container_format'] == 'synthetic-fixture-v1' and d['provenance'] != 'synthetic':
            raise ValueError('Fixture cannot masquerade as real recording')
        for gap in d['gap_ranges']:
            if int(gap['first']) > int(gap['last']):
                raise ValueError('Invalid gap interval')
    elif kind == 'calibration-profile':
        if d['scale'] <= 0:
            raise ValueError('Calibration scale must be positive')
        if d['status'] == 'approved' and (d['provenance'] == 'synthetic' or not d['evidence_refs'] or not d['approval_record']):
            raise ValueError('Approval requires non-synthetic supporting evidence')
    elif kind == 'measurement-result':
        if d['validity'] in {'synthetic', 'invalid', 'uncertainty-not-evaluated'} and d['decision'] in {'pass', 'fail'}:
            raise ValueError('Unverified or synthetic result cannot claim physical pass/fail')
        if d['validity'] == 'invalid' and d['value'] is not None:
            raise ValueError('Invalid result must not reuse a value')
        if d['decision'] in {'pass', 'fail', 'indeterminate'} and (d['uncertainty'] is None or not d['decision_rule_version']):
            raise ValueError('Decision requires uncertainty and rule version')
        if d['uncertainty']:
            u = d['uncertainty']
            if u['coverage_factor'] <= 0 or u['unit'] != d['unit']:
                raise ValueError('Invalid uncertainty factor or units')
            if not math.isclose(u['expanded'], u['standard'] * u['coverage_factor'], rel_tol=1e-9, abs_tol=1e-12):
                raise ValueError('Expanded uncertainty is inconsistent')
    elif kind == 'flow-definition':
        check_graph(d, root)
    elif kind == 'run-manifest':
        if d['status'] == 'succeeded' and not d['outputs']:
            raise ValueError('Successful run must reference committed outputs')
    elif kind == 'stream-receipt':
        if d['level'] in {'durable', 'processed'} and d['commit_record'] is None:
            raise ValueError('Durable receipt needs a commit record')
        if d['level'] == 'processed' and not d['processing_run_id']:
            raise ValueError('Processed receipt needs a consumer run')


def solve3(matrix: list[list[float]], rhs: list[float]) -> list[float]:
    a = [row[:] + [rhs[i]] for i, row in enumerate(matrix)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda r: abs(a[r][col]))
        a[col], a[pivot] = a[pivot], a[col]
        if abs(a[col][col]) < 1e-12:
            raise ValueError('Degenerate synthetic geometry')
        divisor = a[col][col]
        a[col] = [v / divisor for v in a[col]]
        for row in range(3):
            if row == col:
                continue
            factor = a[row][col]
            a[row] = [v - factor * q for v, q in zip(a[row], a[col])]
    return [a[i][3] for i in range(3)]


def geometry_probe(root: Path) -> dict[str, Any]:
    with (root / 'data/profile-circle.csv').open(encoding='utf-8', newline='') as f:
        points = [(float(row['x_mm']), float(row['z_mm'])) for row in csv.DictReader(f)]
    m = [[0.0] * 3 for _ in range(3)]
    rhs = [0.0] * 3
    for x, z in points:
        v = [x, z, 1.0]
        y = -(x*x + z*z)
        for i in range(3):
            rhs[i] += v[i] * y
            for j in range(3):
                m[i][j] += v[i] * v[j]
    a, b, cc = solve3(m, rhs)
    radius_sq = a*a/4 + b*b/4 - cc
    if radius_sq <= 0:
        raise ValueError('Invalid synthetic fitted radius')
    diameter = 2 * math.sqrt(radius_sq)
    error = abs(diameter - 20.0)
    if error > 1e-8:
        raise ValueError('Synthetic algebraic-circle consistency probe failed')
    return {'test': 'synthetic_algebraic_geometry_consistency', 'points': len(points),
            'diameter_mm': diameter, 'absolute_error_mm': error, 'tolerance_mm': 1e-8,
            'status': 'passed', 'physical_accuracy_verified': False,
            'scope': 'Checks fixture and a small algebraic probe only, not the proposed production geometric fitter.'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, help='Optional local JSON report output')
    args = parser.parse_args()
    schemas = {k: load(BASE / 'contracts' / f'{k}.schema.json') for k in KINDS}
    registry = Registry().with_resources((s['$id'], Resource.from_contents(s)) for s in schemas.values())
    validators = {}
    for kind, schema in schemas.items():
        Draft202012Validator.check_schema(schema)
        validators[kind] = Draft202012Validator(schema, registry=registry)
    examples = {kind: load(EXAMPLES / f'{kind}.json') for kind in KINDS}
    def validate(kind: str, d: dict[str, Any]) -> None:
        validators[kind].validate(d)
        semantic(kind, d, EXAMPLES)
    positive = []
    for kind, d in examples.items():
        validate(kind, d)
        positive.append({'schema': kind, 'status': 'passed'})
    tests: list[tuple[str, str, dict[str, Any]]] = []
    def negative(name: str, kind: str, change) -> None:
        d = copy.deepcopy(examples[kind]); change(d); tests.append((name, kind, d))
    negative('numeric-sequence-rejected', 'frame-envelope', lambda d: d.update(sequence=1))
    negative('uint64-overflow-rejected', 'frame-envelope', lambda d: d.update(sequence=str(2**64)))
    negative('unknown-schema-version', 'frame-envelope', lambda d: d.update(schema_version='9.0'))
    negative('invalid-row-stride', 'frame-envelope', lambda d: d['layout']['planes'][0].update(row_stride=1))
    negative('raw-depth-cannot-be-mm', 'frame-envelope', lambda d: d.update(frame_kind='depth_raw', unit='mm'))
    negative('jpeg-depth-rejected', 'frame-envelope', lambda d: d.update(frame_kind='depth_raw', unit='raw-device-code', encoding='jpeg'))
    negative('bad-digest-rejected', 'artifact-ref', lambda d: d.update(sha256='0'*64))
    negative('path-traversal-rejected', 'artifact-ref', lambda d: d.update(path='../README.md'))
    negative('synthetic-approval-rejected', 'calibration-profile', lambda d: d.update(status='approved'))
    negative('synthetic-pass-rejected', 'measurement-result', lambda d: d.update(decision='pass'))
    negative('duplicate-node-rejected', 'flow-definition', lambda d: d['nodes'].append(copy.deepcopy(d['nodes'][0])))
    negative('real-device-node-rejected', 'flow-definition', lambda d: d['nodes'][0].update(type='source.live_camera'))
    negative('port-type-mismatch', 'flow-definition', lambda d: d['edges'][0].update(to='judge'))
    negative('cyclic-or-invalid-back-edge', 'flow-definition', lambda d: d['edges'].append({'from':'archive','from_port':'out','to':'source','to_port':'in'}))
    negative('durable-without-record', 'stream-receipt', lambda d: d.update(level='durable'))
    negative('success-without-output', 'run-manifest', lambda d: d.update(outputs=[]))
    outcomes = []
    for name, kind, d in tests:
        try:
            validate(kind, d)
        except (ValueError, ValidationError) as exc:
            outcomes.append({'test': name, 'status': 'rejected-as-expected', 'reason': str(exc).splitlines()[0]})
        else:
            raise AssertionError('Negative case unexpectedly accepted: ' + name)
    geometry = geometry_probe(EXAMPLES)
    report = {'scope': 'design examples only; no UniVision runtime or hardware executed',
              'schema_count': len(schemas), 'positive_examples': positive,
              'negative_cases': outcomes, 'geometry': geometry,
              'live_repo_head_verified': False, 'network_tested': False,
              'camera_tested': False, 'production_acceptance': False}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"PASS: {len(schemas)} schemas, {len(positive)} examples, {len(outcomes)} rejected negative cases, 1 synthetic geometry probe.")
    print('Not a runtime, transport, camera, or physical measurement acceptance test.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
