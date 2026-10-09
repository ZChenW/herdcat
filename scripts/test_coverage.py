#!/usr/bin/env python3
"""Offline coverage accounting tests with hand-written gcov JSON responses."""
import unittest
import subprocess
from pathlib import Path
import tempfile

from coverage_report import merge_documents, totals, snapshot


class CoverageTests(unittest.TestCase):
  def test_duplicate_translation_units_use_union_of_lines_and_branches(self):
    # Two executables exercise opposite outcomes of the same source branch.
    def document(counts):
      return {'current_working_directory': '/repo', 'files': [{
        'file': 'src/config/config_parse.c',
        'functions': [{'name': 'parse', 'start_line': 3, 'start_column': 1,
                       'end_line': 8, 'execution_count': 1}],
        'lines': [{'line_number': 4, 'function_name': 'parse', 'count': 1,
                   'branches': [{'count': n, 'fallthrough': bool(i),
                                 'throw': False, 'source_block_id': 2,
                                 'destination_block_id': 3 + i}
                                for i, n in enumerate(counts)]},
                  {'line_number': 5, 'count': counts[0], 'branches': []}]}]}
    files = merge_documents([document([1, 0]), document([0, 1])], '/repo')
    self.assertEqual(totals(files), (2, 2, 2, 2))
    self.assertEqual(files['src/config/config_parse.c']['functions'][0]
                     ['execution_count'], 2)

  def test_exclusions_and_unexecuted_functions_remain_visible(self):
    def source(name):
      return {'file': name,
              'functions': [{'name': 'never_called', 'start_line': 1,
                             'start_column': 1, 'execution_count': 0}],
              'lines': [{'line_number': 2, 'count': 0,
                         'branches': [{'count': 0}, {'count': 0}]}]}
    names = ['src/utils/json.c', 'src/graphics/embedded_assets.c',
             'lib/nanosvg.h', 'protocols/xdg-shell-protocol.c',
             'tests/test_config.c', 'include/utils/json.h', '/other/src/file.c']
    files = merge_documents([{'current_working_directory': '/repo',
                              'files': [source(name) for name in names]}], '/repo')
    self.assertEqual(list(files), ['src/utils/json.c', 'include/utils/json.h'])
    self.assertEqual(totals(files), (0, 2, 0, 4))
    self.assertEqual(files['src/utils/json.c']['functions'][0]['name'],
                     'never_called')

  def test_same_line_has_distinct_short_circuit_outcomes(self):
    document = {'current_working_directory': '/repo', 'files': [{
        'file': '/repo/src/utils/json.c', 'functions': [], 'lines': [{
            'line_number': 10, 'count': 2, 'branches': [
                {'count': 1}, {'count': 1}, {'count': 0}, {'count': 2}]}]}]}
    self.assertEqual(totals(merge_documents([document, document], '/repo')),
                     (1, 1, 3, 4))

  def test_compiler_wrapper_overrides_special_recipe_optimization(self):
    result = subprocess.run([
        'python3', str(Path(__file__).with_name('coverage_report.py')), '--cc',
        '-O2', '-D_FORTIFY_SOURCE=2', '-dM', '-E', '-x', 'c', '-'],
        input='', capture_output=True, text=True, check=True)
    self.assertNotIn('#define __OPTIMIZE__', result.stdout)
    self.assertNotIn('#define _FORTIFY_SOURCE', result.stdout)

  def test_privileged_helper_keeps_its_hardened_build(self):
    result = subprocess.run([
        'python3', str(Path(__file__).with_name('coverage_report.py')), '--cc',
        '-O2', '-D_FORTIFY_SOURCE=2', '-###', '-c', 'src/input/input_helper.c'],
        capture_output=True, text=True, check=True)
    self.assertNotIn('-fprofile-arcs', result.stderr)
    self.assertNotIn('-ftest-coverage', result.stderr)
    self.assertIn('-O2', result.stderr)
    self.assertIn('_FORTIFY_SOURCE=2', result.stderr)

  def test_source_snapshot_detects_edits_and_new_tests_ignores_reports(self):
    with tempfile.TemporaryDirectory(prefix='hc-cov-accounting-', dir='/tmp') as directory:
      root = Path(directory)
      (root / 'Makefile').write_text('test:')
      (root / 'src').mkdir()
      (root / 'src/a.c').write_text('int a;')
      original = snapshot(root)
      (root / 'docs').mkdir()
      (root / 'docs/report.md').write_text('coverage')
      self.assertEqual(original, snapshot(root))
      (root / 'src/a.c').write_text('int b;')
      self.assertNotEqual(original, snapshot(root))
      original = snapshot(root)
      (root / 'tests').mkdir()
      (root / 'tests/test_a.c').write_text('int main(void);')
      self.assertNotEqual(original, snapshot(root))


if __name__ == '__main__':
  unittest.main()
