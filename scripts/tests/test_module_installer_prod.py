"""Exercise per-device module installers using only ordinary Lua IO."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('module_prod_builder', ROOT / 'scripts/build_module_installer_prod.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class ModuleInstallerTests(unittest.TestCase):
    def test_exact_target_config(self):
        for product in builder.PRODUCTS:
            for target in builder.SUPPORTED[2:]:
                source, config = builder.render(product, [target])
                self.assertEqual(len(config['targets']), 1)
                self.assertFalse(config['targets'][target.rsplit('-', 1)[1]]['runtime_pending'])
            source, config = builder.render(product, builder.SUPPORTED[2:])
            self.assertEqual(set(config['targets']), {'4.100.139', '4.100.155'})
            if product == 'resource-hook':
                self.assertEqual(config['token'], 'resource_hook')
                self.assertEqual(config['assets'], [])
                with self.assertRaises(ValueError):
                    builder.render(product, builder.SUPPORTED[:2])
            else:
                source, config = builder.render(product, builder.SUPPORTED[:2])
                self.assertEqual(len(config['targets']), 2)
                self.assertFalse(config['targets']['3.101.043']['runtime_pending'])
            self.assertNotIn('-- @', source)
            with self.assertRaises(ValueError):
                builder.render(product, ['xiaomi-band-11-4.100.108'])
            with self.assertRaises(ValueError):
                builder.render(product, builder.SUPPORTED)

    def test_installer_protocol_with_restricted_lua(self):
        interpreter = os.environ.get('CANOPUS_TEST_LUA', 'lua')
        if not shutil.which(interpreter):
            self.skipTest('Lua interpreter required')
        with tempfile.TemporaryDirectory(prefix='canopus-module-prod-test-') as temp:
            for product in builder.PRODUCTS:
                target_groups = (builder.SUPPORTED[2:],) if product == 'resource-hook' else (
                    builder.SUPPORTED[:2], builder.SUPPORTED[2:])
                for targets in target_groups:
                    source, _ = builder.render(product, targets)
                    for forbidden in ('os.execute', 'debug.', 'pmain', 'RECOVERY_PROFILE', '/dev/canopus'):
                        self.assertNotIn(forbidden, source)
                    entry = Path(temp) / 'main.lua'
                    entry.write_text(source)
                    subprocess.run([interpreter, str(ROOT / 'scripts/lua/test_module_installer_prod.lua'),
                                    str(entry), product], cwd=ROOT, check=True)

    def test_unsigned_receipt_is_rejected(self):
        if not shutil.which('openssl'):
            self.skipTest('OpenSSL required')
        with self.assertRaises(subprocess.CalledProcessError):
            builder.verify_signature(bytes(256))


if __name__ == '__main__':
    unittest.main()
