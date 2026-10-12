import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'check_version_sources.py'
spec = importlib.util.spec_from_file_location('native_versions', MODULE_PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class NativeVersionTests(unittest.TestCase):
    def test_cmake_preserves_two_components(self):
        self.assertEqual(module.extract('project(Demo VERSION 0.1 LANGUAGES CXX)', {'kind':'cmake-project','project':'Demo'}), '0.1')
    def test_cmake_rejects_dynamic_version(self):
        with self.assertRaises(module.SourceError):
            module.extract('project(Demo VERSION ${SDK_VERSION})', {'kind':'cmake-project','project':'Demo'})
    def test_cmake_rejects_duplicate_projects(self):
        with self.assertRaises(module.SourceError):
            module.extract('project(Demo VERSION 0.1)\nproject(Demo VERSION 0.1)', {'kind':'cmake-project','project':'Demo'})
    def test_cmake_ignores_commented_version(self):
        self.assertEqual(module.extract('# project(Demo VERSION 9.9)\nproject(Demo VERSION 0.1)', {'kind':'cmake-project','project':'Demo'}), '0.1')
    def test_ini_reads_only_metadata(self):
        self.assertEqual(module.extract('[metadata]\nversion = 0.0.4\n[dependency]\nversion = 9.9.9', {'kind':'ini','section':'metadata','key':'version'}), '0.0.4')
    def test_python_tuple_derivation(self):
        self.assertEqual(module.extract("__version_info__ = ('1', '8', '6')\n__version__ = '.'.join(__version_info__)\n", {'kind':'python-tuple','variable':'__version_info__'}), '1.8.6')
    def test_python_refuses_evaluation(self):
        with self.assertRaises(ValueError):
            module.extract("__version_info__ = open('unsafe')\n", {'kind':'python-tuple','variable':'__version_info__'})
    def test_python_rejects_conflicting_derived_version(self):
        with self.assertRaises(module.SourceError):
            module.extract("__version_info__ = ('1', '8', '6')\n__version__ = '9.9.9'\n", {'kind':'python-tuple','variable':'__version_info__'})
    def test_cpp_detects_component_drift(self):
        content='inline constexpr std::uint32_t version_major = 0;\ninline constexpr std::uint32_t version_minor = 3;\ninline constexpr std::uint32_t version_patch = 0;\ninline constexpr std::string_view version_string = "0.3.0";\n'
        self.assertEqual(module.extract(content, {'kind':'cpp-version'}), '0.3.0')
        with self.assertRaises(module.SourceError):
            module.extract(content.replace('version_minor = 3', 'version_minor = 4'), {'kind':'cpp-version'})
    def test_read_only_and_path_escape(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); source=root/'source'; source.write_text('example')
            before=source.read_bytes(); self.assertEqual(module.read(root,'source'), 'example'); self.assertEqual(before,source.read_bytes())
            with self.assertRaises(module.SourceError):module.read(root,'../source')
    def test_uv_lock_version_mismatch(self):
        import json
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root/'pyproject.toml').write_text('[project]\nname="example"\nversion="0.1.0"\n')
            (root/'uv.lock').write_text('[[package]]\nname="example"\nversion="0.1.0"\nsource={editable="."}\n')
            (root/'version-sources.json').write_text(json.dumps({'schema_version':1,'units':{},'assertions':[{'kind':'uv-local-version','path':'uv.lock','manifest':'pyproject.toml','package':'example'}]}))
            self.assertEqual(module.check(root), {})
            (root/'uv.lock').write_text((root/'uv.lock').read_text().replace('0.1.0','0.2.0'))
            with self.assertRaises(module.SourceError):module.check(root)
    def test_resource_four_components(self):
        content="filevers=(1, 1, 1, 0),\nprodvers=(1, 1, 1, 0),\nStringStruct('FileVersion', '1.1.1.0')\nStringStruct('ProductVersion', '1.1.1.0')"
        self.assertEqual(module.extract(content, {'kind':'windows-resource'}), '1.1.1.0')
        with self.assertRaises(module.SourceError):module.extract(content.replace("'1.1.1.0'", "'1.1.2.0'", 1), {'kind':'windows-resource'})

if __name__ == '__main__':
    unittest.main()
