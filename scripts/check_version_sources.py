#!/usr/bin/env python3
"""Read-only validation of native version authorities; never imports application code."""
from __future__ import annotations
import argparse
import ast
import configparser
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tomllib

class SourceError(ValueError):
    pass

def read(root, path):
    relative = PurePosixPath(path)
    if relative.is_absolute() or '..' in relative.parts or '\\' in path:
        raise SourceError('Unsafe relative source path')
    target = root / path
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise SourceError('Symlink sources are not accepted')
    return target.read_text(encoding='utf-8-sig')

def extract(text, spec):
    kind = spec['kind']
    if kind == 'ini':
        parsed = configparser.ConfigParser(interpolation=None, strict=True)
        parsed.read_string(text)
        return parsed.get(spec['section'], spec['key'])
    if kind == 'python-tuple':
        assignments = [n for n in ast.parse(text).body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == spec['variable'] for t in n.targets)]
        if len(assignments) != 1:
            raise SourceError('Expected one literal Python version tuple')
        value = ast.literal_eval(assignments[0].value)
        if not isinstance(value, tuple) or len(value) != 3 or not all(isinstance(v, str) and v.isdigit() for v in value):
            raise SourceError('Expected three string components')
        derived = [n for n in ast.parse(text).body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == '__version__' for t in n.targets)]
        if len(derived) != 1 or ast.dump(derived[0].value) != ast.dump(ast.parse("'.'.join(" + spec['variable'] + ')', mode='eval').body):
            raise SourceError('Python __version__ must derive from the inspected tuple')
        return '.'.join(value)
    if kind == 'cmake-project':
        # Comments cannot supply a version. Literal project() declarations only.
        cleaned = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
        projects = re.findall(r'(?is)\bproject\s*\(([^()]*)\)', cleaned)
        matching = [p for p in projects if p.split() and p.split()[0] == spec['project']]
        if len(matching) != 1:
            raise SourceError('Expected one named CMake project declaration')
        fields = re.findall(r'(?i)\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})(?=\s|$)', matching[0])
        if len(fields) != 1:
            raise SourceError('Expected one literal CMake project version')
        return fields[0]
    if kind == 'cpp-version':
        components = []
        for name in ('major', 'minor', 'patch'):
            values = re.findall(r'^inline constexpr std::uint32_t version_' + name + r' = ([0-9]+);$', text, re.M)
            if len(values) != 1:
                raise SourceError('Expected one C++ version component')
            components.append(values[0])
        strings = re.findall(r'^inline constexpr std::string_view version_string = "([0-9]+\.[0-9]+\.[0-9]+)";$', text, re.M)
        if strings != ['.'.join(components)]:
            raise SourceError('C++ version string and components disagree')
        return strings[0]
    if kind == 'windows-resource':
        tuples = re.findall(r'^\s*(?:filevers|prodvers)=\(([0-9]+), ([0-9]+), ([0-9]+), ([0-9]+)\),$', text, re.M)
        strings = re.findall(r"StringStruct\('(?:FileVersion|ProductVersion)', '([0-9]+(?:\.[0-9]+){3})'\)", text)
        if len(tuples) != 2 or len(strings) != 2 or len(set(['.'.join(t) for t in tuples] + strings)) != 1:
            raise SourceError('Windows file/product resource versions disagree')
        return strings[0]
    raise SourceError('Unsupported read-only version adapter: ' + kind)

def check(root):
    config = json.loads(read(root, 'version-sources.json'))
    if config.get('schema_version') != 1:
        raise SourceError('Unsupported native source schema')
    result = {}
    for unit, details in config['units'].items():
        values = [extract(read(root, s['path']), s) for s in [details['canonical'], *details.get('mirrors', [])]]
        if len(set(values)) != 1:
            raise SourceError(unit + ': canonical and native mirrors disagree')
        result[unit] = {'version': values[0], 'mode': 'read-only', 'reason': details['reason']}
    for assertion in config.get('assertions', []):
        data = tomllib.loads(read(root, assertion['path']))
        if assertion['kind'] == 'cargo-inherited-version':
            if data.get('package', {}).get('version') != {'workspace': True}:
                raise SourceError(assertion['path'] + ': expected version.workspace = true')
        elif assertion['kind'] == 'uv-local-version':
            project = tomllib.loads(read(root, assertion['manifest']))['project']
            entries = [p for p in data.get('package', []) if p.get('name') == assertion['package'] and p.get('source') == {'editable': '.'}]
            if len(entries) != 1 or entries[0].get('version') != project.get('version'):
                raise SourceError(assertion['path'] + ': local distribution version differs from project')
        else:
            raise SourceError('Unsupported assertion')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['show', 'check'], nargs='?', default='check')
    parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        versions = check(root)
    except (SourceError, ValueError, KeyError, OSError, configparser.Error, SyntaxError) as exc:
        print('Native version check failed: ' + str(exc), file=sys.stderr)
        return 1
    print(json.dumps({'status': 'ok', 'native_units': versions}, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
