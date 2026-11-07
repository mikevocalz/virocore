"""Source regression gate only; the AAR job provides actual native compilation."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class PostOpaqueContract(unittest.TestCase):
    def test_every_base_path_calls_hook_before_postprocessing(self):
        source = (ROOT / 'ViroRenderer/VROChoreographer.cpp').read_text()
        body = source.split('void VROChoreographer::renderScene(', 1)[1].split('void VROChoreographer::setClearColor(', 1)[0]
        base = '_baseRenderPass->render(scene, outgoingScene, inputs, context, driver);'
        hook = '_customPostOpaquePass->render(scene, outgoingScene, inputs, context, driver);'
        self.assertEqual(body.count(base), 5, 'Review new render paths explicitly')
        self.assertEqual(body.count(hook), 5)
        for suffix in body.split(base)[1:]:
            self.assertRegex(suffix, r'^\s*if \(_customPostOpaquePass\) \{\s*' + re.escape(hook) + r'\s*\}')

    def test_attach_detach_public_surface(self):
        header = (ROOT / 'ViroRenderer/VROChoreographer.h').read_text()
        self.assertIn('void setCustomPostOpaquePass(std::shared_ptr<VRORenderPass> pass)', header)
        self.assertIn('_customPostOpaquePass = pass;', header)
        self.assertIn('std::shared_ptr<VRORenderPass> getCustomPostOpaquePass() const', header)

if __name__ == '__main__':
    unittest.main()
