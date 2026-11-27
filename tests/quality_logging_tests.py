"""Verify structured reports, source freshness, watcher events, and MSBuild integration."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import uuid


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'build' / 'quality_logging.py'
spec = importlib.util.spec_from_file_location('quality_logging', SCRIPT)
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


class QualityLoggingTests(unittest.TestCase):
    def setUp(self):
        temporary_root = ROOT / 'artifacts' / 'code-quality' / 'logging-tests'
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=temporary_root)
        self.directory = Path(self.temporary.name).resolve()
        self.assertTrue(self.directory.is_relative_to(temporary_root.resolve()))

    def tearDown(self):
        self.temporary.cleanup()

    def test_tool_failures_and_stale_sources_are_not_clean_reports(self):
        logs = self.directory
        stale = logs / 'work' / 'lizard'
        stale.mkdir(parents=True)
        (stale / 'complexity.json').write_text('{"violations":[{"stale":true}]}', encoding='utf-8')

        def process(command, path, timeout):
            tool = command[command.index('-Tool') + 1]
            output = logs / 'work' / tool
            output.mkdir(parents=True, exist_ok=True)
            message = ''
            if tool == 'format':
                message = str(ROOT / 'Engine' / 'JobSystem.cpp') + ':3:1: error: code should be clang-formatted [-Wclang-format-violations]\n'
            if tool == 'cpd':
                (output / 'cpd.xml').write_text('<pmd-cpd xmlns="https://pmd-code.org/schema/cpd-report"/>', encoding='utf-8')
            if tool == 'clang-tidy':
                message = str(ROOT / 'Engine' / 'JobSystem.cpp') + '(8,2): warning :  test warning [readability-function-size]\n'
            path.write_text(message, encoding='utf-8')
            return dict(exit_code=1 if tool != 'cpd' else 0,
                        process_state='timeout' if tool == 'clang-tidy' else 'completed', duration_seconds=0)

        request = dict(trigger='Manual', project='Engine', configuration='Development', files=[])
        with patch.object(quality, 'run_process', side_effect=process), patch.object(
            quality, 'source_hashes', side_effect=[{'Engine/Core/JobSystem.cpp': 'before'}, {'Engine/Core/JobSystem.cpp': 'after'}]
        ):
            packet = quality.collect(logs, request)
        results = {row['tool']: row for row in packet['tools']}
        self.assertEqual(results['format']['status'], 'findings')
        self.assertEqual(results['lizard']['status'], 'error')
        self.assertEqual(results['clang-tidy']['status'], 'timeout')
        self.assertEqual(results['clang-tidy']['finding_count'], 1)
        self.assertTrue(packet['has_tool_errors'])
        self.assertEqual(packet['stale_files'], ['Engine/Core/JobSystem.cpp'])
        self.assertFalse(any(row['tool'] == 'lizard' for row in packet['findings']))
        self.assertEqual(read(logs / 'latest.json')['run_id'], packet['run_id'])
        self.assertEqual(read(logs / 'status.json')['state'], 'idle')
        with quality.exclusive_lock(logs / 'analysis.lock') as acquired:
            self.assertTrue(acquired)
            self.assertIsNone(quality.collect(logs, request))

    def test_msbuild_queues_without_recursion_and_can_be_disabled(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe'
        msbuild = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild',
                                          '-find', 'MSBuild\\Current\\Bin\\MSBuild.exe'], text=True).splitlines()[0]
        project = self.directory / 'DirectXGameEngine.proj'
        props = ROOT / 'scripts' / 'build' / 'code_quality.props'
        project.write_text(f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<Import Project="{props}"/>
<Target Name="PrepareForBuild"><Message Text="Fixture build" Importance="high"/></Target>
</Project>''', encoding='utf-8')
        for enabled in (True, False):
            logs = self.directory / ('enabled' if enabled else 'disabled')
            command = [msbuild, str(project), '-nologo', '-target:PrepareForBuild', '-property:Configuration=Development',
                       f'-property:EnableCodeQualityLogging={str(enabled).lower()}',
                       '-property:CodeQualityAutoStartWatcher=false', f'-property:CodeQualityLogDirectory={logs}']
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            events = list((logs / 'queue').glob('*.json'))
            self.assertEqual(len(events), int(enabled), result.stdout)
            if enabled:
                self.assertEqual(read(events[0])['trigger'], 'Build')
                self.assertEqual(read(events[0])['event'], 'build-start')
                self.assertEqual(read(events[0])['project'], 'Engine')

    def test_process_timeout_is_bounded_and_reported(self):
        result = quality.run_process([sys.executable, '-c', 'import time; time.sleep(30)'],
                                     self.directory / 'timeout.log', .2)
        self.assertEqual(result['process_state'], 'timeout')
        self.assertLess(result['duration_seconds'], 12)

    def test_real_save_event_is_logged_without_changing_source(self):
        logs = self.directory / 'watcher'
        source = ROOT / 'Game' / ('QualityLoggingFixture_' + uuid.uuid4().hex + '.cpp')
        source.write_text('int fixture(int value) { return value; }\n', encoding='utf-8')
        output_path = self.directory / 'watcher-output.log'
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        with output_path.open('wb') as output:
            process = subprocess.Popen([sys.executable, str(SCRIPT), 'watch', '--log-directory', str(logs)],
                                       cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, creationflags=flags)
            try:
                deadline = time.monotonic() + 10
                while not (logs / 'watcher.json').exists() and time.monotonic() < deadline:
                    time.sleep(.1)
                self.assertTrue((logs / 'watcher.json').exists())
                # Wait until the initial file snapshot has been taken.
                time.sleep(.5)
                content = 'int fixture(int value){if(value>0)return value;return 0;}\n'
                source.write_text(content, encoding='utf-8')
                deadline = time.monotonic() + 40
                while not (logs / 'latest-save.json').exists() and time.monotonic() < deadline:
                    time.sleep(.2)
                self.assertTrue((logs / 'latest-save.json').exists(), output_path.read_text(errors='replace'))
                packet = read(logs / 'latest-save.json')
                self.assertEqual(packet['request']['trigger'], 'Save')
                self.assertFalse(packet['has_tool_errors'], packet['tools'])
                self.assertTrue(any(row['file'] == source.relative_to(ROOT).as_posix() and row['tool'] == 'format'
                                    for row in packet['findings']))
                self.assertEqual(source.read_text(encoding='utf-8'), content)
                self.assertEqual({row['tool']: row['status'] for row in packet['tools']}['clang-tidy'], 'skipped')
            finally:
                (logs / 'watcher.stop').write_text('stop', encoding='utf-8')
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                source.unlink(missing_ok=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
