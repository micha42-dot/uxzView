"""Validate embedded language catalogs without Qt or an installed SDK."""
from collections import Counter
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LocalizationTests(unittest.TestCase):
    def test_five_complete_columns_and_unique_sources(self):
        rows = json.loads((ROOT / 'resources/languages.json').read_text(encoding='utf-8'))
        self.assertGreater(len(rows), 500)
        self.assertEqual(len(rows), len({r[0] for r in rows}))
        for row in rows:
            self.assertEqual(len(row), 5, row[0])
            self.assertTrue(all(isinstance(t, str) and t for t in row), row[0])

    def test_substitution_parameters_are_preserved(self):
        rows = json.loads((ROOT / 'resources/languages.json').read_text(encoding='utf-8'))
        for row in rows:
            parameters = Counter(re.findall(r'%\d+|\{name\}|#+', row[0]))
            for text in row[1:]:
                self.assertEqual(Counter(re.findall(r'%\d+|\{name\}|#+', text)), parameters, row[0])

    def test_every_translation_call_has_a_catalog_entry(self):
        known = {r[0] for r in json.loads((ROOT / 'resources/languages.json').read_text(encoding='utf-8'))}
        for file in (ROOT / 'src').rglob('*'):
            if file.suffix not in {'.cpp', '.h'}:
                continue
            for match in re.finditer(r'IX_TEXT\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', file.read_text(encoding='utf-8')):
                source = ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))
                self.assertIn(source, known, str(file.relative_to(ROOT)))
            bindings = re.finditer(
                r'bindUiText\(\s*[^,;\n]+,\s*"[^"]+",\s*QStringLiteral\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)',
                file.read_text(encoding='utf-8'))
            for match in bindings:
                source = ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))
                self.assertIn(source, known, str(file.relative_to(ROOT)))

    def test_working_menu_labels_have_translations_and_valid_text(self):
        known = {r[0] for r in json.loads((ROOT / 'resources/languages.json').read_text(encoding='utf-8'))}
        menus = json.loads((ROOT / 'resources/menus.de.json').read_text(encoding='utf-8'))['menus']
        for menu in menus:
            self.assertIn(menu['label'], known)
            for item in menu['items']:
                if not any(item.get(k) for k in ('command', 'submenu', 'dynamic')):
                    continue
                label = item['label']
                self.assertNotIn('\ufffd', label)
                if re.fullmatch(r'RGB -> [RGB]{3}', label):
                    continue  # channel-order notation is language independent
                self.assertIn(label, known)


if __name__ == '__main__':
    unittest.main()
