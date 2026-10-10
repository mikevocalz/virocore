"""Exercise artifact staging without an Xcode installation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FrameworkArtifactTest(unittest.TestCase):
    def run_build(self, produce_binary=True):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        (root / 'scripts').mkdir()
        (root / 'ios').mkdir()
        (root / 'bin').mkdir()
        shutil.copy2(ROOT / 'scripts/build_ios_framework.sh', root / 'scripts')
        (root / 'ios/ViroKit.podspec').write_text('podspec')
        stale = root / 'ios/dist/ViroKit.framework/stale.h'
        stale.parent.mkdir(parents=True)
        stale.write_text('old header')
        xcode = root / 'bin/xcodebuild'
        xcode.write_text('''#!/bin/bash
set -eu
while [ "$#" -gt 0 ]; do
  if [ "$1" = "-derivedDataPath" ]; then build_dir="$2"; shift; fi
  shift
done
framework="$build_dir/Build/Products/Release-iphoneos/ViroKit.framework"
mkdir -p "$framework/Headers"
if [ "$PRODUCE_BINARY" = "yes" ]; then printf binary > "$framework/ViroKit"; fi
''')
        xcode.chmod(0o755)
        ditto = root / 'bin/ditto'
        ditto.write_text('#!/bin/bash\nset -eu\ncp -R "$1" "$2"\n')
        ditto.chmod(0o755)
        result = subprocess.run(
            ['bash', str(root / 'scripts/build_ios_framework.sh')],
            env={**os.environ, 'PATH': f"{root / 'bin'}:{os.environ['PATH']}",
                 'PRODUCE_BINARY': 'yes' if produce_binary else 'no'},
            capture_output=True, text=True,
        )
        return root, result

    def test_stages_built_binary_and_removes_stale_headers(self):
        root, result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((root / 'ios/dist/ViroKit.framework/ViroKit').read_text(), 'binary')
        self.assertTrue((root / 'ios/dist/ViroKit.podspec').is_file())
        self.assertFalse((root / 'ios/dist/ViroKit.framework/stale.h').exists())

    def test_jenkins_does_not_call_retired_lane(self):
        self.assertNotIn('fastlane virorender_viroreact_virokit_static_lib',
                         (ROOT / 'Jenkinsfile').read_text())

    def test_successful_xcode_exit_without_binary_fails_packaging(self):
        _, result = self.run_build(produce_binary=False)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
