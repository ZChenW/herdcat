#!/usr/bin/env python3
"""Replay runtime assertions without opening sandbox-blocked Unix sockets.

Load only function definitions, so the scripts' compositor startup stays off.
The font/drag rectangles are the review logs' actual committed snapshots.
"""
import ast
from pathlib import Path
import unittest

SCRIPTS = Path(__file__).resolve().parent


def load_function(script, name, namespace=None):
    tree = ast.parse((SCRIPTS / script).read_text())
    function = next(node for node in ast.walk(tree)
                    if isinstance(node, ast.FunctionDef) and node.name == name)
    scope = {} if namespace is None else namespace
    exec(compile(ast.Module(body=[function], type_ignores=[]), script, 'exec'), scope)
    return scope[name]


class GeometryTests(unittest.TestCase):
    def test_buffer_tiers_and_visible_bars(self):
        check = load_function('test_runtime.py', 'assert_buffer_sizes')
        # Independently checked by test_surface_tiers.c: quiet 40/60px cats,
        # five-board 40px cat, visible 45px cat. Physical scales 1.25/1.5/2.
        text = '\n'.join(('commit TEST-1 90x70', 'commit TEST-1 300x149',
                          'commit TEST-1 357x179', 'commit TEST-2 216x140',
                          'commit TEST-2 712x314', 'commit TEST-1 960x243',
                          'commit TEST-2 2048x324'))
        check(text)
        for missing in text.splitlines():
            with self.assertRaises(AssertionError, msg=missing):
                check(text.replace(missing, ''))

    def test_zero_tier_menu_tracks_cat_during_growth(self):
        for below in (False, True):
            old_cat = (0, 20, 72, 39)
            new_cat = (84, 80, 72, 40)
            card = (92, 16, 56, 62) if not below else (92, 122, 56, 62)
            cats = {'TEST-1': old_cat}

            def wait_for(ready):
                cats['TEST-1'] = new_cat
                self.assertTrue(ready(), 'visible card must use the current cat')

            def settled(sample, ready, **kwargs):
                self.assertTrue(ready(sample()))
                return sample()

            function = load_function('test_font_panel_runtime.py', 'card_region',
                                     dict(cat_regions=lambda: cats,
                                          regions=lambda: {'TEST-1': card},
                                          wait_for=wait_for, wait_settled=settled))
            self.assertEqual(function('TEST-1', below=below), card)

    def test_drag_tracks_output_cat_not_margin(self):
        check = load_function('test_drag_runtime.py', 'vertical_travel')
        # Review fan trace: cat moved up 40; padded surface margin moved 38.
        before = (0, 0, 0, 280, 240, 156)
        after = (0, 0, 38, 300, 240, 156)
        cat_before, cat_after = (84, 114, 72, 40), (84, 112, 72, 40)
        self.assertTrue(check(before, after, cat_before, cat_after, -40, False, 600))
        self.assertFalse(check(before, after, cat_before, cat_after, -39, False, 600))
        # Post trace has the same displacement with a taller canvas.
        self.assertTrue(check((0, 0, 0, 248, 300, 182),
                              (0, 0, 38, 268, 300, 182),
                              (116, 140, 72, 40), (116, 138, 72, 40),
                              -40, False, 600))
        self.assertTrue(check((0, 0, 0, 393, 238, 156),
                              (42, 0, 0, 363, 238, 156),
                              (83, 0, 72, 40), (83, 3, 72, 40), 45, True, 768))


if __name__ == '__main__':
    unittest.main()
