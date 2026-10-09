"""Shared recording format, privacy checks and adapter-aware comparison."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


class Redactor:
    def __init__(self, home, username):
        self.home, self.username = home, username
        self.ids, self.prompts = {}, {}

    def clean(self, value, key='', private=False):
        content = key.lower() in {
            'prompt', 'response', 'answer', 'text', 'content', 'input', 'output',
            'tool_input', 'tool_output', 'tool_response', 'tool_result',
            'tool_args', 'toolargs', 'toolinput', 'toolresult', 'message',
            'description', 'command', 'arguments', 'error', 'result', 'reason'}
        private = private or content
        if isinstance(value, dict):
            return {k: self.clean(v, k, private) for k, v in value.items()}
        if isinstance(value, list):
            return [self.clean(v, key, private) for v in value]
        if key.lower() in {'agent_pid', 'pid'} and isinstance(value, int) and not isinstance(value, bool) and value > 1:
            return 424242
        if not isinstance(value, str) or not value:
            return value
        if key.lower() == 'prompt':
            return self.prompts.setdefault(value, f'Sample prompt {len(self.prompts) + 1}')
        if private:
            return 'Sample text'
        lower = key.lower()
        if lower in {'cwd', 'workspaceroot', 'directory', 'project_path'}:
            return '/tmp/herdcat-probe/project'
        if lower in {'transcript_path', 'sessionfile', 'log_path'}:
            return '/tmp/herdcat-probe/transcript.jsonl'
        if lower == 'id' or lower.endswith(('_id', 'id')):
            return self.ids.setdefault(value, f'{len(self.ids) + 1:032x}')
        value = value.replace(self.home, '/tmp/herdcat-probe/home')
        value = value.replace(self.username, 'sample-user')
        value = re.sub(r'[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}', 'sample-email', value)
        value = re.sub(r'[A-Za-z0-9_+/=-]{40,}', 'sample-token', value)
        return value


def check_private(text, home, username):
    for item in (home, username):
        if item and item in text:
            raise ValueError('recording contains a private identity or home path')
    if re.search(r'[\w.+-]+@[\w.-]+\.[A-Za-z]{2,}', text):
        raise ValueError('recording contains an email')
    if re.search(r'[A-Za-z0-9_+/=-]{40,}', text):
        raise ValueError('recording contains a token-like string')


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def checked_json(path, value, home, username):
    # SHA-256 attribution is intentionally long, not a credential.
    filtered = {k: v for k, v in value.items() if k != 'source_sha256'} if isinstance(value, dict) else value
    check_private(json.dumps(filtered, ensure_ascii=False), home, username)
    write_json(path, value)


def shape(records):
    fields, pattern = {}, []

    def visit(value, path, target):
        kind = ('null' if value is None else 'boolean' if isinstance(value, bool)
                else 'number' if isinstance(value, (int, float))
                else 'string' if isinstance(value, str)
                else 'object' if isinstance(value, dict) else 'array')
        target.setdefault(path, set()).add(kind)
        if isinstance(value, dict):
            for key, item in value.items():
                visit(item, path + '.' + key, target)
        elif isinstance(value, list):
            for item in value:
                visit(item, path + '[]', target)

    for event, payload in records:
        if not pattern or pattern[-1] != event:
            pattern.append(event)
        visit(payload, '$', fields.setdefault(event, {}))
    return {'pattern': pattern, 'fields': {
        event: {key: sorted(types) for key, types in paths.items()}
        for event, paths in fields.items()}}


def compare_shapes(old, new, policy):
    changes, consumed = [], False
    old_events, new_events = set(old['fields']), set(new['fields'])
    for kind, names in [('added event', new_events - old_events),
                        ('removed event', old_events - new_events)]:
        for name in sorted(names):
            used = name in policy['events']
            changes.append({'kind': kind, 'event': name, 'consumed': used})
            consumed |= used
    if old['pattern'] != new['pattern']:
        # Compare only the adapter-consumed subsequence for automatic acceptance.
        used = ([e for e in old['pattern'] if e in policy['events']] !=
                [e for e in new['pattern'] if e in policy['events']])
        changes.append({'kind': 'order pattern', 'old': old['pattern'],
                        'new': new['pattern'], 'consumed': used})
        consumed |= used
    for event in sorted(old_events | new_events):
        before, after = old['fields'].get(event, {}), new['fields'].get(event, {})
        for path in sorted(set(before) | set(after)):
            if before.get(path) == after.get(path):
                continue
            used = event in policy['events'] and any(
                part.rstrip('[]') in policy['fields'] for part in path.split('.')[1:])
            kind = 'added field' if path not in before else 'removed field' if path not in after else 'changed type'
            changes.append({'kind': kind, 'event': event, 'path': path,
                            'old': before.get(path), 'new': after.get(path), 'consumed': used})
            consumed |= used
    return {'changes': changes, 'consumed_changed': consumed}


def conclusion(diff, milestones_ok):
    if not milestones_ok or (diff and diff['consumed_changed']):
        return 'needs-attention'
    if diff is None:
        return 'new'
    return 'compatible' if diff['changes'] else 'unchanged'


def recording_records(directory):
    directory = Path(directory)
    for line in (directory / 'expect.tsv').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        _, filename, explicit, *_ = line.split('\t')
        if filename == '-':
            continue
        payload = json.loads((directory / filename).read_text())
        event = explicit if explicit != '-' else payload.get('hook_event_name', payload.get('hookEventName', payload.get('hookName', '')))
        yield event, payload
