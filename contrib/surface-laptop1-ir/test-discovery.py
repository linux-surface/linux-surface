# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("launcher", Path(__file__).with_name("surface-laptop1-ir.py"))
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

GRAPH = """driver          ipu3-cio2
- entity 3: ov7251 2-0060 (1 pad, 1 link)
   pad0: SOURCE
      -> "ipu3-csi2 2":0 []
- entity 19: ipu3-csi2 2 (2 pads, 2 links)
   pad1: SOURCE
      -> "ipu3-cio2 2":0 [ENABLED]
- entity 26: ipu3-cio2 2 (1 pad, 1 link)
"""

class Discovery(unittest.TestCase):
    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        self.dmi, self.video, self.dev = (root / s for s in ("dmi","v4l","dev"))
        for p in (self.dmi,self.video,self.dev): p.mkdir()
        for name,value in (("sys_vendor","Microsoft Corporation"),("product_name","Surface Laptop"),
                           ("product_sku","Surface_Laptop")):
            (self.dmi / name).write_text(value + "\n")
        self.hid = self.video / "v4l-subdev8/device/firmware_node/hid"
        self.hid.parent.mkdir(parents=True)
        self.hid.write_text("INT347E\n")
        self.media = self.dev / "media7"
        self.media.touch()

    def run_discovery(self, graph=GRAPH):
        mapping = {"ipu3-cio2 2":"/dev/video23", "ov7251 2-0060":"/dev/v4l-subdev8",
                   "ipu3-csi2 2":"/dev/v4l-subdev18"}
        def read(media, op, entity=None):
            self.assertEqual(media,self.media)
            if op == "-p": return graph
            self.assertEqual(op, "-e")  # No link or format writes during discovery.
            return mapping[entity]
        with mock.patch.object(launcher,"media_ctl",side_effect=read):
            return launcher.discover(self.dmi,self.video,self.dev)

    def test_disabled_link_dynamic_nodes(self):
        self.assertEqual(self.run_discovery(), ("/dev/video23","/dev/v4l-subdev8",str(self.media),
                                                 "/dev/v4l-subdev18","3","19"))

    def test_non_surface_and_shared_sl2_subsystem(self):
        (self.dmi / "product_sku").write_text("Surface_Laptop_2")
        with self.assertRaisesRegex(ValueError, "first-generation"): self.run_discovery()
        (self.dmi / "sys_vendor").unlink()
        with self.assertRaises(ValueError): self.run_discovery()

    def test_unrelated_controller_or_firmware(self):
        with self.assertRaisesRegex(ValueError, "Expected one"):
            self.run_discovery(GRAPH.replace("driver          ipu3-cio2","driver          ipu3-imgu"))
        self.hid.write_text("INT347A")
        with self.assertRaisesRegex(ValueError, "Expected one"): self.run_discovery()

    def test_missing_topology(self):
        with self.assertRaisesRegex(ValueError, "Expected one"):
            self.run_discovery(GRAPH.replace("- entity 26: ipu3-cio2 2","- entity 26: other"))

if __name__ == "__main__": unittest.main()
