#!/usr/bin/env python3
"""Read-only module documentation checks. Python standard library + Git only."""

import argparse
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

MANIFEST = "docs/ai/modules.json"
HEADINGS = ("职责与边界", "入口与调用", "核心流程", "配置与约束", "失败与取消",
            "修改与验证", "关联模块")
META = re.compile(r"<!-- module-review\s*(\{.*?\})\s*-->", re.S)


class CheckError(Exception):
    pass


def git(root, *args, data=None, optional=False):
    result = subprocess.run(["git", "-C", str(root), *args], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode and not optional:
        raise CheckError(result.stderr.decode("utf-8", "replace").strip())
    return result.stdout if result.returncode == 0 else b""


def decode(raw):
    return raw.decode("utf-8", "surrogateescape")


def quote_path(path):
    # Git's C-style quoting, including newlines and non-ASCII bytes.
    return b'"' + b"".join(bytes([c]) if 32 <= c < 127 and c not in (34, 92)
                            else ("\\%03o" % c).encode() for c in os.fsencode(path)) + b'"'


class Snapshot:
    def __init__(self, root, kind="worktree", ref=None):
        self.root, self.kind, self.entries = Path(root), kind, {}
        self._reads, self._matches = {}, {}
        if kind == "tree":
            raw = git(root, "ls-tree", "-rz", "--full-tree", ref)
            for record in raw.split(b"\0"):
                if record:
                    info, path = record.split(b"\t", 1)
                    mode, _, oid = info.split()
                    self.entries[decode(path)] = (mode.decode(), oid.decode())
        else:
            raw = git(root, "ls-files", "--stage", "-z")
            for record in raw.split(b"\0"):
                if record:
                    info, path = record.split(b"\t", 1)
                    mode, oid, stage = info.split()
                    if stage != b"0":
                        raise CheckError("暂存区有未解决的合并冲突")
                    self.entries[decode(path)] = (mode.decode(), oid.decode())
            if kind == "worktree":
                others = git(root, "ls-files", "--others", "--exclude-standard", "-z")
                for path in others.split(b"\0"):
                    if path:
                        self.entries[decode(path)] = ("100644", "")
                files = []
                for path in list(self.entries):
                    disk = self.root / path
                    if disk.is_symlink():
                        target = os.fsencode(os.readlink(disk))
                        algorithm = git(root, "rev-parse", "--show-object-format").strip().decode()
                        blob = ("blob %d\0" % len(target)).encode() + target
                        self.entries[path] = ("120000", hashlib.new(algorithm, blob).hexdigest())
                    elif disk.is_file():
                        files.append(path)
                    elif self.entries[path][0] != "160000":
                        del self.entries[path]
                if files:
                    raw_ids = git(root, "hash-object", "--no-filters", "--stdin-paths",
                              data=b"\n".join(quote_path(p) for p in files) + b"\n").splitlines()
                    if len(raw_ids) != len(files):
                        raise CheckError("Git 内容身份数量不匹配")
                    # Older blobs may predate current text/clean attributes. Preserve
                    # their identity when bytes match the index exactly; otherwise
                    # hash as Git would stage the current content. Never use mtimes.
                    changed = [p for p, oid in zip(files, raw_ids)
                               if oid.decode() != self.entries[p][1]]
                    ids = git(root, "hash-object", "--stdin-paths",
                              data=b"\n".join(quote_path(p) for p in changed) + b"\n").splitlines() if changed else []
                    if len(ids) != len(changed):
                        raise CheckError("Git 规范化内容身份数量不匹配")
                    for path, oid in zip(changed, ids):
                        # File mode follows the Git index; content follows the worktree.
                        self.entries[path] = (self.entries[path][0], oid.decode())
        self._directories = {parent.as_posix() for path in self.entries
                             for parent in Path(path).parents}

    def read(self, path):
        if path not in self.entries:
            raise CheckError("快照缺少文件: " + path)
        if self.kind == "worktree":
            return (self.root / path).read_bytes().decode("utf-8")
        # Index/tree snapshots are immutable for this invocation. Never share this
        # cache between snapshots or reuse it for live worktree reads.
        if path not in self._reads:
            self._reads[path] = git(self.root, "cat-file", "blob", self.entries[path][1]).decode("utf-8")
        return self._reads[path]

    def exists(self, path):
        return path in self.entries or path.rstrip("/") in self._directories

    def matching(self, patterns):
        key = tuple(patterns)
        if key not in self._matches:
            self._matches[key] = frozenset(matches(patterns, self.entries))
        return self._matches[key]


def path_pattern(pattern):
    if not isinstance(pattern, str) or not pattern or pattern.startswith("/") or ".." in pattern.split("/") or "\\" in pattern:
        raise CheckError("非法仓库相对路径: " + repr(pattern))
    # '*' never crosses directories; '**' does. Literal brackets are not glob syntax.
    return compiled_pattern(pattern)


@lru_cache(maxsize=4096)
def compiled_pattern(pattern):
    return re.compile("^" + re.escape(pattern).replace(r"\*\*", ".*").replace(r"\*", "[^/]*") + "$")


def matches(patterns, paths):
    paths = paths if isinstance(paths, (dict, set, frozenset)) else set(paths)
    result, expressions = set(), []
    for pattern in patterns:
        expression = path_pattern(pattern)
        if "*" in pattern:
            expressions.append(expression)
        elif pattern in paths:
            result.add(pattern)
    if expressions:
        result.update(p for p in paths if any(e.fullmatch(p) for e in expressions))
    return result


def manifest(snapshot):
    value = json.loads(snapshot.read(MANIFEST))
    if not isinstance(value, dict) or value.get("version") != 1 or not isinstance(value.get("modules"), list) or not value["modules"]:
        raise CheckError("模块清单 version/modules 无效")
    ids, docs = set(), set()
    for module in value["modules"]:
        if not isinstance(module, dict):
            raise CheckError("模块条目必须是对象")
        mid, doc = module.get("id", ""), module.get("doc", "")
        if not isinstance(mid, str) or not isinstance(doc, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", mid) or mid in ids or doc in docs:
            raise CheckError("模块 ID 或文档路径无效/重复: " + repr(mid))
        if not doc.startswith("docs/ai/modules/") or not doc.endswith(".md"):
            raise CheckError("模块文档必须位于 docs/ai/modules: " + doc)
        path_pattern(doc)
        ids.add(mid)
        docs.add(doc)
        for key in ("sources", "watch", "tests", "entrypoints"):
            if not isinstance(module.get(key), list):
                raise CheckError(mid + " 缺少路径列表 " + key)
            for pattern in module[key]:
                path_pattern(pattern)
        if not module["sources"] or not module["entrypoints"] or not module.get("test_note"):
            raise CheckError(mid + " 缺少来源、入口或验证边界")
    if not isinstance(value.get("exclude", []), list):
        raise CheckError("exclude 必须是列表")
    for exclusion in value.get("exclude", []):
        if not isinstance(exclusion, dict) or not isinstance(exclusion.get("paths"), list) or not exclusion["paths"] or not isinstance(exclusion.get("reason"), str) or not exclusion["reason"].strip():
            raise CheckError("排除项必须说明范围和原因")
        for pattern in exclusion["paths"]:
            path_pattern(pattern)
    return value


def source_paths(module, snapshot):
    return snapshot.matching(module["sources"] + module["watch"] + module["tests"])


def fingerprint(module, snapshot):
    digest = hashlib.sha256()
    for path in sorted(source_paths(module, snapshot)):
        mode, oid = snapshot.entries[path]
        digest.update(os.fsencode(path) + b"\0" + mode.encode() + b"\0" + oid.encode() + b"\n")
    return "sha256:" + digest.hexdigest()


def markdown_body(text):
    return re.sub(r"```.*?```|~~~.*?~~~|<!--.*?-->", "", text, flags=re.S)


def anchors(text):
    result, counts = set(), {}
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", markdown_body(text), re.M):
        slug = re.sub(r"[^\w\-\s]", "", heading.lower()).replace(" ", "-")
        number = counts.get(slug, 0)
        counts[slug] = number + 1
        result.add(slug if number == 0 else slug + "-" + str(number))
    result.update(re.findall(r'<(?:a|span)\s+(?:name|id)=["\']([^"\']+)["\']', text))
    return result


def link_errors(path, text, snapshot):
    errors = []
    body = markdown_body(text)
    # Inline links/images and reference definitions; angle brackets allow spaces.
    links = re.findall(r"!?\[[^\]\n]*\]\((<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)", body)
    links += re.findall(r"^\[[^\]\n]+\]:\s*(<[^>]+>|\S+)", body, re.M)
    for raw in links:
        raw = raw.strip("<>")
        split = urlsplit(raw)
        if split.scheme or split.netloc:
            if split.scheme == "file":
                errors.append(path + ": 不使用本机 file 链接 " + raw)
            continue
        target = unquote(split.path)
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(path), target)) if target else path
        if target.startswith("/") or resolved == ".." or resolved.startswith("../") or not snapshot.exists(resolved):
            errors.append(path + ": 链接目标不存在或越出仓库 " + raw)
        elif split.fragment and resolved.endswith(".md") and resolved in snapshot.entries:
            if unquote(split.fragment) not in anchors(snapshot.read(resolved)):
                errors.append(path + ": Markdown 锚点不存在 " + raw)
    return errors


def check(snapshot, config, selected=None):
    errors = []
    excluded = set()
    for rule in config.get("exclude", []):
        excluded |= snapshot.matching(rule["paths"])
    owners = {}
    for module in config["modules"]:
        if any(p.startswith("docs/ai/") for p in source_paths(module, snapshot)):
            raise CheckError(module["id"] + ": 文档不能参与源码指纹（含 watch/tests）")
        owned = snapshot.matching(module["sources"])
        if not owned:
            errors.append(module["id"] + ": 没有归属源码，请检查分支或映射")
        for path in owned:
            if path in excluded or path in owners:
                raise CheckError("重复归属/同时排除: " + path)
            owners[path] = module["id"]
        for path in module["entrypoints"]:
            if path not in snapshot.entries:
                errors.append(module["id"] + ": 入口不存在 " + path)
        for pattern in module["tests"] + module["watch"]:
            if not snapshot.matching([pattern]):
                errors.append(module["id"] + ": 测试/依赖范围不存在 " + pattern)
    for path in sorted(set(snapshot.entries) - excluded - set(owners)):
        errors.append("未归属: " + path)
    for module in config["modules"]:
        if selected is not None and module["id"] not in selected:
            continue
        path = module["doc"]
        if path not in snapshot.entries:
            errors.append("缺少模块文档: " + path)
            continue
        text = snapshot.read(path)
        for heading in HEADINGS:
            section = re.search(r"^## " + re.escape(heading) + r"\s*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
            if not section or len(section.group(1).strip()) < 12:
                errors.append(path + ": 缺少有效章节 " + heading)
        found = META.search(text)
        if not found:
            errors.append(path + ": 缺少 module-review 元数据")
        else:
            review = json.loads(found.group(1))
            if not isinstance(review, dict):
                raise CheckError(path + ": module-review 必须是对象")
            if review.get("id") != module["id"] or not isinstance(review.get("reason"), str) or not review["reason"].strip():
                errors.append(path + ": 复核 ID/原因不完整")
            if review.get("source_fingerprint") != fingerprint(module, snapshot):
                errors.append(path + ": 源码变化需复核并记录指纹")
        errors += link_errors(path, text, snapshot)
    for path in snapshot.entries:
        if path.endswith(".md") and path.startswith("docs/ai/") and "/history/" not in path and "/modules/" not in path:
            errors += link_errors(path, snapshot.read(path), snapshot)
    for path in ("AGENTS.md", "CLAUDE.md"):
        if path in snapshot.entries:
            errors += link_errors(path, snapshot.read(path), snapshot)
    return errors, len(owners), len(excluded)


def affected(current, previous, config):
    changed = {p for p in set(current.entries) | set(previous.entries)
               if current.entries.get(p) != previous.entries.get(p)}
    if MANIFEST in changed or MANIFEST not in previous.entries:
        return {m["id"] for m in config["modules"]}
    old = {m["id"]: m for m in manifest(previous)["modules"]}
    return {m["id"] for m in config["modules"] if m["doc"] in changed or
            changed & (source_paths(m, current) | source_paths(old.get(m["id"], m), previous))}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--all", action="store_true", help="工作目录全量检查（默认）")
    modes.add_argument("--staged", action="store_true", help="暂存快照相对 HEAD 的增量检查")
    modes.add_argument("--base", help="当前 HEAD 相对指定 Git ref 的累计检查（不含工作区）")
    parser.add_argument("--fingerprints", action="store_true", help="只打印预期指纹，不更新文档")
    parser.add_argument("--root", default=".", help="仓库目录")
    args = parser.parse_args(argv)
    try:
        root = decode(git(args.root, "rev-parse", "--show-toplevel")).strip()
        current = Snapshot(root, "index" if args.staged else "tree" if args.base else "worktree", "HEAD")
        config = manifest(current)
        selected = None
        if args.base or args.staged:
            ref = args.base or "HEAD"
            oid = git(root, "rev-parse", "--verify", ref + "^{commit}", optional=not args.base).strip()
            if oid:
                selected = affected(current, Snapshot(root, "tree", oid.decode()), config)
        if args.fingerprints:
            print(json.dumps({m["id"]: fingerprint(m, current) for m in config["modules"]
                              if selected is None or m["id"] in selected}, indent=2, ensure_ascii=False))
            return 0
        errors, owned, excluded = check(current, config, selected)
        for error in errors:
            print(error)
        count = len(config["modules"]) if selected is None else len(selected)
        print(f"模块 {len(config['modules'])}；本次复核 {count}；归属文件 {owned}；明确排除 {excluded}；问题 {len(errors)}")
        return 1 if errors else 0
    except (CheckError, OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        print("检查工具/配置错误: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
