#!/usr/bin/env python3
"""Fail closed unless each GitHub Actions workflow is workflow_dispatch-only.

This is an offline pre-push check; no GitHub Actions run is started.
"""
from pathlib import Path
import re
import sys

WORKFLOW_SUFFIXES = {'.yml', '.yaml'}
EVENT = re.compile(r'^\s{2}([A-Za-z_][A-Za-z_0-9]*):(?:\s|$)')
ON = re.compile(r'^(?:on|[\'" ]on[\'" ]):\s*(.*?)\s*$'.replace('[\'" ]', "[\\'\"]"))

def check(root: Path) -> list[str]:
    errors = []
    folder = root / '.github' / 'workflows'
    if not folder.exists():
        return errors
    for file in sorted(folder.iterdir()):
        if not file.is_file() or file.suffix.lower() not in WORKFLOW_SUFFIXES:
            continue
        lines = file.read_text(encoding='utf-8').splitlines()
        found = []
        for i, line in enumerate(lines):
            match = ON.match(line)
            if not match:
                continue
            raw = match.group(1).split('#', 1)[0].strip()
            if raw:
                found.append([raw])
                continue
            events = []
            for subsequent in lines[i + 1:]:
                if subsequent and not subsequent.startswith((' ', '\t')) and not subsequent.lstrip().startswith('#'):
                    break
                m = EVENT.match(subsequent)
                if m:
                    events.append(m.group(1))
            found.append(events)
        if found != [['workflow_dispatch']]:
            errors.append(f'{file.relative_to(root)}: unexpected triggers: {found!r}')
    return errors

if __name__ == '__main__':
    errors = check(Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd())
    for error in errors:
        print('ERROR', error)
    print('PASS manual-only workflows' if not errors else 'FAIL unsafe workflows')
    raise SystemExit(bool(errors))
