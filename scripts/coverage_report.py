#!/usr/bin/env python3
"""GCC coverage in a private source copy; no desktop or normal build mutation."""
import argparse
import gzip
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


EXCLUDED = {'src/graphics/embedded_assets.c'}


COPIED = ('Makefile', 'src', 'include', 'lib', 'protocols', 'tests', 'scripts',
          'assets', 'integrations', 'completions', 'man', 'packaging/systemd',
          'herdcat.conf.example', '.clang-format', '.clang-tidy')

def merge_documents(documents, root):
  """Union counters from shared libraries, embedded sources and executables."""
  root = Path(root).resolve()
  files = {}
  for document in documents:
    cwd = Path(document['current_working_directory'])
    for source in document['files']:
      path = Path(source['file'])
      path = path if path.is_absolute() else cwd / path
      try:
        name = path.resolve().relative_to(root).as_posix()
      except ValueError:
        continue
      if not name.startswith(('src/', 'include/')) or name in EXCLUDED:
        continue
      target = files.setdefault(name, {'lines': {}, 'branches': {},
                                       'functions': {}})
      for line in source['lines']:
        number = line['line_number']
        target['lines'][number] = target['lines'].get(number, 0) + line['count']
        for i, branch in enumerate(line.get('branches', [])):
          # GCC versions before 14 do not provide block IDs. Index is stable
          # because every translation unit uses the same unoptimized flags.
          key = (number, line.get('function_name', ''), i,
                 branch.get('source_block_id'), branch.get('destination_block_id'))
          target['branches'][key] = target['branches'].get(key, 0) + branch['count']
      for function in source['functions']:
        key = (function['name'], function['start_line'], function['start_column'])
        if key not in target['functions']:
          target['functions'][key] = dict(function, execution_count=0)
        target['functions'][key]['execution_count'] += function['execution_count']
  for target in files.values():
    target['functions'] = list(target['functions'].values())
  return files


def totals(files):
  lines = [count for source in files.values() for count in source['lines'].values()]
  branches = [count for source in files.values() for count in source['branches'].values()]
  return (sum(count > 0 for count in lines), len(lines),
          sum(count > 0 for count in branches), len(branches))


def percentage(hit, total):
  return f'{100 * hit / total:.2f}%' if total else 'n/a'


def write_report(documents, work, output, label):
  files = merge_documents(documents, work)
  hit, total, taken, branches = totals(files)
  rows = [f'{label}: lines {hit}/{total} ({percentage(hit, total)}); '
          f'branches {taken}/{branches} ({percentage(taken, branches)})',
          'Branches = executed GCC branch arcs, including short-circuit outcomes.',
          'Excluded: embedded_assets.c, lib/, protocols/, tests/, system headers.',
          '', 'File | Lines hit/total (%) | Branches hit/total (%)']
  ordered = sorted(files, key=lambda name: (totals({name: files[name]})[0] /
                   max(1, totals({name: files[name]})[1]), name))
  for name in ordered:
    lh, lt, bh, bt = totals({name: files[name]})
    rows.append(f'{name} | {lh}/{lt} ({percentage(lh, lt)}) | '
                f'{bh}/{bt} ({percentage(bh, bt)})')
  uncovered = []
  for name in sorted(files):
    for function in sorted(files[name]['functions'], key=lambda f: f['start_line']):
      if function['execution_count'] == 0:
        uncovered.append(f"{name}:{function['start_line']} {function['name']}")
  rows.extend(['', f'Unexecuted functions ({len(uncovered)}):', *uncovered])
  (output / f'{label}.txt').write_text('\n'.join(rows) + '\n')
  # Standalone HTML requires no third-party package or external resources.
  links = []
  pages = output / label
  pages.mkdir(exist_ok=True)
  for name in ordered:
    target = name.replace('/', '_') + '.html'
    links.append(f'<li><a href="{label}/{target}">{html.escape(name)}</a></li>')
    source_lines = (work / name).read_text().splitlines()
    rendered = []
    for number, line in enumerate(source_lines, 1):
      count = files[name]['lines'].get(number)
      color = '#d5f5dc' if count else '#ffd9d9' if count == 0 else '#f5f5f5'
      rendered.append(f'<tr id="L{number}" style="background:{color}">'
                      f'<td>{number}</td><td>{count if count is not None else ""}</td>'
                      f'<td><pre>{html.escape(line)}</pre></td></tr>')
    (pages / target).write_text('<!doctype html><meta charset="utf-8">'
         f'<title>{html.escape(name)}</title><h1>{html.escape(name)}</h1>'
         '<style>pre{margin:0}td{vertical-align:top;padding:0 8px}</style>'
         '<p>Green: executed; red: executable but unexecuted; gray: non-code.</p>'
         '<table>' + ''.join(rendered) + '</table>')
  (output / f'{label}.html').write_text('<!doctype html><meta charset="utf-8">'
         f'<title>{label} coverage</title><pre>{html.escape(chr(10).join(rows))}</pre>'
         '<ul>' + ''.join(links) + '</ul>')
  (output / 'index.html').write_text('<!doctype html><meta charset="utf-8">'
         '<title>herdcat coverage</title><h1>herdcat coverage</h1><ul>' +
         ''.join(f'<li><a href="{name}.html">{name}</a></li>'
                 for name in ('unit', 'runtime', 'merged')
                 if (output / f'{name}.html').exists()) + '</ul>')
  print(rows[0], flush=True)


def collect(work):
  documents = []
  with tempfile.TemporaryDirectory(prefix='hc-gcov-', dir='/tmp') as temporary:
    for notes in sorted((work / 'build').rglob('*.gcno')):
      data = notes.with_suffix('.gcda')
      if data.exists() and not os.access(data, os.R_OK):
        raise RuntimeError(f'gcov data is unreadable: {data}')
      result = subprocess.run(['gcov', '--json-format', '--branch-probabilities',
                               str(notes)], cwd=temporary, capture_output=True, text=True)
      if result.returncode:
        raise RuntimeError(f'gcov failed for {notes}: {result.stderr}')
      # Missing gcda means an unexecuted translation unit, not missing data.
      for path in Path(temporary).glob('*.gcov.json.gz'):
        with gzip.open(path, 'rt') as stream:
          documents.append(json.load(stream))
        path.unlink()
  return documents


def run_make(work, output, goal, jobs):
  with tempfile.TemporaryDirectory(prefix='hc-cov-', dir='/tmp') as private:
    env = dict(os.environ)
    for key in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'NIRI_SOCKET', 'SWAYSOCK',
                'DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'DBUS_SESSION_BUS_ADDRESS',
                'CLAUDE_PID', 'MAKEFLAGS', 'MFLAGS'):
      env.pop(key, None)
    env.update(HOME=private, XDG_RUNTIME_DIR=private, PYTHONDONTWRITEBYTECODE='1')
    for key in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'):
      env[key] = str(Path(private) / key.lower())
    command = ['make', f'-j{jobs}', 'BUILD_TYPE=coverage', f'TEST_JOBS={jobs}', goal]
    print('Running ' + ' '.join(command), flush=True)
    with (output / f'{goal}.log').open('w') as log:
      result = subprocess.run(command, cwd=work, env=env, stdout=log,
                              stderr=subprocess.STDOUT)
    return result.returncode


def snapshot(root):
  paths = [root / 'Makefile']
  for directory in ('src', 'include', 'lib', 'protocols', 'tests', 'scripts',
                    'integrations', 'assets'):
    paths.extend(path for path in (root / directory).rglob('*')
                 if path.is_file() and '__pycache__' not in path.parts)
  return {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
          for path in paths}


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--runtime', action='store_true')
  parser.add_argument('--jobs', type=int, default=2)
  args = parser.parse_args()
  if args.jobs < 1:
    parser.error('jobs must be positive')
  for tool in ('gcc', 'gcov'):
    if not shutil.which(tool):
      parser.error(f'{tool} is required for GCC coverage')
  root = Path(__file__).resolve().parents[1]
  output = root / 'build/coverage'
  work = output / 'work'
  output.mkdir(parents=True, exist_ok=True)
  if not args.runtime:
    if work.exists():
      shutil.rmtree(work)
    for old in ('unit', 'runtime', 'merged'):
      for suffix in ('.json', '.txt', '.html'):
        (output / (old + suffix)).unlink(missing_ok=True)
      if (output / old).exists():
        shutil.rmtree(output / old)
    # Only what a build and its tests read; a checkout also holds packaging
    # output and local notes, some of them dangling links.
    work.mkdir(parents=True)
    for name in COPIED:
      source = root / name
      if source.is_dir():
        shutil.copytree(source, work / name, symlinks=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
      elif source.exists():
        shutil.copy2(source, work / name)
    (output / 'snapshot.json').write_text(json.dumps(snapshot(root)))
  elif not (output / 'unit.json').exists() or not work.exists():
    parser.error('run make coverage before make coverage-runtime')
  elif snapshot(root) != json.loads((output / 'snapshot.json').read_text()):
    parser.error('sources changed since unit coverage; run make coverage again')
  else:
    for path in (work / 'build').rglob('*.gcda'):
      path.unlink()
  label = 'runtime' if args.runtime else 'unit'
  code = run_make(work, output, 'test-runtime' if args.runtime else 'test', args.jobs)
  documents = collect(work)
  if not merge_documents(documents, work):
    raise RuntimeError('no project coverage data was generated')
  (output / f'{label}.json').write_text(json.dumps(documents))
  write_report(documents, work, output, label)
  if args.runtime:
    unit = json.loads((output / 'unit.json').read_text())
    write_report(unit + documents, work, output, 'merged')
  if code:
    print(f'Tests failed; coverage is partial. See {output / ("test-runtime.log" if args.runtime else "test.log")}', flush=True)
  return code


if __name__ == '__main__':
  if sys.argv[1:2] == ['--cc']:
    # Special test/helper recipes hard-code -O2 or omit CFLAGS. Override them
    # consistently so duplicate source branch graphs can be unioned safely.
    if 'src/input/input_helper.c' in sys.argv[2:]:
      # The real helper intentionally forbids gcov's exit-time file writes.
      # Its validators are instrumented by test_input_helper.c instead.
      raise SystemExit(subprocess.call(['gcc', *sys.argv[2:]]))
    raise SystemExit(subprocess.call(['gcc', *sys.argv[2:], '-O0', '-g',
                                     '-U_FORTIFY_SOURCE', '--coverage']))
  raise SystemExit(main())
