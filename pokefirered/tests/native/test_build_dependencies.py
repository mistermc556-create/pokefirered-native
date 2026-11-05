"""Native build cache regression tests; no project data is modified."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_pc", ROOT / "tools/native/build_pc.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class DependencyTests(unittest.TestCase):
    def test_generated_data_invalidates_object(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "wild_encounter.c"
            obj = root / "wild_encounter.o"
            header = root / "src/data/wild_encounters.h"
            header.parent.mkdir(parents=True)
            for path in [source, header, obj]:
                path.write_text("test")
                os.utime(path, ns=(1000000000, 1000000000))
            dependencies = BUILD.shared_dependencies(root)
            self.assertIn(header, dependencies)
            # Ignore the real builder's own timestamp in this isolated fixture.
            dependencies = [p for p in dependencies if p.is_relative_to(root)]
            self.assertTrue(BUILD.object_is_current(obj, source, dependencies))
            os.utime(header, ns=(1000000001, 1000000001))
            self.assertFalse(BUILD.object_is_current(obj, source, dependencies))

    def test_missing_dependency_or_object_requires_rebuild(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, obj, missing = (root / name for name in ["source.c", "object.o", "gone.h"])
            source.touch()
            self.assertFalse(BUILD.object_is_current(obj, source, []))
            obj.touch()
            self.assertFalse(BUILD.object_is_current(obj, source, [missing]))


if __name__ == "__main__":
    unittest.main()