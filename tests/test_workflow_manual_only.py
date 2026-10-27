"""Prevent accidental auto-triggering of GitHub Actions."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def validate_manual_only(text: str):
    lines = text.splitlines()
    indices = [i for i, line in enumerate(lines) if line.startswith('on:')]
    if len(indices) != 1:
        raise AssertionError('exactly one top-level on: required')
    events = []
    for line in lines[indices[0] + 1:]:
        if line and not line.startswith((' ', '\t', '#')):
            break
        if line.startswith('  ') and not line.startswith('    ') and not line.lstrip().startswith('#'):
            events.append(line.strip().split(':', 1)[0])
    if events != ['workflow_dispatch']:
        raise AssertionError(f'automatic Actions triggers forbidden: {events}')


class ManualActionsSafety(unittest.TestCase):
    def test_workflows(self):
        paths = list((ROOT/'.github/workflows').glob('*.yml')) + list((ROOT/'.github/workflows').glob('*.yaml'))
        self.assertTrue(paths)
        for p in paths:
            with self.subTest(p=p.name):
                validate_manual_only(p.read_text(encoding='utf-8'))

    def test_reject_push(self):
        with self.assertRaises(AssertionError):
            validate_manual_only('on:\n  workflow_dispatch:\n  push:\njobs:\n')

    def test_reject_schedule(self):
        with self.assertRaises(AssertionError):
            validate_manual_only('on:\n  schedule:\n  workflow_dispatch:\njobs:\n')
