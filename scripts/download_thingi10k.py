#!/usr/bin/env python3
"""分批下载 Thingi10K 原始 STL/OBJ（Python 3.9+，无第三方依赖）。

来源：https://github.com/Thingi10K/Thingi10K
默认只抽取有几何缺陷标记的模型；标记来自数据集，不代表本机检测结果。
只下载原始字节，不修复、不缩放、不解压；各模型许可见 manifest.json。
"""

import argparse
import csv
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import random
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager


REVISION = "2d5d3b2f3cd3711028ad75b12788c13b25559ec6"
BASE_URL = f"https://huggingface.co/datasets/Thingi10K/Thingi10K/resolve/{REVISION}"
INDEX_URL = f"{BASE_URL}/metadata/input_summary.csv"
FLAGS = (
    "No duplicated faces", "Closed", "Edge manifold", "No degenerate faces",
    "Vertex manifold", "Single Component", "PWN",
)
# Single Component and PWN are deliberately not defect gates. Multiple shells
# may be intentional, and PWN is not equivalent to a local winding conflict.
DEFECTS = {
    "boundary": ("Closed", "不闭合／可能有开放边"),
    "nonmanifold-edge": ("Edge manifold", "非流形边"),
    "nonmanifold-vertex": ("Vertex manifold", "非流形顶点"),
    "duplicate": ("No duplicated faces", "重复面"),
    "degenerate": ("No degenerate faces", "退化面"),
}
SUBSETS = ("defects", "all", *DEFECTS, "metadata-clean")
CHUNK = 1024 * 1024


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_index(data):
    reader = csv.DictReader(io.StringIO(data.decode("utf-8-sig")))
    required = {"ID", "Thing ID", "License", "Link", *FLAGS}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError("官方索引缺少必要字段，停止下载。")
    models = {}
    for row in reader:
        file_id = str(int(row["ID"]))
        thing_id = str(int(row["Thing ID"]))
        suffix = Path(urllib.parse.urlsplit(row["Link"]).path).suffix.lower()
        if suffix not in (".stl", ".obj"):
            continue
        if int(file_id) < 0 or int(thing_id) < 0 or file_id in models:
            raise ValueError("官方索引包含重复或无效 ID。")
        flags = {}
        for key in FLAGS:
            value = row[key].strip().upper()
            if value not in ("TRUE", "FALSE", "NA", ""):
                raise ValueError(f"模型 {file_id} 的 {key} 标记无效。")
            flags[key] = None if value in ("NA", "") else value == "TRUE"
        filename = file_id + suffix
        models[file_id] = {
            "id": file_id,
            "thing_id": thing_id,
            "license": row["License"],
            "source_link": row["Link"],
            "thing_url": f"https://www.thingiverse.com/thing:{thing_id}",
            "filename": filename,
            "download_url": f"{BASE_URL}/raw_meshes/{filename}",
            "dataset_flags": flags,
            "metadata_defects": [key for key, (flag, _) in DEFECTS.items() if flags[flag] is False],
            "metadata_defect_labels": [label for flag, label in DEFECTS.values() if flags[flag] is False],
            "verification_status": "not_run",
        }
    if not models:
        raise ValueError("索引中没有可下载的 STL/OBJ。")
    return models


def matches_subset(model, subset):
    flags = model["dataset_flags"]
    if subset == "all":
        return True
    if subset == "defects":
        return any(flags[flag] is False for flag, _ in DEFECTS.values())
    if subset == "metadata-clean":
        return all(flags[flag] is True for flag in FLAGS)
    return flags[DEFECTS[subset][0]] is False


def select_models(models, count, seed, ids, batch=1, subset="all"):
    if batch < 1:
        raise ValueError("批次编号必须大于 0。")
    if subset not in SUBSETS:
        raise ValueError(f"未知筛选类型：{subset}")
    if ids:
        if subset != "all":
            raise ValueError("指定文件 ID 时不能同时筛选类型。")
        selected = list(dict.fromkeys(str(int(value)) for value in ids))
        missing = [value for value in selected if value not in models]
        if missing:
            raise ValueError("索引中找不到这些 STL/OBJ 文件 ID：" + ", ".join(missing))
    else:
        selected = sorted((key for key, model in models.items() if matches_subset(model, subset)), key=int)
        if not selected:
            raise ValueError(f"筛选类型 {subset} 没有符合条件的 STL/OBJ。")
        if subset == "all" and count > len(selected):
            raise ValueError(f"要求 {count} 个，但索引中只有 {len(selected)} 个 STL/OBJ。")
        start = (batch - 1) * count
        if start >= len(selected):
            raise ValueError(f"第 {batch} 批已超出筛选后的模型总数 {len(selected)}。")
        random.Random(seed).shuffle(selected)
        selected = selected[start:start + count]
    return [models[value] for value in selected]


def publish_new(source, target):
    # Exclusive creation also protects files created by another program while downloading.
    with target.open("xb") as destination:
        try:
            with source.open("rb") as incoming:
                shutil.copyfileobj(incoming, destination, CHUNK)
        except BaseException:
            destination.close()
            target.unlink()
            raise


def download(url, target, timeout, retries, max_bytes):
    """Retry complete transfers; a partial transfer is never a completed model."""
    for attempt in range(retries + 1):
        fd, temporary = tempfile.mkstemp(prefix=".thingi10k-", suffix=".part", dir=target.parent)
        part = Path(temporary)
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "Thingi10K-test-downloader/1.0"})
            with os.fdopen(fd, "wb") as output:
                with urllib.request.urlopen(request, timeout=timeout) as response:
                    if response.status != 200:
                        raise ValueError(f"服务器返回非完整响应：HTTP {response.status}")
                    if "text/html" in response.headers.get("Content-Type", "").lower():
                        raise ValueError("服务器返回网页而不是文件。")
                    length = response.headers.get("Content-Length")
                    expected = int(length) if length is not None else None
                    if expected is not None and (expected < 0 or expected > max_bytes):
                        raise ValueError("文件超过大小上限；可增大 --max-mib。")
                    total = 0
                    prefix = bytearray()
                    while True:
                        chunk = response.read(CHUNK)
                        if not chunk:
                            break
                        total += len(chunk)
                        if total > max_bytes:
                            raise ValueError("文件超过大小上限；可增大 --max-mib。")
                        if len(prefix) < 256:
                            prefix.extend(chunk[:256 - len(prefix)])
                        output.write(chunk)
                    if expected is not None and total != expected:
                        raise OSError(f"下载不完整：预期 {expected} 字节，实际 {total} 字节。")
                    if prefix.startswith(b"version https://git-lfs.github.com/spec/v1"):
                        raise ValueError("下载到大文件指针，而不是模型。")
                    if bytes(prefix).lstrip().lower().startswith((b"<!doctype html", b"<html")):
                        raise ValueError("服务器返回网页而不是文件。")
            publish_new(part, target)
            return
        except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
            if isinstance(error, (FileExistsError, PermissionError)) or attempt == retries:
                raise
            print(f"  下载中断，重试 {attempt + 1}/{retries}：{error}", file=sys.stderr, flush=True)
            time.sleep(min(attempt + 1, 3))
        finally:
            part.unlink(missing_ok=True)


@contextmanager
def output_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / ".download.lock"
    try:
        handle = lock.open("x", encoding="utf-8")
    except FileExistsError:
        raise ValueError(f"目录正在下载或上次被强制终止。确认没有下载进程后删除 {lock} 再运行。") from None
    try:
        with handle:
            handle.write(str(os.getpid()))
        yield
    finally:
        lock.unlink()


def save_manifest(path, manifest):
    fd, temporary = tempfile.mkstemp(prefix=".manifest-", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            json.dump(manifest, output, ensure_ascii=False, indent=2)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def run(args):
    if args.batch < 1:
        raise ValueError("批次编号必须大于 0。")
    if args.subset not in SUBSETS or (args.ids and args.subset != "all"):
        raise ValueError("筛选类型无效，或与指定文件 ID 冲突。")
    parent = args.output.expanduser().resolve()
    if args.subset != "all":
        parent = parent / args.subset
        if parent.is_symlink():
            raise ValueError(f"不使用已有软链接：{parent}")
    directory = parent / f"batch-{args.batch:03d}"
    if directory.is_symlink():
        raise ValueError(f"不使用已有软链接：{directory}")
    batch_settings = {"number": args.batch, "mode": "ids" if args.ids else "random"}
    if args.ids:
        batch_settings["ids"] = list(dict.fromkeys(str(int(value)) for value in args.ids))
    else:
        batch_settings.update(count=args.count, seed=args.seed)
        if args.subset != "all":
            batch_settings.update(mode="filtered", subset=args.subset)
    with output_lock(directory):
        manifest_path = directory / "manifest.json"
        index_path = directory / f"input_summary-{REVISION}.csv"
        model_dir = directory / "models"
        for path in (manifest_path, index_path, model_dir):
            if path.is_symlink():
                raise ValueError(f"不使用已有软链接：{path}")
        manifest = {"schema_version": 1, "revision": REVISION, "index_url": INDEX_URL, "models": {}}
        if manifest_path.exists():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if (not isinstance(manifest, dict) or manifest.get("schema_version") != 1
                    or manifest.get("revision") != REVISION or not isinstance(manifest.get("models"), dict)):
                raise ValueError("已有清单不是本脚本当前版本的结果，请换一个下载目录。")
            if not all(isinstance(item, dict) for item in manifest["models"].values()):
                raise ValueError("已有清单的模型记录无效，请换一个下载目录。")
            if manifest.get("batch_settings") != batch_settings:
                raise ValueError("该批次的选择配置已改变，请沿用原批次参数或使用新的下载父目录。")
        if not index_path.exists():
            print("下载官方模型索引……", flush=True)
            download(INDEX_URL, index_path, args.timeout, args.retries, 8 * CHUNK)
        index_hash = sha256_file(index_path)
        if manifest.get("index_sha256", index_hash) != index_hash:
            raise ValueError("本地索引已改变，请使用新的下载目录，原文件保持不变。")
        models = parse_index(index_path.read_bytes())
        selected = select_models(models, args.count, args.seed, args.ids, args.batch, args.subset)
        manifest["batch_settings"] = batch_settings
        manifest["index_sha256"] = index_hash
        manifest["last_selection"] = [model["id"] for model in selected]
        # Save expected defects even in dry-run mode, without claiming that the
        # bytes have been downloaded or checked by a local geometry engine.
        manifest["selection"] = selected
        save_manifest(manifest_path, manifest)
        print(f"第 {args.batch} 批，共 {len(selected)} 个。目录：{directory}", flush=True)
        print("数据集标记仅用于筛选，尚未执行本机检测；不保证导入后仍会报同样的问题。", flush=True)
        print("标记未涵盖自相交；metadata-clean 也不代表可打印或全部检查通过。", flush=True)
        for key, (_, label) in DEFECTS.items():
            total = sum(key in model["metadata_defects"] for model in selected)
            if total:
                print(f"  {label}：{total} 个（类型可重叠）", flush=True)
        if args.dry_run:
            for model in selected:
                labels = "、".join(model["metadata_defect_labels"]) or "没有这五类缺陷标记（未实测）"
                print(f"{model['filename']}  {labels}  {model['license']}")
            print(f"共 {len(selected)} 个；仅保存索引和清单，没有下载模型。")
            return 0
        model_dir.mkdir(exist_ok=True)
        failed, downloaded, skipped = 0, 0, 0
        for number, model in enumerate(selected, 1):
            target = model_dir / model["filename"]
            previous = manifest["models"].get(model["id"], {})
            labels = "、".join(model["metadata_defect_labels"]) or "没有这五类缺陷标记（未实测）"
            print(f"[{number}/{len(selected)}] {model['filename']} — {labels}", flush=True)
            try:
                if target.is_symlink():
                    raise ValueError("已有模型是软链接，不覆盖。")
                if target.exists():
                    if (previous.get("download_url") != model["download_url"]
                            or previous.get("sha256") != sha256_file(target)):
                        raise ValueError("已有文件未登记或被修改，不覆盖；请保留它并换下载目录。")
                    skipped += 1
                    print("  已下载且校验一致，跳过。", flush=True)
                else:
                    download(model["download_url"], target, args.timeout, args.retries, args.max_mib * CHUNK)
                    downloaded += 1
                manifest["models"][model["id"]] = {
                    **model, "status": "downloaded", "bytes": target.stat().st_size,
                    "sha256": sha256_file(target), "path": f"models/{model['filename']}",
                }
            except (ValueError, OSError, urllib.error.URLError, http.client.HTTPException) as error:
                failed += 1
                manifest["models"][model["id"]] = {
                    **model, **previous, "status": "failed", "last_error": str(error),
                }
                print(f"  失败：{error}", file=sys.stderr, flush=True)
            save_manifest(manifest_path, manifest)
        print(f"完成：新下载 {downloaded}，跳过 {skipped}，失败 {failed}。目录：{directory}")
        print("原模型未修复或缩放；许可及原始缺陷标记见 manifest.json。")
        return 1 if failed else 0


def positive_int(value):
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("必须大于 0")
    return parsed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--count", type=positive_int, default=100, help="每批数量（默认 100；同一轮各批保持不变）")
    selection.add_argument("--ids", nargs="+", type=positive_int, help="指定文件 ID，不是 Thing ID")
    parser.add_argument("--batch", type=positive_int, default=1, help="批次编号（默认 1）；指定 ID 时仅用于目录命名")
    parser.add_argument("--seed", type=int, default=2026, help="随机种子（默认 2026；同一轮各批保持不变）")
    parser.add_argument("--subset", choices=SUBSETS,
                        help="默认 defects：五类几何缺陷；all：全部随机；boundary：不闭合；"
                             "nonmanifold-edge/vertex：非流形边/点；duplicate：重复面；"
                             "degenerate：退化面；metadata-clean：七项标记正常的对照组（非检测通过）")
    parser.add_argument("--output", "-o", type=Path, default=Path.home() / "Downloads" / "Thingi10K",
                        help="下载父目录；筛选模式保存到 类型/batch-编号/models，all 或指定 ID 保持旧批次路径")
    parser.add_argument("--timeout", type=positive_int, default=30, help="单次网络读写超时秒数（默认 30）")
    parser.add_argument("--retries", type=int, choices=range(0, 6), default=2, help="每个文件失败后的重试次数（默认 2）")
    parser.add_argument("--max-mib", type=positive_int, default=512, help="每个模型大小上限 MiB（默认 512）")
    parser.add_argument("--dry-run", action="store_true", help="只获取索引并列出选定文件，不下载模型")
    args = parser.parse_args(argv)
    if args.ids and args.subset is not None:
        parser.error("--ids 与 --subset 不能同时使用；指定 ID 不进行缺陷筛选。")
    args.subset = args.subset or ("all" if args.ids else "defects")
    try:
        return run(args)
    except KeyboardInterrupt:
        print("\n已停止；已登记的完整文件下次会校验并跳过。未登记文件不会被覆盖。", file=sys.stderr)
        return 130
    except (ValueError, OSError, urllib.error.URLError, http.client.HTTPException) as error:
        print(f"下载失败：{error}\n请检查网络能否访问 huggingface.co，或使用新的下载目录。", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
