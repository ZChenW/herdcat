"""Source-preserving configuration parsers for the setup entrypoint."""
from dataclasses import dataclass
import json


class SetupError(Exception):
    """A configuration cannot safely be changed."""


def load_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise SetupError('duplicate JSON key')
            result[key] = value
        return result
    try:
        return json.loads(text, object_pairs_hook=pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(
                              SetupError('non-finite JSON number')))
    except (ValueError, RecursionError):
        raise SetupError('invalid JSON') from None


@dataclass
class Node:
    start: int
    end: int
    value: object
    children: object = None


class Jsonc:
    """Parse JSONC with source spans; edits retain unrelated text and comments."""
    def __init__(self, text):
        self.text = text
        self.pos = 0
        self.root = self.value()
        self.space()
        if self.pos != len(text) or not isinstance(self.root.value, dict):
            raise SetupError('expected a JSONC object')

    def space(self):
        while self.pos < len(self.text):
            if self.text[self.pos].isspace():
                self.pos += 1
            elif self.text.startswith('//', self.pos):
                end = self.text.find('\n', self.pos)
                self.pos = len(self.text) if end < 0 else end
            elif self.text.startswith('/*', self.pos):
                end = self.text.find('*/', self.pos + 2)
                if end < 0:
                    raise SetupError('unterminated JSONC comment')
                self.pos = end + 2
            else:
                return

    def value(self, depth=0):
        if depth > 128:
            raise SetupError('JSONC nesting too deep')
        self.space()
        start = self.pos
        if start == len(self.text):
            raise SetupError('missing JSONC value')
        kind = self.text[start]
        if kind in '{[':
            self.pos += 1
            closing = '}' if kind == '{' else ']'
            value = {} if kind == '{' else []
            children = {} if kind == '{' else []
            self.space()
            if self.pos < len(self.text) and self.text[self.pos] == closing:
                self.pos += 1
                return Node(start, self.pos, value, children)
            while True:
                self.space()
                key_start = self.pos
                if kind == '{':
                    key = self.value(depth + 1)
                    if not isinstance(key.value, str) or key.value in value:
                        raise SetupError('invalid or duplicate JSONC key')
                    self.space()
                    if self.text[self.pos:self.pos + 1] != ':':
                        raise SetupError('missing JSONC colon')
                    self.pos += 1
                    child = self.value(depth + 1)
                    value[key.value] = child.value
                    children[key.value] = (key_start, child)
                else:
                    child = self.value(depth + 1)
                    value.append(child.value)
                    children.append(child)
                self.space()
                token = self.text[self.pos:self.pos + 1]
                if token == closing:
                    self.pos += 1
                    return Node(start, self.pos, value, children)
                if token != ',':
                    raise SetupError('missing JSONC comma')
                self.pos += 1
                self.space()
                if self.text[self.pos:self.pos + 1] == closing:
                    self.pos += 1
                    return Node(start, self.pos, value, children)
        try:
            value, length = json.JSONDecoder().raw_decode(self.text[start:])
        except (ValueError, RecursionError):
            raise SetupError('invalid JSONC value') from None
        if isinstance(value, float) and not (-float('inf') < value < float('inf')):
            raise SetupError('non-finite JSONC number')
        self.pos += length
        return Node(start, self.pos, value)


def apply_edits(text, edits):
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text


