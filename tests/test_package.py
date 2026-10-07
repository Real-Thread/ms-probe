import glob
import importlib.util
import json
from pathlib import Path
import re
import runpy
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPDX = "SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-Commercial"
TARGETS = (
    ("stm32f407-rt-spark", "SOC_STM32F407ZG", "RT_SCOPE_STM32F407_PORT", "gcc"),
    ("qemu-mps3-an536", "SOC_QEMU_MPS3_AN536", "RT_SCOPE_QEMU_MPS3_AN536_PORT", "gcc"),
    ("kf32a158-evb", "SOC_KF32A158", "RT_SCOPE_KF32A158_PORT", "kf32-gcc"),
    ("s32k3-core", "SOC_FAMILY_S32K3", "RT_SCOPE_S32K3_PORT", "gcc"),
)


class PackageTests(unittest.TestCase):
    def test_metadata(self):
        metadata = json.loads((ROOT / "package.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["name"], "ms-probe")
        self.assertEqual(metadata["enable"], "PKG_USING_MS_PROBE")
        self.assertEqual(metadata["license"], "GPL-2.0")
        self.assertEqual(metadata["category"], "tools")
        self.assertIn("probe", metadata["keywords"])
        self.assertNotIn("backtrace", metadata["keywords"])
        self.assertNotIn("coredump", metadata["keywords"])
        self.assertEqual(metadata["repository"], "https://github.com/Real-Thread/ms-probe")
        self.assertEqual(metadata["site"], [{
            "version": "latest",
            "URL": "https://github.com/Real-Thread/ms-probe.git",
            "filename": "ms-probe.zip",
            "VER_SHA": "main",
        }])

    def test_dual_license_notices(self):
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        self.assertIn("GNU GENERAL PUBLIC LICENSE", license_text)
        self.assertIn("Version 2, June 1991", license_text)
        commercial = (ROOT / "LICENSES/LicenseRef-Commercial.txt").read_text(encoding="utf-8")
        self.assertIn("business@rt-thread.com", commercial)
        self.assertIn("separate written", commercial)
        sources = [path for path in ROOT.rglob("*") if path.suffix in {".c", ".h", ".S"}]
        self.assertTrue({ROOT / "include/ms_probe.h", ROOT / "src/ms_probe.c",
                         ROOT / "ports/ms_scope_baremetal.h",
                         ROOT / "ports/ms_scope_baremetal.c"}.issubset(sources))
        for path in sources:
            with self.subTest(path=path.relative_to(ROOT)):
                text = path.read_text(encoding="utf-8")
                self.assertEqual(text.count(SPDX), 1)
                self.assertNotIn("SPDX-License-Identifier: Apache-2.0", text)
                self.assertNotIn("expressly prohibited", text)
                self.assertNotIn("Republication, copying or redistribution", text)

    def test_document_links_are_package_local(self):
        links = set()
        for path in ROOT.rglob("*.md"):
            for target in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
                target = target.split("#", 1)[0]
                if not target or re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                    continue
                with self.subTest(document=path.relative_to(ROOT), target=target):
                    resolved = (path.parent / target).resolve()
                    self.assertTrue(resolved.is_relative_to(ROOT), "Link depends on the parent repo")
                    self.assertTrue(resolved.exists(), "Link target is missing")
                    links.add(resolved.relative_to(ROOT).as_posix())
        self.assertTrue({"LICENSE", "LICENSES/LicenseRef-Commercial.txt",
                         "tests/qemu-vexpress-a9/README.md",
                         "docs/limitations.md"}.issubset(links))

    def _groups(self, enabled, platform="gcc"):
        current = ROOT
        config = SimpleNamespace(ARCH="arm", CPU="cortex-m4", PLATFORM=platform)
        building = ModuleType("building")

        def get_depend(dependencies):
            if isinstance(dependencies, str):
                dependencies = [dependencies]
            return all(item in enabled or not item for item in dependencies)

        def define_group(name, sources, depend, **options):
            if not get_depend(depend):
                return []
            return [{"name": name, "sources": sources, **options}]

        def sconscript(path):
            nonlocal current
            script = current / path
            previous = current
            current = script.parent
            try:
                return runpy.run_path(str(script), init_globals={"rtconfig": config})["group"]
            finally:
                current = previous

        building.GetCurrentDir = lambda: str(current)
        building.GetDepend = get_depend
        building.DefineGroup = define_group
        building.Glob = lambda pattern: sorted(glob.glob(str(current / pattern)))
        building.Import = lambda _name: None
        building.Return = lambda _name: None
        building.SConscript = sconscript
        with patch.dict(sys.modules, {"building": building}):
            return sconscript("SConscript")

    def test_scons_disabled_has_no_sources(self):
        self.assertEqual(self._groups(set()), [])

    def test_scons_generic_probe_is_package_local(self):
        groups = self._groups({"RT_USING_SCOPE"})
        self.assertEqual([item["name"] for item in groups], ["MicroscopeScope"])
        self.assertEqual(set(map(Path, groups[0]["sources"])), {
            ROOT / "src/ms_probe.c", ROOT / "ports/ms_scope_baremetal.c",
        })

    def test_package_contains_only_probe_components(self):
        for name in ("arch", "backtrace", "coredump"):
            with self.subTest(directory=name):
                self.assertFalse((ROOT / name).exists())
        retired_header = re.compile(
            r'^\s*#\s*include\s*[<"](?:rt_backtrace|backtrace_internal|'
            r'coredump|coredump_arch|elf_define)\.h[>"]', re.MULTILINE,
        )
        for directory in ("include", "src", "ports"):
            for path in (ROOT / directory).rglob("*"):
                if path.suffix in {".c", ".h", ".S"}:
                    with self.subTest(source=path.relative_to(ROOT)):
                        self.assertIsNone(retired_header.search(path.read_text(encoding="utf-8")))

    def test_retired_component_switches_do_not_add_sources(self):
        switches = {"RT_USING_STACK_BACKTRACE", "RT_USING_COREDUMP",
                    "RT_USING_COREDUMP_TESTCASE"}
        self.assertEqual(self._groups(switches), [])
        groups = self._groups(switches | {"RT_USING_SCOPE"})
        self.assertEqual([item["name"] for item in groups], ["MicroscopeScope"])
        for name, _soc, port, platform in TARGETS:
            with self.subTest(port=name):
                enabled = {"RT_USING_SCOPE", port}
                self.assertEqual(self._groups(enabled | switches, platform),
                                 self._groups(enabled, platform))

    def test_scons_selects_only_the_matching_port(self):
        for name, _soc, symbol, platform in TARGETS:
            with self.subTest(port=name):
                groups = self._groups({"RT_USING_SCOPE", symbol}, platform)
                self.assertEqual({item["name"] for item in groups}, {
                    "MicroscopeScope", "MicroscopePort-" + name,
                })
                port = next(item for item in groups if item["name"].startswith("MicroscopePort-"))
                self.assertEqual(len(port["sources"]), 3)
                for group in groups:
                    for source in group["sources"]:
                        self.assertTrue(Path(source).is_file())
                        self.assertTrue(Path(source).is_relative_to(ROOT))
                if name != "qemu-mps3-an536":
                    self.assertIn("linker_scripts/ms_scope.ld", port["LINKFLAGS"])
                mismatch = self._groups({"RT_USING_SCOPE", symbol}, "unsupported")
                self.assertEqual([item["name"] for item in mismatch], ["MicroscopeScope"])

    @unittest.skipUnless(importlib.util.find_spec("kconfiglib"), "Install tests/requirements.txt")
    def test_package_and_legacy_kconfig_switches(self):
        import kconfiglib

        for name, soc, port, _platform in TARGETS:
            with self.subTest(port=name), tempfile.TemporaryDirectory() as temporary:
                wrapper = Path(temporary) / "Kconfig"
                symbols = [item[1] for item in TARGETS] + [
                    "BSP_USING_USB_TO_USART", "BSP_USING_UART", "BSP_USING_UART0",
                    "BSP_USING_UART3", "BOARD_S32K3X4_EVB", "RT_USING_MSH",
                ]
                definitions = "\n".join(
                    'config {}\n    bool\n    default {}\n'.format(item, "y" if item == soc else "n")
                    for item in symbols
                )
                wrapper.write_text(definitions + '\nrsource "{}"\n'.format(ROOT / "Kconfig"), encoding="utf-8")
                config = kconfiglib.Kconfig(str(wrapper), warn=False)
                for symbol in ("PKG_USING_MS_PROBE", "RT_USING_MICROSCOPE", "RT_USING_SCOPE"):
                    self.assertEqual(config.syms[symbol].str_value, "n")
                for symbol in ("RT_USING_STACK_BACKTRACE", "RT_STACK_BACKTRACE_DEPTH_MAX",
                               "RT_USING_COREDUMP", "RT_USING_COREDUMP_TESTCASE",
                               "RT_COREDUMP_ARCH_ARMV7M", "RT_COREDUMP_ARCH_TRICORE",
                               "RT_COREDUMP_ARCH_AARCH64"):
                    self.assertNotIn(symbol, config.syms)
                config.syms["PKG_USING_MS_PROBE"].set_value("y")
                self.assertEqual(config.syms["RT_USING_MICROSCOPE"].str_value, "y")
                self.assertEqual(config.syms["PKG_MS_PROBE_PATH"].str_value, "/packages/tools/ms-probe")
                self.assertEqual(config.syms["PKG_MS_PROBE_VER"].str_value, "latest")
                self.assertEqual(config.syms["PKG_USING_MS_PROBE_LATEST_VERSION"].str_value, "y")
                config.syms["RT_USING_SCOPE"].set_value("y")
                self.assertEqual(config.syms[port].str_value, "y")
                self.assertEqual(config.syms["RT_SCOPE_MAX_PAYLOAD"].str_value, "256")
                self.assertEqual(config.syms["RT_SCOPE_USING_FAULT_INJECTION"].str_value, "n")
                config.syms["PKG_USING_MS_PROBE"].set_value("n")
                self.assertEqual(config.syms["RT_USING_MICROSCOPE"].str_value, "n")
                self.assertEqual(config.syms["RT_USING_SCOPE"].str_value, "n")
                self.assertEqual(config.syms[port].str_value, "n")
                config.syms["RT_USING_MICROSCOPE"].set_value("y")
                self.assertEqual(config.syms["RT_USING_SCOPE"].str_value, "y")
                self.assertEqual(config.syms[port].str_value, "y")

    @unittest.skipUnless(importlib.util.find_spec("kconfiglib"), "Install tests/requirements.txt")
    def test_index_can_load_options_without_duplicate_package_menu(self):
        import kconfiglib

        with tempfile.TemporaryDirectory() as temporary:
            wrapper = Path(temporary) / "Kconfig"
            wrapper.write_text(
                'config PKG_USING_MS_PROBE\n    bool "ms-probe"\n'
                'if PKG_USING_MS_PROBE\n'
                'rsource "{}"\nendif\n'.format(ROOT / "Kconfig.options"), encoding="utf-8",
            )
            config = kconfiglib.Kconfig(str(wrapper), warn=False)
            for symbol in ("RT_USING_STACK_BACKTRACE", "RT_STACK_BACKTRACE_DEPTH_MAX",
                           "RT_USING_COREDUMP", "RT_USING_COREDUMP_TESTCASE",
                           "RT_COREDUMP_ARCH_ARMV7M", "RT_COREDUMP_ARCH_TRICORE",
                           "RT_COREDUMP_ARCH_AARCH64"):
                self.assertNotIn(symbol, config.syms)
            config.syms["PKG_USING_MS_PROBE"].set_value("y")
            self.assertEqual(config.syms["RT_USING_MICROSCOPE"].str_value, "y")
            config.syms["RT_USING_SCOPE"].set_value("y")
            self.assertEqual(config.syms["RT_USING_SCOPE"].str_value, "y")
            self.assertEqual(len(config.syms["PKG_USING_MS_PROBE"].nodes), 1)
            config.syms["PKG_USING_MS_PROBE"].set_value("n")
            self.assertEqual(config.syms["RT_USING_MICROSCOPE"].str_value, "n")
            self.assertEqual(config.syms["RT_USING_SCOPE"].str_value, "n")


if __name__ == "__main__":
    unittest.main()
