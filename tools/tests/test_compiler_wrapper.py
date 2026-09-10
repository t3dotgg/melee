import unittest
from pathlib import Path
from unittest.mock import patch

from tools.download_tool import wibo_url
from tools.project import ProjectConfig


class CompilerWrapperTests(unittest.TestCase):
    def setUp(self):
        self.config = ProjectConfig()
        self.config.build_dir = Path("build")
        self.config.wibo_tag = "1.1.0"

    def test_supported_hosts_download_a_local_wrapper(self):
        hosts = (
            ("darwin", "arm64"),
            ("darwin", "x86_64"),
            ("linux", "i386"),
            ("linux", "x86_64"),
        )
        for system, machine in hosts:
            with self.subTest(system=system, machine=machine):
                with (
                    patch("tools.project.sys.platform", system),
                    patch("tools.project.platform.machine", return_value=machine),
                    patch("tools.project.is_windows", return_value=False),
                ):
                    self.assertTrue(self.config.use_wibo())
                    self.assertEqual(
                        self.config.compiler_wrapper(), Path("build/tools/wibo")
                    )

    def test_explicit_wrapper_disables_automatic_download(self):
        self.config.wrapper = Path("/custom/wine")
        with (
            patch("tools.project.sys.platform", "darwin"),
            patch("tools.project.platform.machine", return_value="arm64"),
        ):
            self.assertFalse(self.config.use_wibo())
            self.assertEqual(self.config.compiler_wrapper(), self.config.wrapper)

    def test_unsupported_hosts_keep_wine_fallback(self):
        for system, machine in (
            ("linux", "aarch64"),
            ("linux", "arm64"),
            ("linux", "ppc64le"),
            ("darwin", "ppc"),
        ):
            with self.subTest(system=system, machine=machine):
                with (
                    patch("tools.project.sys.platform", system),
                    patch("tools.project.platform.machine", return_value=machine),
                    patch("tools.project.is_windows", return_value=False),
                ):
                    self.assertFalse(self.config.use_wibo())
                    self.assertEqual(self.config.compiler_wrapper(), Path("wine"))

    def test_windows_runs_compilers_without_a_wrapper(self):
        with (
            patch("tools.project.sys.platform", "win32"),
            patch("tools.project.is_windows", return_value=True),
        ):
            self.assertFalse(self.config.use_wibo())
            self.assertIsNone(self.config.compiler_wrapper())

    def test_unset_version_disables_automatic_download(self):
        self.config.wibo_tag = None
        with (
            patch("tools.project.sys.platform", "darwin"),
            patch("tools.project.platform.machine", return_value="arm64"),
        ):
            self.assertFalse(self.config.use_wibo())

    def test_download_uses_platform_specific_release_asset(self):
        with patch("tools.download_tool.platform.system", return_value="Darwin"):
            self.assertEqual(
                wibo_url("1.1.0"),
                "https://github.com/decompals/wibo/releases/download/1.1.0/wibo-macos",
            )
        with patch("tools.download_tool.platform.system", return_value="Linux"):
            self.assertEqual(
                wibo_url("0.7.0"),
                "https://github.com/decompals/wibo/releases/download/0.7.0/wibo",
            )


if __name__ == "__main__":
    unittest.main()
