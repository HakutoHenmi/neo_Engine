"""Exercise both analysis scripts against real C++ fixtures and reports."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def ps_literal(value):
    return "'" + str(value).replace("'", "''") + "'"


def invoke(script, files, output, *options, expected=0):
    command = (
        f"& {ps_literal(ROOT / 'scripts' / 'build' / script)} "
        f"-Files @({','.join(ps_literal(path) for path in files)}) "
        f"-OutputDirectory {ps_literal(output)} {' '.join(options)}"
    )
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
        cwd=ROOT, capture_output=True, encoding='utf-8', errors='replace', check=False,
    )
    if result.returncode != expected:
        raise AssertionError(f'{script}: expected exit {expected}, got {result.returncode}\n{result.stdout}\n{result.stderr}')


def main():
    with tempfile.TemporaryDirectory(prefix='ReadabilityFixture_', dir=ROOT / 'tests') as directory:
        fixture = Path(directory).resolve()
        assert fixture.is_relative_to((ROOT / 'tests').resolve())
        output = ROOT / 'artifacts' / 'code-quality' / 'readability-tests' / fixture.name
        complex_file = fixture / 'first source.cpp'
        duplicate = fixture / 'duplicate \u3042.inl'
        clean = fixture / 'clean.cpp'
        complex_text = '''int score(int value, int low, int high) {
    int total = 0;
    if (value > low) { total += value * 3; }
    if (value < high) { total += high - value; }
    for (int i = 0; i < value; ++i) { total += i * low; }
    if (total > high) { return total - high; }
    return total + low;
}
'''
        complex_file.write_text(complex_text, encoding='utf-8')
        duplicate.write_text(complex_text + '#define TRAILING_MARKER 1', encoding='utf-8')
        clean.write_text('int add(int value) {\n    return value + 1;\n}\n', encoding='utf-8')
        original_hashes = {path: hashlib.sha256(path.read_bytes()).digest() for path in (complex_file, duplicate, clean)}

        limits = ('-MaxComplexity 2', '-MaxFunctionLines 20', '-MaxParameters 2')
        complexity_output = output / 'complexity'
        invoke('measure_complexity.ps1', [complex_file, duplicate], complexity_output, *limits)
        report = json.loads((complexity_output / 'complexity.json').read_text(encoding='utf-8-sig'))
        assert report['files_analyzed'] == 2 and report['violating_function_count'] == 2
        assert all('complexity' in row['exceeded'] and 'parameters' in row['exceeded'] for row in report['violations'])
        invoke('measure_complexity.ps1', [complex_file], complexity_output, *limits, '-Check', expected=1)
        invoke('measure_complexity.ps1', [clean], complexity_output, *limits, '-Check')

        duplicate_output = output / 'duplicates'
        invoke('find_duplicates.ps1', [complex_file, duplicate], duplicate_output, '-MinimumTokens 30')
        summary = json.loads((duplicate_output / 'duplicates.json').read_text(encoding='utf-8-sig'))
        assert summary['files_analyzed'] == 2 and summary['duplicate_groups'] >= 1
        xml = ET.parse(duplicate_output / 'cpd.xml').getroot()
        ns = {'cpd': xml.tag.partition('}')[0].lstrip('{')}
        occurrences = xml.findall('cpd:duplication/cpd:file', ns)
        assert {node.attrib['path'] for node in occurrences} == {str(complex_file), str(duplicate)}
        assert all(int(node.attrib['line']) == 1 for node in occurrences)
        invoke('find_duplicates.ps1', [complex_file, duplicate], duplicate_output, '-MinimumTokens 30', '-Check', expected=1)
        invoke('find_duplicates.ps1', [clean], duplicate_output, '-MinimumTokens 30', '-Check')

        vendor = ROOT / 'Engine' / 'ThirdParty' / 'nlohmann' / 'ordered_map.hpp'
        for script in ('measure_complexity.ps1', 'find_duplicates.ps1'):
            invoke(script, [vendor], output / 'excluded', expected=1)
        for path, digest in original_hashes.items():
            assert hashlib.sha256(path.read_bytes()).digest() == digest, f'Analysis modified {path}'
    print('Readability tool integration tests passed: thresholds, duplicates, .inl, UTF-8 paths, exit codes and source preservation.')


if __name__ == '__main__':
    main()
