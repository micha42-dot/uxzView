import hashlib
from html.parser import HTMLParser
import json
from datetime import datetime, timezone
from pathlib import Path
import sys
import tempfile
import tarfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import package as release_package
import dependency_sources
from runtime_files import clean_runtime, runtime_files, private_part
from source_archive import source_files, documentation_files, ROOT_FILES
from bundle_tools import IDS, prepare as prepare_tools, release_tools
from build_tool_packs import pack
from package_native import archive_stage


class ReadmeSourceTests(unittest.TestCase):
    def test_local_readme_images_are_in_public_sources(self):
        class Images(HTMLParser):
            def __init__(self):
                super().__init__()
                self.sources = []

            def handle_starttag(self, tag, attrs):
                if tag == 'img':
                    self.sources.extend(value for key, value in attrs if key == 'src')
        root = Path(__file__).resolve().parents[1]
        parser = Images()
        parser.feed((root / 'README.md').read_text(encoding='utf-8'))
        selected = set(source_files(root))
        documentation = set(documentation_files(root))
        for source in parser.sources:
            if '://' not in source and not source.startswith('data:'):
                with self.subTest(image=source):
                    self.assertIn(root / source, selected)
                    self.assertIn(root / source, documentation)


class ReleaseRuntimeTests(unittest.TestCase):
    def setUp(self):
        # Use the platform temporary directory, independent of CMake creating
        # tests/output. addCleanup also removes fixtures when setUp fails.
        self.temporary = tempfile.TemporaryDirectory(prefix='uxzview-release-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / 'dist/uxzView'
        self.runtime.mkdir(parents=True)
        (self.root / 'build').mkdir()
        (self.root / 'scripts').mkdir()
        lock = self.root / 'scripts/sdk-lock.json'
        lock.write_bytes(b'{}')
        for name in ('uxzView.exe', 'ixextensions.dll', 'ixrawpreview.dll'):
            (self.root / 'build' / name).write_bytes(b'current-build-' + name.encode())
            (self.runtime / name).write_bytes((self.root / 'build' / name).read_bytes())
        (self.runtime / 'runtime-manifest.json').write_text(json.dumps({'unresolved': []}))
        (self.runtime / 'dependency-sources.json').write_text(json.dumps({
            'source_urls_checked_at': datetime.now(timezone.utc).isoformat(),
            'sdk_lock_sha256': hashlib.sha256(lock.read_bytes()).hexdigest(),
            'runtime_files': {}, 'packages': []}))
        self.addCleanup(patch.stopall)
        patch.object(release_package, 'ROOT', self.root).start()

    def test_matching_built_application_and_modules_are_accepted(self):
        release_package.validate_release(self.runtime)

    def _stale_source_inventory(self):
        path = self.runtime / 'dependency-sources.json'
        manifest = json.loads(path.read_text())
        manifest['source_index'] = dependency_sources.SOURCE_BASE
        manifest['source_urls_checked_at'] = '2000-01-01T00:00:00+00:00'
        manifest['packages'] = [{'name': 'example',
            'source_url': dependency_sources.SOURCE_BASE + 'example-1.src.tar.zst'}]
        path.write_text(json.dumps(manifest))
        (self.runtime / 'SOURCES.md').write_text(
            'Source-index availability check: 2000-01-01T00:00:00+00:00\n')
        patch.object(dependency_sources, 'ROOT', self.root).start()
        return manifest

    def test_refresh_rechecks_sources_without_sdk_download_archives(self):
        manifest = self._stale_source_inventory()
        checked = datetime.now(timezone.utc).isoformat()
        with patch.object(dependency_sources, 'check_source_urls', return_value=checked) as check:
            dependency_sources.refresh_urls()
        check.assert_called_once_with(manifest['packages'])
        self.assertFalse((self.root / '.deps/cache').exists())
        self.assertEqual(json.loads((self.runtime / 'dependency-sources.json').read_text())
                         ['source_urls_checked_at'], checked)
        self.assertIn(checked, (self.runtime / 'SOURCES.md').read_text())
        release_package.validate_release(self.runtime)

    def test_refresh_rejects_changed_runtime_before_source_check(self):
        self._stale_source_inventory()
        before = (self.runtime / 'dependency-sources.json').read_bytes()
        (self.runtime / 'uxzView.exe').write_bytes(b'changed-runtime')
        with patch.object(dependency_sources, 'check_source_urls') as check:
            with self.assertRaises(RuntimeError):
                dependency_sources.refresh_urls()
        check.assert_not_called()
        self.assertEqual((self.runtime / 'dependency-sources.json').read_bytes(), before)

    def test_refresh_source_failure_preserves_existing_inventory_and_notes(self):
        self._stale_source_inventory()
        before = {name: (self.runtime / name).read_bytes()
                  for name in ('dependency-sources.json', 'SOURCES.md')}
        with patch.object(dependency_sources, 'check_source_urls', side_effect=RuntimeError('source absent')):
            with self.assertRaises(RuntimeError):
                dependency_sources.refresh_urls()
        for name, content in before.items():
            self.assertEqual((self.runtime / name).read_bytes(), content)
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_stale_application_is_rejected_before_packaging(self):
        (self.runtime / 'uxzView.exe').write_bytes(b'previous-build')
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_missing_built_module_is_rejected(self):
        (self.runtime / 'ixextensions.dll').unlink()
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_stale_module_is_rejected(self):
        (self.runtime / 'ixrawpreview.dll').write_bytes(b'previous-module')
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_missing_archive_plugin_is_rejected(self):
        plugin = self.root / 'build/imageformats/ixarchive.dll'
        plugin.parent.mkdir()
        plugin.write_bytes(b'archive-codec')
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_stale_archive_plugin_is_rejected(self):
        for folder in (self.root / 'build/imageformats', self.runtime / 'imageformats'):
            folder.mkdir()
        (self.root / 'build/imageformats/ixarchive.dll').write_bytes(b'archive-codec')
        (self.runtime / 'imageformats/ixarchive.dll').write_bytes(b'old-codec')
        with self.assertRaises(RuntimeError):
            release_package.validate_release(self.runtime)

    def test_deployment_preserves_personal_profile(self):
        personal = self.runtime / 'IxView-data/file-journal/trash/important.bin'
        personal.parent.mkdir(parents=True)
        personal.write_bytes(b'personal backup')
        marker = self.runtime / 'portable.mode'
        marker.touch()
        tools = self.runtime / 'tools/tools.json'
        tools.parent.mkdir()
        tools.write_bytes(b'personal disabled/removed tools')
        clean_runtime(self.runtime, self.root)
        self.assertEqual(personal.read_bytes(), b'personal backup')
        self.assertTrue(marker.exists())
        self.assertEqual(tools.read_bytes(), b'personal disabled/removed tools')
        self.assertFalse((self.runtime / 'uxzView.exe').exists())

    def test_archives_and_dependency_checks_exclude_personal_profiles(self):
        personal = self.runtime / 'IxView-data/extensions/private.dll'
        personal.parent.mkdir(parents=True)
        personal.write_bytes(b'personal plugin')
        (self.runtime / 'portable.mode').touch()
        release_package.validate_release(self.runtime)
        self.assertNotIn(personal, list(runtime_files(self.runtime)))
        (self.root / 'CMakeLists.txt').write_text('project(IxView VERSION 0.4.3)')
        def source_zip(destination):
            with zipfile.ZipFile(destination, 'w') as archive:
                archive.writestr('source.txt', 'public')
            return destination
        with patch.object(release_package, 'audit'), patch.object(release_package, 'create_source_archive', source_zip):
            release_package.package()
        with zipfile.ZipFile(self.root / 'dist/uxzView-win64.zip') as archive:
            self.assertTrue(any(name.endswith('uxzView.exe') for name in archive.namelist()))
            self.assertFalse(any(private_part(part) for name in archive.namelist() for part in Path(name).parts))

    def test_source_selection_excludes_portable_data(self):
        for name in ROOT_FILES:
            (self.root / name).touch(exist_ok=True)
        public = self.root / 'src/example.cpp'
        private = self.root / 'src/IxView-data/settings/private.ini'
        private.parent.mkdir(parents=True)
        public.write_text('// public')
        private.write_text('personal data')
        selected = set(source_files(self.root))
        self.assertIn(public, selected)
        self.assertNotIn(private, selected)
        guide = self.root / 'docs/NEW_GUIDE.md'
        notes = self.root / 'docs/WORK_IN_PROGRESS.md'
        guide.parent.mkdir()
        guide.write_text('public new guide')
        notes.write_text('private notes')
        documentation = set(documentation_files(self.root))
        self.assertIn(guide, documentation)
        self.assertNotIn(notes, documentation)
        self.assertNotIn(public, documentation)
        for relative in ('src/PORTABLE.MODE/settings.ini', 'src/ixview-DATA/local.ini',
                         'scripts/__pycache__/cached.pyc', 'tests/output/private.png',
                         'docs/REQUIREMENTS.de.txt'):
            private_path = self.root / relative
            private_path.parent.mkdir(parents=True, exist_ok=True)
            private_path.write_text('local data')
            self.assertNotIn(private_path, set(source_files(self.root)))

    def test_native_archives_exclude_personal_tool_stores(self):
        stage = self.root / 'native'
        public = ('bin/uxzView', 'uxzView.app/Contents/MacOS/uxzView', 'share/docs/tools.md')
        private = ('tools/tools.json', 'bin/Tools/private-engine',
                   'uxzView.app/Contents/MacOS/tools/private-engine',
                   'bin/IxView-data/settings.ini', 'portable.mode')
        for name in public + private:
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        source = self.root / 'source.zip'
        source.write_bytes(b'public source fixture')
        output = self.root / 'native.tar.gz'
        archive_stage(stage, source, output)
        with tarfile.open(output) as archive:
            names = archive.getnames()
            for name in public:
                self.assertIn('uxzView/' + name, names)
            for name in private:
                self.assertNotIn('uxzView/' + name, names)
            self.assertEqual(archive.extractfile('uxzView-source.zip').read(), source.read_bytes())
        for name in private:
            self.assertTrue((stage / name).exists())


class BundledToolsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='uxzview-bundle-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / 'dist/uxzView'
        self.runtime.mkdir(parents=True)
        (self.root / 'build').mkdir()
        (self.root / 'resources').mkdir()
        output = self.root / 'dist/tools'
        output.mkdir()
        payload = self.root / 'payload'
        payload.write_bytes(b'fixture payload')
        entries = []
        for identifier in sorted(IDS):
            if identifier.startswith('tessdata-'):
                code = identifier.removeprefix('tessdata-')
                manifest = dict(kind='ocr-language', os='any', arch='any', language=code)
                files = {'data/' + code + '.traineddata': payload}
            else:
                manifest = dict(kind='image-tool', os='windows', arch='x86_64', entryPoint='bin/tool.exe',
                                protocol='zxing-json-v1' if identifier == 'zxing-cpp' else 'tesseract-text-v1')
                files = {'bin/tool.exe': payload}
            entries.append(pack(output, dict(id=identifier, version='1.0.0', **manifest), files))
        (self.root / 'resources/tool-catalog.json').write_text(json.dumps(dict(schema=1, packages=entries)))

    def test_ready_tools_and_release_defaults_do_not_publish_personal_state(self):
        prepare_tools(self.root, self.runtime)
        state_file = self.runtime / 'tools/tools.json'
        state = json.loads(state_file.read_text())
        self.assertEqual(set(state['packages']), IDS)
        self.assertEqual(state['layout'], 'flat')
        self.assertTrue((self.runtime / 'tools/zxing-cpp/1.0.0/bin/tool.exe').is_file())
        self.assertFalse((self.runtime / 'tools/tools').exists())
        state['packages']['zxing-cpp']['enabled'] = False
        del state['packages']['tesseract-ocr']
        state_file.write_text(json.dumps(state))
        private = self.runtime / 'tools/private.txt'
        private.write_text('private')
        before = state_file.read_bytes()
        prepare_tools(self.root, self.runtime)
        self.assertEqual(state_file.read_bytes(), before)
        self.assertEqual(private.read_text(), 'private')
        files = dict(release_tools(self.runtime))
        self.assertNotIn('tools/private.txt', files)
        published = json.loads(files['tools/tools.json'].read_text())
        self.assertTrue(published['packages']['zxing-cpp']['enabled'])
        self.assertIn('tesseract-ocr', published['packages'])

    def test_changed_release_payload_is_rejected(self):
        prepare_tools(self.root, self.runtime)
        files = dict(release_tools(self.runtime))
        files['tools/zxing-cpp/1.0.0/bin/tool.exe'].write_bytes(b'changed')
        with self.assertRaises(RuntimeError):
            list(release_tools(self.runtime))

    def test_corrupt_pack_leaves_installed_tools_untouched(self):
        prepare_tools(self.root, self.runtime)
        state = (self.runtime / 'tools/tools.json').read_bytes()
        package = next((self.root / 'dist/tools').glob('*.ixtool'))
        package.write_bytes(package.read_bytes()[:-1] + b'x')
        with self.assertRaises(RuntimeError):
            prepare_tools(self.root, self.runtime)
        self.assertEqual((self.runtime / 'tools/tools.json').read_bytes(), state)
        self.assertEqual(len(dict(release_tools(self.runtime))), 15)


if __name__ == '__main__':
    unittest.main()
