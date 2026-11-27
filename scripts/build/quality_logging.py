"""Collect local quality reports and queue save/build events for AI review."""

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / 'scripts' / 'build'
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx', '.inl'}


def now():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


@contextmanager
def exclusive_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as handle:
        if handle.tell() == 0:
            handle.write(b'0')
            handle.flush()
        handle.seek(0)
        acquired = False
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
        except OSError:
            pass
        try:
            yield acquired
        finally:
            if acquired:
                handle.seek(0)
                if os.name == 'nt':
                    msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def sources():
    result = []
    for top in ('Engine', 'Game'):
        for directory, children, files in os.walk(ROOT / top):
            children[:] = [name for name in children if name.lower() not in {'thirdparty', 'out', 'build', '.git'}]
            result.extend(Path(directory) / name for name in files if Path(name).suffix.lower() in SOURCE_SUFFIXES)
    return sorted(result)


def versions():
    watched = sources() + [ROOT / '.clang-format', ROOT / '.clang-tidy', SCRIPTS / 'quality_logging.json']
    result = {}
    for path in watched:
        try:
            stat = path.stat()
            result[str(path)] = (stat.st_mtime_ns, stat.st_size)
        except FileNotFoundError:
            pass
    return result


def source_hashes():
    result = {}
    for path in sources():
        try:
            result[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        except FileNotFoundError:
            pass
    return result


def config_hashes():
    paths = [ROOT / '.clang-format', ROOT / '.clang-tidy', SCRIPTS / 'quality_logging.json']
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}


def relative(filename):
    path = Path(filename)
    if not path.is_absolute():
        path = ROOT / path
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def diagnostic(tool, rule, filename, line, message, **extra):
    return dict(tool=tool, rule=rule, file=relative(filename), line=int(line), message=message, **extra)


def read_findings(tool, raw_log, output):
    if tool == 'lizard':
        report = json.loads((output / 'complexity.json').read_text(encoding='utf-8-sig'))
        return [diagnostic(tool, row['exceeded'], row['file'], row['start_line'], row['function'],
                           severity='warning', end_line=row['end_line'],
                           metrics={key: row[key] for key in ('complexity', 'nloc', 'parameters')})
                for row in report['violations']]
    if tool == 'cpd':
        root = ET.parse(output / 'cpd.xml').getroot()
        ns = {'c': root.tag.partition('}')[0].lstrip('{')}
        findings = []
        for index, group in enumerate(root.findall('c:duplication', ns), 1):
            locations = [dict(file=relative(node.attrib['path']), line=int(node.attrib['line']),
                              end_line=int(node.attrib['endline'])) for node in group.findall('c:file', ns)]
            first = locations[0]
            findings.append(diagnostic(tool, 'duplicate-code', first['file'], first['line'],
                                       f"{group.attrib['tokens']} tokens in {len(locations)} locations",
                                       severity='info', group=index, locations=locations,
                                       metrics={'tokens': int(group.attrib['tokens']), 'lines': int(group.attrib['lines'])}))
        return findings
    findings = []
    for text in raw_log.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        match = re.match(r'^(.+?)(?:\((\d+),(\d+)\)|:(\d+):(\d+)):\s*(warning|error)\s*:\s*(.*)', text)
        if not match:
            continue
        filename, msline, mscol, line, col, severity, message = match.groups()
        if tool == 'format' and 'clang-formatted' not in message:
            continue
        check = re.search(r'\[([a-z][\w-]+)\]', message)
        rule = 'formatting' if tool == 'format' else check.group(1) if check else 'compiler-diagnostic'
        findings.append(diagnostic(tool, rule, filename, msline or line, message,
                                   severity=severity, column=int(mscol or col)))
    # The same header can be reported by several translation units.
    return list({(row['file'], row['line'], row['rule'], row['message']): row for row in findings}.values())


def run_process(command, log_path, timeout):
    started = time.monotonic()
    environment = dict(os.environ, PYTHONUTF8='1')
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    with log_path.open('wb') as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                   env=environment, creationflags=flags)
        try:
            code = process.wait(timeout=timeout)
            state = 'completed'
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                try:
                    subprocess.run(['taskkill.exe', '/PID', str(process.pid), '/T', '/F'],
                                   stdout=output, stderr=subprocess.STDOUT, check=False, timeout=10)
                except subprocess.TimeoutExpired:
                    output.write(b'Process-tree termination timed out.\n')
            if process.poll() is None:
                process.kill()
            process.wait()
            code, state = process.returncode, 'timeout'
    return dict(exit_code=code, process_state=state, duration_seconds=round(time.monotonic() - started, 2))


def collect(log_directory, request):
    with exclusive_lock(log_directory / 'analysis.lock') as acquired:
        if not acquired:
            return None
        config = json.loads((SCRIPTS / 'quality_logging.json').read_text(encoding='utf-8-sig'))
        run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + uuid.uuid4().hex[:8]
        run = log_directory / 'runs' / run_id
        run.mkdir(parents=True)
        write_json(run / 'request.json', request)
        before = source_hashes()
        configuration_before = config_hashes()
        packet = dict(schema_version=1, run_id=run_id, started_at=now(), repository=str(ROOT),
                      request=request, source_sha256=before, config_sha256=configuration_before, tools=[], findings=[])
        for tool in ('format', 'lizard', 'cpd', 'clang-tidy'):
            write_json(log_directory / 'status.json', dict(state='running', run_id=run_id, tool=tool, updated_at=now()))
            result = dict(tool=tool, scope='Engine and Game', raw_log=str(run / (tool + '.log')))
            if tool == 'clang-tidy' and (request['trigger'] == 'Save' or request.get('skip_clang_tidy')):
                result.update(status='skipped', reason='Save checks defer clang-tidy to build/manual collection.')
                packet['tools'].append(result)
                continue
            output = log_directory / 'work' / tool
            output.mkdir(parents=True, exist_ok=True)
            command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                       str(SCRIPTS / 'run_quality_task.ps1'), '-Tool', tool, '-RequestFile', str(run / 'request.json'),
                       '-OutputDirectory', str(output)]
            try:
                timeout = config['clang_tidy_timeout_seconds'] if tool == 'clang-tidy' else config['tool_timeout_seconds']
                result.update(run_process(command, Path(result['raw_log']), timeout))
                # Parse only reports produced by a successful run. Failed tools can leave old reports.
                valid_report = tool in ('format', 'clang-tidy') or (result['process_state'] == 'completed' and result['exit_code'] == 0)
                findings = read_findings(tool, Path(result['raw_log']), output) if valid_report else []
                if result['process_state'] == 'timeout':
                    result['status'] = 'timeout'
                elif result['exit_code'] != 0 and not (tool == 'format' and findings):
                    result['status'] = 'error'
                else:
                    result['status'] = 'findings' if findings else 'ok'
                result['finding_count'] = len(findings)
                if tool == 'format' and request['trigger'] == 'Save':
                    result['scope'] = request['files'] or 'Engine and Game (configuration changed)'
                if tool == 'clang-tidy':
                    result['scope'] = request['project'] + ' (' + request['configuration'] + ')'
                    if len(request['files']) == 1 and Path(request['files'][0]).suffix.lower() in {'.cpp', '.cc', '.cxx', '.c'}:
                        result['scope'] = request['files']
                packet['findings'].extend(findings)
            except Exception as error:
                result.update(status='error', error=str(error))
            packet['tools'].append(result)
        after = source_hashes()
        packet['stale_files'] = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
        packet['stale_configuration'] = configuration_before != config_hashes()
        packet['completed_at'] = now()
        packet['finding_count'] = len(packet['findings'])
        packet['has_tool_errors'] = any(row['status'] in ('error', 'timeout') for row in packet['tools'])
        write_json(run / 'results.json', packet)
        write_json(log_directory / 'latest.json', packet)
        write_json(log_directory / ('latest-' + request['trigger'].lower() + '.json'), packet)
        summary = [f"Run: {run_id}", f"Trigger: {request['trigger']}", f"Completed: {packet['completed_at']}",
                   f"Findings: {packet['finding_count']}", f"Changed during analysis: {len(packet['stale_files'])}",
                   f"Configuration changed during analysis: {packet['stale_configuration']}"]
        summary += [f"{row['tool']}: {row['status']} ({row.get('finding_count', 0)} findings)" for row in packet['tools']]
        summary += ['', 'First 30 candidates:']
        summary += [f"{row['file']}:{row['line']} [{row['tool']}/{row['rule']}] {row['message']}" for row in packet['findings'][:30]]
        (run / 'summary.txt').write_text('\n'.join(summary) + '\n', encoding='utf-8')
        (log_directory / 'latest.txt').write_text('\n'.join(summary) + '\n', encoding='utf-8')
        (log_directory / 'AI_REVIEW.txt').write_text(
            'Read latest.json and latest-build.json (if present). Check status.json for a run in progress.\n'
            'Treat error, timeout, and skipped tools as incomplete coverage, not a clean result.\n'
            'Verify source_sha256 and config_sha256 before editing. Re-analyze stale files/configuration.\n'
            'Review the original code and prioritize small behavior-preserving fixes.\n'
            'Duplicate code is a candidate, not proof that the code should be merged.\n'
            'Run relevant tests and collect again after a fix. Do not change thresholds merely to hide findings.\n', encoding='utf-8')
        with (log_directory / 'events.jsonl').open('a', encoding='utf-8') as journal:
            journal.write(json.dumps(dict(run_id=run_id, trigger=request['trigger'], completed_at=packet['completed_at'],
                                          finding_count=packet['finding_count'], has_tool_errors=packet['has_tool_errors'],
                                          results=str(run / 'results.json'))) + '\n')
        write_json(log_directory / 'status.json', dict(state='idle', run_id=run_id, updated_at=now()))
        print(f"Quality log: {log_directory / 'latest.json'} ({packet['finding_count']} findings)", flush=True)
        return packet


def running(log_directory):
    with exclusive_lock(log_directory / 'watcher.lock') as acquired:
        return not acquired


def start(log_directory):
    if running(log_directory):
        return
    log_directory.mkdir(parents=True, exist_ok=True)
    flags = (subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP) if os.name == 'nt' else 0
    with (log_directory / 'watcher.log').open('ab') as output:
        subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'watch', '--log-directory', str(log_directory)],
                         cwd=ROOT, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                         creationflags=flags, start_new_session=os.name != 'nt')
    for _ in range(30):
        if running(log_directory):
            return
        time.sleep(.1)
    raise RuntimeError('Watcher did not start. See watcher.log.')


def enqueue(log_directory, request):
    write_json(log_directory / 'queue' / (uuid.uuid4().hex + '.json'), request)


def watch(log_directory):
    with exclusive_lock(log_directory / 'watcher.lock') as acquired:
        if not acquired:
            print('Quality watcher already running.', flush=True)
            return
        stop = log_directory / 'watcher.stop'
        stop.unlink(missing_ok=True)
        write_json(log_directory / 'watcher.json', dict(pid=os.getpid(), started_at=now(), repository=str(ROOT)))
        config = json.loads((SCRIPTS / 'quality_logging.json').read_text(encoding='utf-8-sig'))
        previous, pending = versions(), set()
        last_change, last_run = 0.0, 0.0
        print('Quality watcher ready', flush=True)
        while not stop.exists():
            try:
                current = versions()
                changed = {name for name in previous.keys() | current.keys() if previous.get(name) != current.get(name)}
                if changed:
                    pending.update(changed)
                    last_change = time.monotonic()
                previous = current
                quiet = time.monotonic() - last_change >= config['save_debounce_seconds']
                if pending and quiet:
                    files = [name for name in sorted(pending) if Path(name).suffix.lower() in SOURCE_SUFFIXES]
                    enqueue(log_directory, dict(trigger='Save', event='file-change', files=files,
                                                project='All', configuration='Development', queued_at=now()))
                    pending.clear()
                queued = sorted((log_directory / 'queue').glob('*.json'))
                if queued and time.monotonic() - last_run >= config['minimum_interval_seconds']:
                    requests = [json.loads(path.read_text(encoding='utf-8-sig')) for path in queued]
                    build = [item for item in requests if item['trigger'] == 'Build']
                    request = dict(trigger='Build' if build else 'Save', event='coalesced', queued_at=now(),
                                   project='All', configuration=(build[-1]['configuration'] if build else 'Development'),
                                   files=sorted({name for item in requests for name in item.get('files', [])}),
                                   events=requests)
                    if collect(log_directory, request) is not None:
                        for path in queued:
                            path.unlink(missing_ok=True)
                        last_run = time.monotonic()
                time.sleep(1)
            except Exception as error:
                print(f'{now()} Watcher error: {error}', flush=True)
                time.sleep(2)
        print('Quality watcher stopped', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['collect', 'enqueue', 'watch', 'start', 'stop', 'status'])
    parser.add_argument('--log-directory', type=Path, default=ROOT / 'artifacts' / 'code-quality' / 'logs')
    parser.add_argument('--trigger', choices=['Manual', 'Save', 'Build'], default='Manual')
    parser.add_argument('--project', choices=['All', 'Engine', 'Game'], default='All')
    parser.add_argument('--configuration', choices=['Debug', 'Development', 'Release'], default='Development')
    parser.add_argument('--event', default='manual')
    parser.add_argument('--file', type=Path)
    parser.add_argument('--skip-clang-tidy', action='store_true')
    parser.add_argument('--no-start-watcher', action='store_true')
    args = parser.parse_args()
    logs = args.log_directory.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    request = dict(trigger='Build' if args.action == 'enqueue' else args.trigger, event=args.event,
                   project=args.project, configuration=args.configuration, queued_at=now(),
                   files=[], skip_clang_tidy=args.skip_clang_tidy)
    if args.file:
        file = args.file.resolve()
        if not file.is_file() or not file.is_relative_to(ROOT) or file.suffix.lower() not in SOURCE_SUFFIXES or 'thirdparty' in {part.lower() for part in file.parts} or file.relative_to(ROOT).parts[0] not in ('Engine', 'Game'):
            raise ValueError('Select a first-party C/C++ file under Engine or Game.')
        request['files'] = [str(file)]
    if args.action == 'collect':
        if collect(logs, request) is None:
            raise RuntimeError('Another analysis is running. Retry after status.json becomes idle.')
    elif args.action == 'enqueue':
        enqueue(logs, request)
        if not args.no_start_watcher:
            start(logs)
        print(f'Quality analysis queued: {logs}')
    elif args.action == 'start':
        start(logs)
        print(f'Quality watcher running: {logs}')
    elif args.action == 'watch':
        watch(logs)
    elif args.action == 'stop':
        (logs / 'watcher.stop').write_text(now(), encoding='utf-8')
        print('Quality watcher stop requested.')
    else:
        print(json.dumps(dict(watcher_running=running(logs), log_directory=str(logs))))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'Quality logging error: {error}', file=sys.stderr)
        raise SystemExit(1)
