"""Measure the explicitly selected first-party C/C++ sources with Lizard."""

import argparse
import csv
import json
from pathlib import Path

import lizard


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file-list', required=True, type=Path)
    parser.add_argument('--output-directory', required=True, type=Path)
    parser.add_argument('--max-complexity', required=True, type=int)
    parser.add_argument('--max-lines', required=True, type=int)
    parser.add_argument('--max-parameters', required=True, type=int)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    files = args.file_list.read_text(encoding='utf-8-sig').splitlines()
    functions = []
    for filename in files:
        path = Path(filename)
        # .inl files contain C++ fragments; force the same parser as .cpp files.
        analysis = lizard.analyze_file.analyze_source_code(str(path.with_suffix('.cpp')), path.read_text(encoding='utf-8-sig'))
        for function in analysis.function_list:
            exceeded = []
            if function.cyclomatic_complexity > args.max_complexity:
                exceeded.append('complexity')
            if function.nloc > args.max_lines:
                exceeded.append('lines')
            if function.parameter_count > args.max_parameters:
                exceeded.append('parameters')
            functions.append({
                'file': str(path),
                'function': function.long_name,
                'start_line': function.start_line,
                'end_line': function.end_line,
                'nloc': function.nloc,
                'complexity': function.cyclomatic_complexity,
                'parameters': function.parameter_count,
                'tokens': function.token_count,
                'exceeded': ';'.join(exceeded),
            })
    functions.sort(key=lambda row: (-row['complexity'], -row['nloc'], row['file'], row['start_line']))
    violations = [row for row in functions if row['exceeded']]
    report = {
        'tool': 'Lizard',
        'files_analyzed': len(files),
        'function_count': len(functions),
        'violating_function_count': len(violations),
        'thresholds': {'complexity': args.max_complexity, 'nloc': args.max_lines, 'parameters': args.max_parameters},
        'violations': violations,
    }
    args.output_directory.mkdir(parents=True, exist_ok=True)
    csv_path = args.output_directory / 'complexity.csv'
    fields = ['file', 'function', 'start_line', 'end_line', 'nloc', 'complexity', 'parameters', 'tokens', 'exceeded']
    with csv_path.open('w', encoding='utf-8-sig', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(functions)
    (args.output_directory / 'complexity.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"Lizard: {len(files)} files, {len(functions)} functions, {len(violations)} functions above thresholds.")
    print(f"Report: {csv_path}")
    return 4 if args.check and violations else 0


if __name__ == '__main__':
    raise SystemExit(main())
