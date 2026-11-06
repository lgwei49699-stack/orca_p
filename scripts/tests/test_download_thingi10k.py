"""Offline regression tests for raw Thingi10K downloads; no application build."""

import argparse
import contextlib
import csv
import hashlib
import http.client
import importlib.util
import io
import json
from pathlib import Path
import random
import tempfile
import unittest
from unittest import mock
import urllib.error


SCRIPT = Path(__file__).resolve().parents[1] / "download_thingi10k.py"
SPEC = importlib.util.spec_from_file_location("thingi10k_downloader_under_test", SCRIPT)
downloader = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(downloader)

FIELDS = [
    "ID", "Thing ID", "License", "Link", "No duplicated faces", "Closed",
    "Edge manifold", "No degenerate faces", "Vertex manifold", "Single Component", "PWN",
]
RAW_MODEL = b"solid unchanged\nendsolid unchanged\n"
DEFECT_SUBSETS = {
    "boundary": "Closed",
    "nonmanifold-edge": "Edge manifold",
    "nonmanifold-vertex": "Vertex manifold",
    "duplicate": "No duplicated faces",
    "degenerate": "No degenerate faces",
}


def index_bytes(rows=None, fields=None):
    fields = FIELDS if fields is None else fields
    if rows is None:
        rows = [make_row(1), make_row(2, suffix=".obj"), make_row(3)]
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue().encode("utf-8")


def make_row(file_id, suffix=".stl", **overrides):
    row = {key: "TRUE" for key in FIELDS[4:]}
    row.update({
        "ID": str(file_id), "Thing ID": str(1000 + int(file_id)),
        "License": "Creative Commons - Attribution",
        "Link": f"https://example.invalid/原始名字{suffix}",
    })
    row.update(overrides)
    return row


class Response(io.BytesIO):
    def __init__(self, data, headers=None, status=200):
        super().__init__(data)
        self.headers = {} if headers is None else headers
        self.status = status


class InterruptedResponse(Response):
    def read(self, size=-1):
        raise http.client.IncompleteRead(b"partial", 30)


class OfflineTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="thingi10k tests ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Any unmocked network access fails immediately, including index fetches.
        network = mock.patch.object(
            downloader.urllib.request, "urlopen",
            side_effect=AssertionError("Tests must not access the network"),
        )
        self.network = network.start()
        self.addCleanup(network.stop)
        sleep = mock.patch.object(downloader.time, "sleep")
        self.sleep = sleep.start()
        self.addCleanup(sleep.stop)
        stdout = contextlib.redirect_stdout(io.StringIO())
        stderr = contextlib.redirect_stderr(io.StringIO())
        stdout.__enter__()
        stderr.__enter__()
        self.addCleanup(stdout.__exit__, None, None, None)
        self.addCleanup(stderr.__exit__, None, None, None)


class IndexTests(OfflineTests):
    def test_official_fields_raw_numeric_filenames_and_source_metadata(self):
        raw = index_bytes([
            make_row(170301, Closed="FALSE", **{"Single Component": "FALSE"}),
            make_row(7, suffix=".OBJ", Link="https://example.invalid/something.OBJ?download=1"),
        ])
        models = downloader.parse_index(b"\xef\xbb\xbf" + raw)
        self.assertEqual(list(models), ["170301", "7"])
        model = models["170301"]
        self.assertEqual(model["filename"], "170301.stl")
        self.assertEqual(model["thing_id"], "171301")
        self.assertEqual(model["license"], "Creative Commons - Attribution")
        self.assertEqual(model["download_url"], downloader.BASE_URL + "/raw_meshes/170301.stl")
        self.assertEqual(model["source_link"], "https://example.invalid/原始名字.stl")
        self.assertEqual(model["thing_url"], "https://www.thingiverse.com/thing:171301")
        self.assertEqual(set(model["dataset_flags"]), set(FIELDS[4:]))
        self.assertFalse(model["dataset_flags"]["Closed"])
        self.assertFalse(model["dataset_flags"]["Single Component"])
        self.assertTrue(model["dataset_flags"]["Edge manifold"])
        self.assertEqual(models["7"]["filename"], "7.obj")
        self.network.assert_not_called()

    def test_missing_required_field_is_rejected(self):
        with self.assertRaises(ValueError):
            downloader.parse_index(index_bytes(fields=[field for field in FIELDS if field != "License"]))

    def test_empty_flags_remain_unknown_not_healthy(self):
        models = downloader.parse_index(index_bytes([
            make_row(49911, Closed="", PWN="NA", **{"No duplicated faces": "  "}),
        ]))
        self.assertIsNone(models["49911"]["dataset_flags"]["Closed"])
        self.assertIsNone(models["49911"]["dataset_flags"]["No duplicated faces"])
        self.assertIsNone(models["49911"]["dataset_flags"]["PWN"])
        self.assertTrue(models["49911"]["dataset_flags"]["Edge manifold"])

    def test_duplicate_invalid_ids_and_invalid_flags_are_rejected(self):
        cases = [
            [make_row(1), make_row(1)],
            [make_row(-1)],
            [make_row(1, **{"Thing ID": "-5"})],
            [make_row(1, Closed="UNKNOWN")],
            [make_row(1, ID="not-a-number")],
        ]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                downloader.parse_index(index_bytes(rows))

    def test_non_mesh_rows_are_skipped_and_empty_selection_is_rejected(self):
        models = downloader.parse_index(index_bytes([make_row(1), make_row(2, suffix=".png")]))
        self.assertEqual(list(models), ["1"])
        with self.assertRaises(ValueError):
            downloader.parse_index(index_bytes([make_row(1, suffix=".png")]))

    def test_seed_is_reproducible_prefix_stable_and_does_not_touch_global_rng(self):
        models = downloader.parse_index(index_bytes([make_row(number) for number in range(1, 31)]))
        before = random.getstate()
        small = downloader.select_models(models, count=5, seed=2026, ids=None)
        large = downloader.select_models(models, count=20, seed=2026, ids=None)
        reversed_models = dict(reversed(list(models.items())))
        repeat = downloader.select_models(reversed_models, count=5, seed=2026, ids=None)
        self.assertEqual(small, large[:5])
        self.assertEqual(small, repeat)
        self.assertEqual(len({item["id"] for item in large}), 20)
        self.assertEqual(random.getstate(), before)

    def test_explicit_ids_preserve_order_and_remove_duplicates(self):
        models = downloader.parse_index(index_bytes())
        selected = downloader.select_models(models, count=100, seed=2026, ids=[3, "02", 3, 1])
        self.assertEqual([model["id"] for model in selected], ["3", "2", "1"])

    def test_unknown_ids_and_excessive_count_are_rejected(self):
        models = downloader.parse_index(index_bytes())
        with self.assertRaisesRegex(ValueError, "999"):
            downloader.select_models(models, count=1, seed=2026, ids=[999])
        with self.assertRaises(ValueError):
            downloader.select_models(models, count=4, seed=2026, ids=None)

    def test_batches_page_one_shuffle_without_overlap(self):
        models = downloader.parse_index(index_bytes([make_row(number) for number in range(1, 9)]))
        all_models = downloader.select_models(models, count=8, seed=2026, ids=None)
        first = downloader.select_models(models, count=3, seed=2026, ids=None, batch=1)
        second = downloader.select_models(models, count=3, seed=2026, ids=None, batch=2)
        tail = downloader.select_models(models, count=3, seed=2026, ids=None, batch=3)
        self.assertEqual(first + second + tail, all_models)
        self.assertEqual(len(tail), 2)
        self.assertTrue({model["id"] for model in first}.isdisjoint(model["id"] for model in second))

    def test_batch_outside_dataset_and_invalid_batch_numbers_are_rejected(self):
        models = downloader.parse_index(index_bytes())
        for batch in (0, -1, 3):
            with self.subTest(batch=batch), self.assertRaises(ValueError):
                downloader.select_models(models, count=2, seed=2026, ids=None, batch=batch)
        with self.assertRaises(ValueError):
            downloader.select_models(models, count=4, seed=2026, ids=None, batch=1)

    def test_explicit_ids_are_not_paged_by_batch_number(self):
        models = downloader.parse_index(index_bytes())
        first = downloader.select_models(models, count=100, seed=2026, ids=[3, "02", 3], batch=1)
        later = downloader.select_models(models, count=100, seed=2026, ids=[3, "02", 3], batch=500)
        self.assertEqual(first, later)
        self.assertEqual([model["id"] for model in later], ["3", "2"])


class FilterTests(OfflineTests):
    def setUp(self):
        super().setUp()
        self.rows = [
            make_row(number, **{flag: "FALSE"})
            for number, flag in enumerate(DEFECT_SUBSETS.values(), 1)
        ] + [
            make_row(6, **{"Single Component": "FALSE"}),
            make_row(7, PWN="FALSE"),
            make_row(8, Closed="NA"),
            make_row(9),
        ]
        self.models = downloader.parse_index(index_bytes(self.rows))

    def select(self, subset, count=100, batch=1, models=None):
        return downloader.select_models(
            self.models if models is None else models,
            count=count, seed=2026, ids=None, batch=batch, subset=subset,
        )

    def test_defect_subset_requires_one_of_five_explicit_false_flags(self):
        selected = self.select("defects")
        self.assertEqual({model["id"] for model in selected}, {"1", "2", "3", "4", "5"})
        self.assertTrue(all(model["metadata_defects"] for model in selected))

    def test_each_defect_subset_selects_its_corresponding_flag(self):
        for number, (subset, flag) in enumerate(DEFECT_SUBSETS.items(), 1):
            with self.subTest(subset=subset):
                selected = self.select(subset)
                self.assertEqual([model["id"] for model in selected], [str(number)])
                self.assertEqual(selected[0]["metadata_defects"], [subset])
                self.assertIs(selected[0]["dataset_flags"][flag], False)
                self.assertEqual(selected[0]["verification_status"], "not_run")
                self.assertEqual(len(selected[0]["metadata_defect_labels"]), 1)
                self.assertTrue(any("\u4e00" <= char <= "\u9fff"
                                    for char in selected[0]["metadata_defect_labels"][0]))

    def test_multiple_defects_are_all_recorded_without_component_or_pwn_labels(self):
        models = downloader.parse_index(index_bytes([
            make_row(10, Closed="FALSE", PWN="FALSE", **{
                "No duplicated faces": "FALSE", "Single Component": "FALSE",
            }),
        ]))
        selected = self.select("defects", models=models)
        self.assertEqual(set(selected[0]["metadata_defects"]), {"boundary", "duplicate"})
        self.assertEqual(len(selected[0]["metadata_defect_labels"]), 2)

    def test_unknown_and_false_like_non_booleans_are_not_reported_as_defects(self):
        for value in (None, 0, 0.0, "", "FALSE"):
            with self.subTest(value=value):
                models = downloader.parse_index(index_bytes([make_row(1)]))
                for flag in DEFECT_SUBSETS.values():
                    models["1"]["dataset_flags"][flag] = value
                with self.assertRaises(ValueError):
                    self.select("defects", models=models)

    def test_metadata_clean_requires_all_seven_flags_strictly_true(self):
        selected = self.select("metadata-clean")
        self.assertEqual([model["id"] for model in selected], ["9"])
        self.assertEqual(selected[0]["metadata_defects"], [])
        self.assertEqual(selected[0]["metadata_defect_labels"], [])
        self.assertEqual(selected[0]["verification_status"], "not_run")
        for value in (None, False, 1, "TRUE"):
            with self.subTest(value=value):
                models = downloader.parse_index(index_bytes([make_row(1)]))
                models["1"]["dataset_flags"]["PWN"] = value
                with self.assertRaises(ValueError):
                    self.select("metadata-clean", models=models)

    def test_filtering_precedes_shuffle_and_batches_do_not_overlap(self):
        first = self.select("defects", count=2, batch=1)
        second = self.select("defects", count=2, batch=2)
        tail = self.select("defects", count=2, batch=3)
        complete = self.select("defects")
        reverse_input = dict(reversed(list(self.models.items())))
        self.assertEqual(first + second + tail, complete)
        self.assertEqual(len(tail), 1)
        self.assertEqual(first, self.select("defects", count=2, models=reverse_input))
        self.assertEqual(len({model["id"] for model in first + second + tail}), 5)

    def test_filtered_short_first_batch_is_allowed_but_exhausted_batch_is_rejected(self):
        self.assertEqual(len(self.select("defects", count=100)), 5)
        for count, batch in ((100, 2), (2, 4)):
            with self.subTest(count=count, batch=batch), self.assertRaises(ValueError):
                self.select("defects", count=count, batch=batch)
        with self.assertRaises(ValueError):
            self.select("boundary", models={"9": self.models["9"]})

    def test_all_retains_old_selection_and_excessive_count_rejection(self):
        old = downloader.select_models(self.models, count=9, seed=2026, ids=None)
        self.assertEqual(self.select("all", count=9), old)
        self.assertEqual({model["id"] for model in old}, set(self.models))
        with self.assertRaises(ValueError):
            self.select("all", count=100)

    def test_unknown_subset_is_rejected(self):
        with self.assertRaises(ValueError):
            self.select("not-a-subset")


class DownloadTests(OfflineTests):
    def transfer(self, target=None, retries=0, max_bytes=1024):
        target = self.root / "1.stl" if target is None else target
        downloader.download("https://example.invalid/1.stl", target, 7, retries, max_bytes)
        return target

    def assert_clean_failure(self):
        self.assertFalse((self.root / "1.stl").exists())
        self.assertEqual(list(self.root.glob(".thingi10k-*.part")), [])

    def test_success_keeps_original_bytes_and_removes_temporary_file(self):
        self.network.side_effect = None
        self.network.return_value = Response(RAW_MODEL, {"Content-Length": str(len(RAW_MODEL))})
        target = self.transfer()
        self.assertEqual(target.read_bytes(), RAW_MODEL)
        self.assertEqual(list(self.root.glob(".thingi10k-*.part")), [])
        request = self.network.call_args.args[0]
        self.assertEqual(request.full_url, "https://example.invalid/1.stl")
        self.assertEqual(self.network.call_args.kwargs["timeout"], 7)

    def test_network_error_is_retried_from_the_beginning(self):
        self.network.side_effect = [urllib.error.URLError("offline"), Response(RAW_MODEL)]
        self.assertEqual(self.transfer(retries=1).read_bytes(), RAW_MODEL)
        self.assertEqual(self.network.call_count, 2)
        self.sleep.assert_called_once()

    def test_truncated_response_is_retried_without_appending_partial_bytes(self):
        self.network.side_effect = [
            Response(b"partial", {"Content-Length": "100"}),
            Response(RAW_MODEL, {"Content-Length": str(len(RAW_MODEL))}),
        ]
        self.assertEqual(self.transfer(retries=1).read_bytes(), RAW_MODEL)
        self.assertEqual(self.network.call_count, 2)
        self.assertEqual(list(self.root.glob(".thingi10k-*.part")), [])

    def test_http_read_interruption_is_retried(self):
        self.network.side_effect = [InterruptedResponse(b"partial"), Response(RAW_MODEL)]
        self.assertEqual(self.transfer(retries=1).read_bytes(), RAW_MODEL)
        self.assertEqual(self.network.call_count, 2)

    def test_retry_exhaustion_does_not_publish_partial_file(self):
        self.network.side_effect = [
            Response(b"one", {"Content-Length": "10"}),
            Response(b"two", {"Content-Length": "10"}),
        ]
        with self.assertRaises(OSError):
            self.transfer(retries=1)
        self.assertEqual(self.network.call_count, 2)
        self.assert_clean_failure()

    def test_html_content_type_and_disguised_html_are_rejected(self):
        for headers, body in [
            ({"Content-Type": "text/html; charset=utf-8"}, b"<html>error</html>"),
            ({"Content-Type": "application/octet-stream"}, b" \n<!DOCTYPE html><html>error</html>"),
        ]:
            with self.subTest(headers=headers):
                self.network.side_effect = None
                self.network.return_value = Response(body, headers)
                with self.assertRaises(ValueError):
                    self.transfer()
                self.assert_clean_failure()

    def test_git_lfs_pointer_is_not_a_completed_model(self):
        self.network.side_effect = None
        self.network.return_value = Response(
            b"version https://git-lfs.github.com/spec/v1\noid sha256:deadbeef\nsize 123\n"
        )
        with self.assertRaises(ValueError):
            self.transfer()
        self.assert_clean_failure()

    def test_declared_size_and_streamed_size_limits_are_enforced(self):
        for headers in ({"Content-Length": "11"}, {}):
            with self.subTest(headers=headers):
                self.network.side_effect = None
                self.network.return_value = Response(b"x" * 11, headers)
                with self.assertRaises(ValueError):
                    self.transfer(max_bytes=10)
                self.assert_clean_failure()

    def test_zero_byte_dataset_models_are_preserved_with_or_without_length(self):
        # The research dataset intentionally includes broken/empty inputs. A
        # byte-faithful downloader must not silently remove those test cases.
        for number, headers in enumerate(({"Content-Length": "0"}, {})):
            with self.subTest(headers=headers):
                self.network.side_effect = None
                self.network.return_value = Response(b"", headers)
                target = self.root / f"empty-{number}.stl"
                self.transfer(target)
                self.assertTrue(target.is_file())
                self.assertEqual(target.read_bytes(), b"")
                self.assertEqual(list(self.root.glob(".thingi10k-*.part")), [])

    def test_partial_http_status_is_rejected(self):
        self.network.side_effect = None
        self.network.return_value = Response(RAW_MODEL, status=206)
        with self.assertRaises(ValueError):
            self.transfer()
        self.assert_clean_failure()

    def test_existing_file_is_never_overwritten_or_retried(self):
        target = self.root / "1.stl"
        target.write_bytes(b"user original")
        self.network.side_effect = None
        self.network.return_value = Response(RAW_MODEL)
        with self.assertRaises(FileExistsError):
            self.transfer(target, retries=2)
        self.assertEqual(target.read_bytes(), b"user original")
        self.assertEqual(self.network.call_count, 1)
        self.sleep.assert_not_called()
        self.assertEqual(list(self.root.glob(".thingi10k-*.part")), [])


class RunTests(OfflineTests):
    def setUp(self):
        super().setUp()
        self.batch_dir = self.root / "batch-001"
        self.batch_dir.mkdir()
        self.index = self.batch_dir / f"input_summary-{downloader.REVISION}.csv"
        self.index.write_bytes(index_bytes())
        self.args = argparse.Namespace(
            output=self.root, count=3, seed=2026, ids=[1, 2, 3],
            timeout=7, retries=1, max_mib=8, dry_run=False, batch=1, subset="all",
        )

    def fake_download(self, url, target, timeout, retries, max_bytes):
        self.assertIn("/raw_meshes/", url)
        self.assertEqual(timeout, 7)
        self.assertEqual(retries, 1)
        self.assertEqual(max_bytes, 8 * downloader.CHUNK)
        target.write_bytes(RAW_MODEL + target.name.encode("ascii"))

    def manifest(self, batch=1):
        directory = self.root / f"batch-{batch:03d}"
        return json.loads((directory / "manifest.json").read_text(encoding="utf-8"))

    def test_download_manifest_and_resume_verify_hash_before_skipping(self):
        with mock.patch.object(downloader, "download", side_effect=self.fake_download) as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 3)
        manifest = self.manifest()
        self.assertEqual(manifest["revision"], downloader.REVISION)
        self.assertEqual(manifest["index_sha256"], hashlib.sha256(self.index.read_bytes()).hexdigest())
        self.assertEqual(manifest["last_selection"], ["1", "2", "3"])
        self.assertEqual(manifest["batch_settings"], {"number": 1, "mode": "ids", "ids": ["1", "2", "3"]})
        for file_id, entry in manifest["models"].items():
            payload = (self.batch_dir / entry["path"]).read_bytes()
            self.assertEqual(entry["status"], "downloaded")
            self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(entry["bytes"], len(payload))
            self.assertEqual(entry["id"], file_id)
            self.assertIn("license", entry)
            self.assertIn("dataset_flags", entry)
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        self.assertFalse((self.batch_dir / ".download.lock").exists())
        self.network.assert_not_called()

    def test_modified_model_is_protected_while_other_files_continue(self):
        with mock.patch.object(downloader, "download", side_effect=self.fake_download):
            self.assertEqual(downloader.run(self.args), 0)
        target = self.batch_dir / "models/1.stl"
        original_hash = self.manifest()["models"]["1"]["sha256"]
        target.write_bytes(b"user changed mesh")
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 1)
            transfer.assert_not_called()
        self.assertEqual(target.read_bytes(), b"user changed mesh")
        entry = self.manifest()["models"]["1"]
        self.assertEqual(entry["sha256"], original_hash)
        self.assertEqual(entry["status"], "failed")
        self.assertIn("last_error", entry)
        self.assertEqual(self.manifest()["models"]["2"]["status"], "downloaded")

    def test_existing_unregistered_model_is_protected(self):
        (self.batch_dir / "models").mkdir()
        target = self.batch_dir / "models/1.stl"
        target.write_bytes(b"unregistered user file")
        self.args.ids = [1]
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 1)
            transfer.assert_not_called()
        self.assertEqual(target.read_bytes(), b"unregistered user file")
        self.assertIn("last_error", self.manifest()["models"]["1"])

    def test_failed_download_does_not_stop_later_models_and_can_resume(self):
        def first_run(url, target, *options):
            if target.name == "1.stl":
                raise urllib.error.URLError("simulated connection failure")
            self.fake_download(url, target, *options)

        with mock.patch.object(downloader, "download", side_effect=first_run) as transfer:
            self.assertEqual(downloader.run(self.args), 1)
            self.assertEqual(transfer.call_count, 3)
        first = self.manifest()
        self.assertIn("last_error", first["models"]["1"])
        self.assertEqual(first["models"]["1"]["status"], "failed")
        self.assertFalse((self.batch_dir / "models/1.stl").exists())
        self.assertEqual(first["models"]["2"]["status"], "downloaded")
        self.assertEqual(first["models"]["3"]["status"], "downloaded")
        with mock.patch.object(downloader, "download", side_effect=self.fake_download) as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 1)
            self.assertEqual(transfer.call_args.args[1].name, "1.stl")
        self.assertNotIn("last_error", self.manifest()["models"]["1"])

    def test_dry_run_uses_local_index_without_downloading_models(self):
        self.args.dry_run = True
        self.args.ids = [2, 1]
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        self.assertFalse((self.batch_dir / "models").exists())
        self.assertEqual(self.manifest()["last_selection"], ["2", "1"])
        self.assertEqual(self.manifest()["models"], {})
        self.assertFalse((self.batch_dir / ".download.lock").exists())
        self.network.assert_not_called()

    def test_changed_index_rejects_resume_without_overwriting_manifest(self):
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        manifest_path = self.batch_dir / "manifest.json"
        before = manifest_path.read_bytes()
        self.index.write_bytes(index_bytes([make_row(1), make_row(4)]))
        with self.assertRaises(ValueError):
            downloader.run(self.args)
        self.assertEqual(manifest_path.read_bytes(), before)
        self.assertFalse((self.batch_dir / ".download.lock").exists())

    def test_unknown_id_does_not_create_model_directory_or_manifest(self):
        self.args.ids = [999]
        with self.assertRaises(ValueError):
            downloader.run(self.args)
        self.assertFalse((self.batch_dir / "models").exists())
        self.assertFalse((self.batch_dir / "manifest.json").exists())
        self.assertFalse((self.batch_dir / ".download.lock").exists())

    def test_two_batches_use_separate_directories_and_resume_independently(self):
        raw = index_bytes([make_row(number) for number in range(1, 6)])
        self.index.write_bytes(raw)
        second_dir = self.root / "batch-002"
        second_dir.mkdir()
        (second_dir / self.index.name).write_bytes(raw)
        self.args.ids = None
        self.args.count = 3
        with mock.patch.object(downloader, "download", side_effect=self.fake_download) as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 3)
            self.args.batch = 2
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 5)
        first, second = self.manifest(1), self.manifest(2)
        self.assertTrue(set(first["models"]).isdisjoint(second["models"]))
        self.assertEqual(len(second["models"]), 2)
        for batch, manifest in ((1, first), (2, second)):
            self.assertEqual(manifest["batch_settings"], {
                "number": batch, "mode": "random", "count": 3, "seed": 2026,
            })
            directory = self.root / f"batch-{batch:03d}"
            for entry in manifest["models"].values():
                self.assertTrue((directory / entry["path"]).is_file())
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.args.batch = 1
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        self.assertFalse((self.root / "models").exists())
        self.assertFalse((self.root / "manifest.json").exists())

    def test_legacy_root_files_remain_untouched(self):
        legacy_manifest = self.root / "manifest.json"
        legacy_index = self.root / self.index.name
        legacy_models = self.root / "models"
        legacy_models.mkdir()
        legacy_model = legacy_models / "1.stl"
        originals = {
            legacy_manifest: b"unrelated legacy manifest",
            legacy_index: b"legacy index",
            legacy_model: b"original user mesh",
        }
        for path, payload in originals.items():
            path.write_bytes(payload)
        with mock.patch.object(downloader, "download", side_effect=self.fake_download):
            self.assertEqual(downloader.run(self.args), 0)
        for path, payload in originals.items():
            self.assertEqual(path.read_bytes(), payload)
        self.assertEqual(self.manifest()["models"]["1"]["status"], "downloaded")

    def test_changed_random_batch_settings_reject_resume_without_replacing_manifest(self):
        self.args.ids = None
        self.args.count = 2
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        manifest_path = self.batch_dir / "manifest.json"
        before = manifest_path.read_bytes()
        for changes in ({"count": 1}, {"seed": 123}, {"ids": [1]}):
            with self.subTest(changes=changes):
                args = argparse.Namespace(**{**vars(self.args), **changes})
                with mock.patch.object(downloader, "download") as transfer:
                    with self.assertRaises(ValueError):
                        downloader.run(args)
                    transfer.assert_not_called()
                self.assertEqual(manifest_path.read_bytes(), before)
                self.assertFalse((self.batch_dir / ".download.lock").exists())

    def test_changed_explicit_ids_reject_resume_without_replacing_manifest(self):
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        manifest_path = self.batch_dir / "manifest.json"
        before = manifest_path.read_bytes()
        self.args.ids = [3, 1]
        with self.assertRaises(ValueError):
            downloader.run(self.args)
        self.assertEqual(manifest_path.read_bytes(), before)

    def test_explicit_ids_batch_is_storage_only_and_records_normalized_ids(self):
        directory = self.root / "batch-042"
        directory.mkdir()
        (directory / self.index.name).write_bytes(index_bytes())
        self.args.batch = 42
        self.args.ids = [3, "02", 3]
        with mock.patch.object(downloader, "download", side_effect=self.fake_download) as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 2)
        manifest = self.manifest(42)
        self.assertEqual(manifest["last_selection"], ["3", "2"])
        self.assertEqual(manifest["batch_settings"], {"number": 42, "mode": "ids", "ids": ["3", "2"]})
        self.assertEqual(set(manifest["models"]), {"3", "2"})
        self.args.ids = ["03", 2]
        self.args.count = 1
        self.args.seed = 19
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()

    def test_batch_directory_symlink_is_rejected_before_writing_target(self):
        destination = self.root / "not-a-batch"
        destination.mkdir()
        marker = destination / "keep.txt"
        marker.write_bytes(b"must not change")
        link = self.root / "batch-009"
        try:
            link.symlink_to(destination, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Creating directory symlinks is unavailable: {error}")
        self.args.batch = 9
        with mock.patch.object(downloader, "download") as transfer:
            with self.assertRaises(ValueError):
                downloader.run(self.args)
            transfer.assert_not_called()
        self.assertTrue(link.is_symlink())
        self.assertEqual(marker.read_bytes(), b"must not change")
        self.assertEqual(list(destination.iterdir()), [marker])

    def test_old_all_manifest_without_new_metadata_can_resume_in_original_directory(self):
        self.args.ids = None
        with mock.patch.object(downloader, "download", side_effect=self.fake_download):
            self.assertEqual(downloader.run(self.args), 0)
        old_manifest = self.manifest()
        old_manifest.pop("selection", None)
        for model in old_manifest["models"].values():
            for key in ("metadata_defects", "metadata_defect_labels", "verification_status"):
                model.pop(key, None)
        (self.batch_dir / "manifest.json").write_text(json.dumps(old_manifest), encoding="utf-8")
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        manifest = self.manifest()
        self.assertEqual(manifest["batch_settings"], {
            "number": 1, "mode": "random", "count": 3, "seed": 2026,
        })
        self.assertEqual(set(manifest["models"]), {"1", "2", "3"})
        self.assertFalse((self.root / "all").exists())


class FilteredRunTests(OfflineTests):
    def setUp(self):
        super().setUp()
        self.args = argparse.Namespace(
            output=self.root, count=100, seed=2026, ids=None,
            timeout=7, retries=1, max_mib=8, dry_run=False, batch=1, subset="defects",
        )
        self.raw_index = index_bytes([
            make_row(1, Closed="FALSE"),
            make_row(2, **{"No duplicated faces": "FALSE"}),
            make_row(3, **{"No degenerate faces": "FALSE"}),
            make_row(4),
            make_row(5, **{"Single Component": "FALSE"}),
        ])
        self.directory = self.seed_index()

    def seed_index(self, subset="defects", batch=1):
        directory = self.root / subset / f"batch-{batch:03d}"
        directory.mkdir(parents=True)
        (directory / f"input_summary-{downloader.REVISION}.csv").write_bytes(self.raw_index)
        return directory

    def manifest(self, directory=None):
        return json.loads(((directory or self.directory) / "manifest.json").read_text(encoding="utf-8"))

    def test_filtered_downloads_record_reasons_and_remain_explicitly_unverified(self):
        with mock.patch.object(downloader, "download",
                               side_effect=lambda url, target, *args: target.write_bytes(RAW_MODEL)) as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            self.assertEqual(transfer.call_count, 3)
        manifest = self.manifest()
        self.assertEqual(manifest["batch_settings"], {
            "number": 1, "mode": "filtered", "subset": "defects", "count": 100, "seed": 2026,
        })
        self.assertEqual({entry["id"] for entry in manifest["selection"]}, {"1", "2", "3"})
        self.assertEqual(set(manifest["models"]), {"1", "2", "3"})
        for entry in manifest["models"].values():
            self.assertEqual(entry["status"], "downloaded")
            self.assertEqual(entry["verification_status"], "not_run")
            self.assertEqual(len(entry["metadata_defects"]), 1)
            self.assertEqual(len(entry["metadata_defect_labels"]), 1)
            self.assertEqual((self.directory / entry["path"]).read_bytes(), RAW_MODEL)
        with mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        self.assertFalse((self.root / "batch-001").exists())

    def test_dry_run_saves_full_selection_and_explains_metadata_without_mesh_download(self):
        self.args.dry_run = True
        output = io.StringIO()
        with contextlib.redirect_stdout(output), mock.patch.object(downloader, "download") as transfer:
            self.assertEqual(downloader.run(self.args), 0)
            transfer.assert_not_called()
        manifest = self.manifest()
        self.assertEqual(manifest["models"], {})
        self.assertEqual([item["id"] for item in manifest["selection"]], manifest["last_selection"])
        self.assertEqual(len(manifest["selection"]), 3)
        for item in manifest["selection"]:
            self.assertIn("dataset_flags", item)
            self.assertIn("download_url", item)
            self.assertEqual(item["verification_status"], "not_run")
            self.assertIn(item["filename"], output.getvalue())
            self.assertIn(item["metadata_defect_labels"][0], output.getvalue())
        self.assertFalse((self.directory / "models").exists())
        self.network.assert_not_called()

    def test_filtered_batches_page_and_keep_independent_manifests(self):
        second_directory = self.seed_index(batch=2)
        self.args.count = 2
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        self.args.batch = 2
        self.assertEqual(downloader.run(self.args), 0)
        first, second = self.manifest(), self.manifest(second_directory)
        self.assertEqual(len(first["selection"]), 2)
        self.assertEqual(len(second["selection"]), 1)
        self.assertTrue(set(first["last_selection"]).isdisjoint(second["last_selection"]))

    def test_different_subset_and_old_random_batch_files_remain_separate(self):
        old_directory = self.root / "batch-001"
        old_directory.mkdir()
        old_manifest = old_directory / "manifest.json"
        old_manifest.write_bytes(b"old random download must stay untouched")
        boundary_directory = self.seed_index(subset="boundary")
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        defects_before = (self.directory / "manifest.json").read_bytes()
        self.args.subset = "boundary"
        self.assertEqual(downloader.run(self.args), 0)
        self.assertEqual(self.manifest(boundary_directory)["last_selection"], ["1"])
        self.assertEqual((self.directory / "manifest.json").read_bytes(), defects_before)
        self.assertEqual(old_manifest.read_bytes(), b"old random download must stay untouched")

    def test_filtered_configuration_change_rejects_resume_without_overwrite(self):
        self.args.dry_run = True
        self.assertEqual(downloader.run(self.args), 0)
        manifest_path = self.directory / "manifest.json"
        before = manifest_path.read_bytes()
        for changes in ({"count": 1}, {"seed": 999}):
            with self.subTest(changes=changes):
                args = argparse.Namespace(**{**vars(self.args), **changes})
                with self.assertRaises(ValueError):
                    downloader.run(args)
                self.assertEqual(manifest_path.read_bytes(), before)

    def test_subset_directory_symlink_is_rejected_before_writing_destination(self):
        destination = self.root / "preserved"
        destination.mkdir()
        marker = destination / "keep.txt"
        marker.write_bytes(b"original")
        link = self.root / "boundary"
        try:
            link.symlink_to(destination, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Creating directory symlinks is unavailable: {error}")
        self.args.subset = "boundary"
        with mock.patch.object(downloader, "download") as transfer:
            with self.assertRaises(ValueError):
                downloader.run(self.args)
            transfer.assert_not_called()
        self.assertEqual(list(destination.iterdir()), [marker])
        self.assertEqual(marker.read_bytes(), b"original")


class CommandLineTests(OfflineTests):
    def test_default_batch_is_one(self):
        with mock.patch.object(downloader, "run", return_value=0) as run:
            self.assertEqual(downloader.main(["--dry-run"]), 0)
        self.assertEqual(run.call_args.args[0].batch, 1)
        self.assertEqual(run.call_args.args[0].subset, "defects")
        self.assertTrue(run.call_args.args[0].dry_run)
        self.network.assert_not_called()

    def test_explicit_batch_number_is_parsed(self):
        with mock.patch.object(downloader, "run", return_value=0) as run:
            self.assertEqual(downloader.main(["--batch", "42", "--ids", "3", "2"]), 0)
        self.assertEqual(run.call_args.args[0].batch, 42)
        self.assertEqual(run.call_args.args[0].ids, [3, 2])
        self.assertEqual(run.call_args.args[0].subset, "all")

    def test_explicit_subset_options_are_parsed(self):
        for subset in ("all", "defects", "metadata-clean", *DEFECT_SUBSETS):
            with self.subTest(subset=subset), mock.patch.object(downloader, "run", return_value=0) as run:
                self.assertEqual(downloader.main(["--subset", subset, "--count", "5"]), 0)
                self.assertEqual(run.call_args.args[0].subset, subset)
                self.assertEqual(run.call_args.args[0].count, 5)

    def test_explicit_ids_conflict_with_every_explicit_subset_including_all(self):
        for subset in ("all", "defects", "metadata-clean", *DEFECT_SUBSETS):
            with self.subTest(subset=subset), mock.patch.object(downloader, "run") as run:
                with self.assertRaises(SystemExit) as error:
                    downloader.main(["--subset", subset, "--ids", "1", "2"])
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()

    def test_unknown_subset_fails_before_running(self):
        with mock.patch.object(downloader, "run") as run:
            with self.assertRaises(SystemExit) as error:
                downloader.main(["--subset", "non-existent"])
            self.assertEqual(error.exception.code, 2)
            run.assert_not_called()

    def test_invalid_batch_numbers_fail_before_running(self):
        for value in ("0", "-1", "not-an-integer"):
            with self.subTest(value=value), mock.patch.object(downloader, "run") as run:
                with self.assertRaises(SystemExit) as error:
                    downloader.main(["--batch", value])
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
