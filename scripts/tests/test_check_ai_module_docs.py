#!/usr/bin/env python3
"""Exercise real Git snapshots; no slicer build, hooks, graph or third-party packages."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "check_ai_module_docs.py"
spec = importlib.util.spec_from_file_location("module_docs", TOOL)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class ModuleDocsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Docs Test")
        self.git("config", "user.email", "docs@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.hooksPath", str(self.root / "no-hooks"))
        self.write("src/main.cpp", "int main() {}\n")
        self.config = {"version": 1, "exclude": [{"paths": ["docs/ai/**"], "reason": "文档自身不进入源码指纹"}],
                       "modules": [{"id": "core", "doc": "docs/ai/modules/core.md", "sources": ["src/**"],
                                    "watch": [], "tests": [], "test_note": "测试夹具", "entrypoints": ["src/main.cpp"]}]}
        self.save_config()
        self.review("首次人工核对接口和约束")
        self.git("add", ".")
        self.git("commit", "-qm", "baseline")
        self.base = self.git("rev-parse", "HEAD").strip()

    def write(self, path, text):
        out = self.root / path
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.PIPE).decode().strip()

    def save_config(self):
        self.write(check.MANIFEST, json.dumps(self.config, ensure_ascii=False))

    def review(self, reason, module_index=0):
        module = self.config["modules"][module_index]
        snapshot = check.Snapshot(self.root)
        meta = {"id": module["id"], "source_fingerprint": check.fingerprint(module, snapshot), "reason": reason}
        self.write(module["doc"], "# Core\n\n<!-- module-review\n" + json.dumps(meta) + "\n-->\n\n" +
                   "\n\n".join("## " + h + "\n\n此处描述测试夹具的有效接口与行为边界。" for h in check.HEADINGS))

    def run_check(self, *args):
        run = subprocess.run([os.sys.executable, str(TOOL), "--root", str(self.root), *args],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return run.returncode, (run.stdout + run.stderr).decode()

    def assert_code(self, expected, *args):
        code, output = self.run_check(*args)
        self.assertEqual(expected, code, output)
        return output

    def test_all_and_no_index_required(self):
        self.assert_code(0, "--all")
        self.assertFalse((self.root / ".codegraph").exists())

    def test_deep_chinese_spaces_and_rename_delete(self):
        original = 'src/新目录/deep/a quoted file.cpp' if os.name == "nt" else 'src/新目录/deep/a "quoted" file.cpp'
        self.write(original, "// 新文件\n")
        self.assert_code(1)
        self.review("增加内部辅助文件，入口不变")
        self.assert_code(0)
        self.git("add", ".")
        self.git("commit", "-qm", "new")
        self.git("mv", original, "src/重命名.cpp")
        self.assert_code(1, "--staged")
        self.review("辅助文件改名，调用关系不变")
        self.git("add", "docs")
        self.assert_code(0, "--staged")
        self.git("commit", "-qm", "rename")
        self.git("rm", "src/重命名.cpp")
        self.assert_code(1)
        self.review("移除辅助文件，入口仍保留")
        self.git("add", "docs")
        self.assert_code(0, "--staged")

    def test_unmapped_directory_and_bad_overlap(self):
        self.write("new_module/code.py", "print(1)\n")
        self.assertIn("未归属", self.assert_code(1))
        self.config["exclude"].append({"paths": ["src/**"], "reason": "错误排除"})
        self.save_config()
        self.assert_code(2)

    def test_partial_staging_reads_index_docs(self):
        self.write("src/main.cpp", "int main() { return 0; }\n")
        self.git("add", "src")
        self.review("函数显式返回，已复核无需修改正文")
        self.assert_code(0, "--all")
        self.assert_code(1, "--staged")
        self.git("add", "docs")
        self.assert_code(0, "--staged")
        self.write("src/main.cpp", "int main() { return 2; }\n")
        self.assert_code(0, "--staged")
        self.assert_code(1, "--all")

    def test_shared_parameter_watch_and_partial_review(self):
        self.write("config/defaults.hpp", "const int layers = 4;\n")
        self.write("other/entry.cpp", "other\n")
        for mid, source in (("parameters", "config/defaults.hpp"), ("other", "other/entry.cpp")):
            self.config["modules"].append({"id": mid, "doc": "docs/ai/modules/" + mid + ".md",
                "sources": [source], "watch": [], "tests": [], "entrypoints": [source],
                "test_note": "共享配置测试夹具"})
        self.config["modules"][0]["watch"] = ["config/defaults.hpp"]
        self.save_config()
        for index in range(3):
            self.review("核对模块和共享参数依赖", index)
        self.git("add", ".")
        self.git("commit", "-qm", "shared config")
        self.write("config/defaults.hpp", "const int layers = 5;\n")
        self.git("add", "config")
        current = check.Snapshot(self.root, "index")
        self.assertEqual({"core", "parameters"}, check.affected(
            current, check.Snapshot(self.root, "tree", "HEAD"), check.manifest(current)))
        output = self.assert_code(1, "--staged")
        self.assertIn("core.md: 源码变化", output)
        self.assertIn("parameters.md: 源码变化", output)
        self.review("配置默认层数已修改", 1)
        self.git("add", "docs/ai/modules/parameters.md")
        self.assertIn("core.md: 源码变化", self.assert_code(1, "--staged"))
        self.review("复核受影响默认层数和流程")
        self.assert_code(0, "--all")
        self.assert_code(1, "--staged")
        self.git("add", "docs/ai/modules/core.md")
        self.assert_code(0, "--staged")

    def test_missing_watch_is_not_silently_ignored(self):
        self.config["modules"][0]["watch"] = ["src/missing-config.hpp"]
        self.save_config()
        self.review("测试不存在的依赖仍必须报告")
        self.assertIn("测试/依赖范围不存在", self.assert_code(1))

    def test_literal_and_glob_matching_equivalence(self):
        paths = {"src/a.cpp", "src/deep/b.cpp", "src/中文 [x].hpp", "src/other.h", "scripts/test.py"}
        for patterns in (["src/a.cpp"], ["src/*"], ["src/**"], ["src/中文 [x].hpp"],
                         ["src/*.cpp", "src/deep/**", "src/a.cpp"], [], ["no-file"]):
            expected = {p for p in paths if any(check.path_pattern(pattern).fullmatch(p) for pattern in patterns)}
            self.assertEqual(expected, check.matches(patterns, iter(paths)))
        with self.assertRaises(check.CheckError):
            check.matches(["../invalid"], [])

    def test_snapshot_caches_do_not_cross_content_or_branch(self):
        first = check.Snapshot(self.root, "index")
        original = first.read("src/main.cpp")
        first.matching(["src/**"])
        self.write("src/new.cpp", "new\n")
        self.write("src/main.cpp", "changed\n")
        self.git("add", "src")
        second = check.Snapshot(self.root, "index")
        self.assertEqual(original, first.read("src/main.cpp"))
        self.assertEqual("changed\n", second.read("src/main.cpp"))
        self.assertNotIn("src/new.cpp", first.matching(["src/**"]))
        self.assertIn("src/new.cpp", second.matching(["src/**"]))
        self.assertTrue(second.exists("src/"))
        self.assertFalse(second.exists("sr"))
        self.assertFalse(first.exists("src/new.cpp"))
        self.git("reset", "--hard", "HEAD")
        third = check.Snapshot(self.root, "tree", "HEAD")
        self.assertEqual(original, third.read("src/main.cpp"))
        self.assertNotIn("src/new.cpp", third.matching(["src/**"]))

    def test_date_only_does_not_pass(self):
        self.write("src/main.cpp", "int main() { return 1; }\n")
        path = self.root / "docs/ai/modules/core.md"
        path.write_text(path.read_text() + "\n日期更新：2099-01-01\n")
        self.assert_code(1)

    def test_branch_switch_mtime_and_base_snapshot(self):
        self.git("checkout", "-qb", "feature")
        self.write("src/main.cpp", "int main() { return 1; }\n")
        self.review("返回值调整，说明复核完成")
        self.git("add", ".")
        self.git("commit", "-qm", "feature")
        self.assert_code(0, "--base", self.base)
        self.write("src/main.cpp", "uncommitted\n")
        self.assert_code(0, "--base", self.base)
        self.assert_code(1)
        self.git("restore", "src/main.cpp")
        self.git("checkout", "-q", self.base)
        os.utime(self.root / "src/main.cpp", (1, 1))
        self.assert_code(0)
        self.git("checkout", "-q", "feature")
        self.assert_code(0)

    def test_fingerprints_read_only(self):
        before = self.git("status", "--porcelain")
        output = self.assert_code(0, "--fingerprints")
        self.assertTrue(json.loads(output)["core"].startswith("sha256:"))
        self.assertEqual(before, self.git("status", "--porcelain"))

    def test_missing_entry_link_structure_and_document(self):
        doc = self.root / "docs/ai/modules/core.md"
        doc.write_text(doc.read_text() + "\n[missing](../no-such.md)\n")
        self.assert_code(1)
        doc.unlink()
        self.assertIn("缺少模块文档", self.assert_code(1))
        self.config["modules"][0]["entrypoints"] = ["src/missing.cpp"]
        self.save_config()
        self.assertIn("入口不存在", self.assert_code(1))

    def test_invalid_manifest_and_ref(self):
        self.assert_code(2, "--base", "no-such-ref")
        self.config["modules"][0]["sources"] = ["../outside/**"]
        self.save_config()
        self.assert_code(2)

    def test_broken_json(self):
        self.write(check.MANIFEST, "{")
        self.assert_code(2)

    def test_invalid_schema_returns_two_without_traceback(self):
        for bad in [[], {"version": 1, "modules": [None]}, {**self.config, "exclude": "bad"}]:
            self.write(check.MANIFEST, json.dumps(bad))
            output = self.assert_code(2)
            self.assertNotIn("Traceback", output)

    def test_markdown_anchor_and_reference_link(self):
        doc = self.root / "docs/ai/modules/core.md"
        original = doc.read_text()
        doc.write_text(original + "\n[flow](#核心流程)\n[reference][r]\n\n[r]: ../modules/core.md#职责与边界\n")
        self.assert_code(0)
        doc.write_text(doc.read_text() + "\n[missing](#并不存在)\n")
        self.assertIn("锚点不存在", self.assert_code(1))

    def test_index_manifest_does_not_use_unstaged_fix(self):
        self.write("new/code.py", "print(1)\n")
        self.git("add", "new")
        self.config["modules"][0]["sources"].append("new/**")
        self.save_config()
        self.review("新增代码归属，复核接口")
        self.assert_code(0)
        self.assertIn("未归属", self.assert_code(1, "--staged"))

    def test_cross_module_rename_checks_old_owner(self):
        self.write("src/helper.cpp", "helper\n")
        self.review("初始辅助文件")
        self.git("add", ".")
        self.git("commit", "-qm", "helper")
        self.git("mv", "src/helper.cpp", "helper.cpp")
        self.config["exclude"].append({"paths": ["helper.cpp"], "reason": "移到非业务示例"})
        self.save_config()
        self.git("add", "docs")
        self.assertIn("需复核", self.assert_code(1, "--staged"))

    def test_git_normalization_and_file_timestamp(self):
        self.write(".gitattributes", "*.cpp text eol=lf\n")
        self.config["exclude"].append({"paths": [".gitattributes"], "reason": "夹具属性"})
        self.save_config()
        self.git("add", ".")
        self.git("commit", "-qm", "attributes")
        before = self.run_check("--fingerprints")[1]
        (self.root / "src/main.cpp").write_bytes(b"int main() {}\r\n")
        os.utime(self.root / "src/main.cpp", (7, 7))
        self.assertEqual(before, self.run_check("--fingerprints")[1])
        self.assert_code(0)

    def test_legacy_blob_predating_text_attributes(self):
        source = self.root / "src/main.cpp"
        source.write_bytes(b"int main() { return 1; }\r\n")
        self.review("已有 CRLF 源文件，核对原始内容")
        self.git("add", ".")
        self.git("commit", "-qm", "legacy CRLF blob")
        self.write(".gitattributes", "*.cpp text eol=lf\n")
        self.config["exclude"].append({"paths": [".gitattributes"], "reason": "后来增加的属性"})
        self.save_config()
        self.git("add", ".gitattributes", "docs")
        self.assert_code(0, "--all")
        self.assert_code(0, "--staged")
        self.assertEqual(check.Snapshot(self.root).entries["src/main.cpp"],
                         check.Snapshot(self.root, "index").entries["src/main.cpp"])
        old_stat = source.stat()
        source.write_bytes(b"int main() { return 2; }\r\n")
        os.utime(source, ns=(old_stat.st_atime_ns, old_stat.st_mtime_ns))
        self.assert_code(1, "--all")
        self.assert_code(0, "--staged")
        self.review("返回值已变化，同时间戳也必须复核；按当前 Git 属性入库")
        self.git("add", "src", "docs")
        self.assert_code(0, "--all")
        self.assert_code(0, "--staged")

    def test_required_structure_and_review_reason(self):
        doc = self.root / "docs/ai/modules/core.md"
        original = doc.read_text()
        doc.write_text(original.replace("## 核心流程", "## 无效标题"))
        self.assert_code(1)
        found = check.META.search(original)
        metadata = json.loads(found.group(1))
        metadata["reason"] = ""
        doc.write_text(original.replace(found.group(1), json.dumps(metadata)))
        self.assert_code(1)

    def test_duplicate_module_mapping_is_configuration_error(self):
        second = dict(self.config["modules"][0], id="other", doc="docs/ai/modules/other.md")
        self.config["modules"].append(second)
        self.save_config()
        self.assertIn("重复归属", self.assert_code(2))

    def test_ignored_docs_tree_only_allows_ai(self):
        self.write(".gitignore", "docs\n!/docs/\n/docs/*\n!/docs/ai/\n!/docs/ai/**\n")
        self.config["exclude"].append({"paths": [".gitignore"], "reason": "夹具忽略规则"})
        self.save_config()
        self.write("docs/local/note.md", "local only\n")
        self.write("docs/ai/new/deep.md", "# tracked candidate\n")
        snapshot = check.Snapshot(self.root)
        self.assertNotIn("docs/local/note.md", snapshot.entries)
        self.assertIn("docs/ai/new/deep.md", snapshot.entries)
        self.assert_code(0)

    def test_document_requires_only_available_git(self):
        # The test repo has no graph configuration/database; a failing graph CLI
        # on PATH must never be invoked by any checking mode.
        bin_dir = self.root / "fake-bin"
        bin_dir.mkdir()
        marker = self.root / "graph-was-run"
        if os.name != "nt":
            cli = bin_dir / "codegraph"
            cli.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\nexit 55\n')
            cli.chmod(0o755)
            env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ.get("PATH", ""))
            result = subprocess.run([os.sys.executable, str(TOOL), "--root", str(self.root), "--staged"],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertFalse(marker.exists())

    @unittest.skipIf(os.name == "nt", "Windows symlink privilege is not assumed")
    def test_symlink_snapshot_does_not_read_external_content(self):
        outside = self.root.parent / (self.root.name + "-outside.txt")
        outside.write_text("outside\n")
        self.addCleanup(outside.unlink)
        os.symlink(str(outside), self.root / "src/link.hpp")
        self.review("新增链接，指纹记录链接目标")
        before = self.run_check("--fingerprints")[1]
        outside.write_text("changed outside\n")
        self.assertEqual(before, self.run_check("--fingerprints")[1])
        self.assert_code(0)


if __name__ == "__main__":
    unittest.main()
