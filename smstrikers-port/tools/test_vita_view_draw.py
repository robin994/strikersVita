#!/usr/bin/env python3
"""Independent corruption/attribution fixtures for the versioned draw trace."""

import json
from pathlib import Path
import tempfile
import unittest

from analyze_vita_view_draw import analyze, SCHEMA
from seal_vita_view_draw import seal
from compare_vita_view_draw import compare

SHA = "a" * 64


def sample(payload=False):
    first = {"type": "header", "schema": SCHEMA, "capture_id": "fixture-20261008",
             "self_sha256": SHA, "ini_sha256": "b" * 64,
             "shader_cache_sha256": "c" * 64, "capacity": 32}

    def view(seq, frame, index):
        return {"type": "view", "sequence": seq, "producer_frame": frame, "view": index}

    def draw(seq, frame, index, h, ordinal):
        return {"type": "draw", "sequence": seq, "producer_frame": frame, "consumer_frame": frame,
                "view": index, "logical_draw": ordinal, "target": 0, "tev_stages": 2,
                "texture_mask": 1, "texgen_mask": 1, "vertex_count": 120,
                "index_count": 210, "submit_cpu_us": 28, "uniform_bytes": 80,
                "program_native": 1, "indexed_pn": 1, "blend": 1, "depth": 3,
                "alpha": 2, "alpha_ref0": 128, "alpha_ref1": 64, "alpha_op": 0, "scissor": 0,
                "pipeline_requested": "0123456789abcdef", "pipeline_active": "0123456789abcdef",
                "vertex_hash": "bbbbbbbbbbbbbbbb", "fragment_hash": h,
                "payload_hashes_present": 1 if payload else 0,
                "vertex_payload_hash": "1000000000000001" if payload else "0" * 16,
                "index_payload_hash": "2000000000000002" if payload else "0" * 16,
                "uniform_payload_hash": "3000000000000003" if payload else "0" * 16,
                "draw_state_hash": "4000000000000004" if payload else "0" * 16}

    events = [view(1, 100, 3), draw(2, 100, 3, "aaaa0000aaaa0000", 1),
              view(3, 100, 11), draw(4, 100, 11, "bbbb0000bbbb0000", 2),
              {"type": "frame_complete", "sequence": 5, "producer_frame": 100, "consumer_frame": 100},
              view(6, 101, 3), draw(7, 101, 3, "aaaa0000aaaa0000", 1),
              {"type": "frame_complete", "sequence": 8, "producer_frame": 101, "consumer_frame": 101}]
    end = {"type": "footer", "records": len(events), "last_sequence": len(events),
           "dropped": 0, "truncated": False}
    return [first, *events, end]


class ViewDrawAnalysisTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.file = Path(tmp.name) / "view_draw.jsonl"

    def parse(self, records, terminal=True):
        payload = "\n".join(json.dumps(record, sort_keys=True) for record in records)
        self.file.write_text(payload + ("\n" if terminal else ""))
        return analyze(self.file)

    def test_ordered_view_attribution_and_total(self):
        report = self.parse(sample())
        self.assertTrue(report["complete"])
        self.assertEqual(report["completed_frames"], 2)
        self.assertEqual(report["views"]["Shadowed"],
                         {"draws": 2, "vertices": 240, "indices": 420, "submit_cpu_us": 56})
        self.assertEqual(report["views"]["Characters"]["draws"], 1)
        self.assertEqual(report["top_fragment_programs"][0]["fragment_hash"], "aaaa0000aaaa0000")
        self.assertEqual(report["draws_outside_named_views"], 0)
        self.assertNotIn("fps", report)

    def test_truncated_missing_footer_and_loss_are_never_complete(self):
        fixture = sample()
        with self.assertRaisesRegex(ValueError, "terminal newline"):
            self.parse(fixture, terminal=False)
        with self.assertRaisesRegex(ValueError, "footer"):
            self.parse(fixture[:-1])
        fixture[-1]["dropped"] = 1
        with self.assertRaisesRegex(ValueError, "Partial capture"):
            self.parse(fixture)

    def test_empty_gameplay_capture_rejected(self):
        fixture = sample()
        fixture[1:-1] = [
            {"type": "view", "sequence": 1, "producer_frame": 100, "view": 3},
            {"type": "frame_complete", "sequence": 2,
             "producer_frame": 100, "consumer_frame": 100},
        ]
        fixture[-1]["records"] = 2
        fixture[-1]["last_sequence"] = 2
        with self.assertRaisesRegex(ValueError, "no GXM draw"):
            self.parse(fixture)

    def test_gaps_duplicates_and_wrong_markers_rejected(self):
        fixture = sample()
        fixture[3]["sequence"] = 9
        with self.assertRaisesRegex(ValueError, "Noncontiguous"):
            self.parse(fixture)
        fixture = sample()
        fixture[2]["view"] = 11
        with self.assertRaisesRegex(ValueError, "matching ordered view"):
            self.parse(fixture)
        fixture = sample()
        fixture[3]["producer_frame"] = 99
        with self.assertRaisesRegex(ValueError, "backwards"):
            self.parse(fixture)

    def test_identity_and_unfinished_frame_rejected(self):
        fixture = sample()
        fixture[0]["ini_sha256"] = "unknown"
        with self.assertRaisesRegex(ValueError, "ini_sha256"):
            self.parse(fixture)
        fixture = sample()
        fixture.pop(-2)  # remove the second frame-completion marker
        fixture[-1]["records"] -= 1
        fixture[-1]["last_sequence"] -= 1
        with self.assertRaisesRegex(ValueError, "incomplete"):
            self.parse(fixture)

    def test_draw_ordinals_and_consumer_frame_must_be_consistent(self):
        fixture = sample()
        fixture[4]["logical_draw"] = 3
        with self.assertRaisesRegex(ValueError, "logical draw"):
            self.parse(fixture)
        fixture = sample()
        fixture[4]["consumer_frame"] = 101
        with self.assertRaisesRegex(ValueError, "consumer frame"):
            self.parse(fixture)
        fixture = sample()
        fixture[5]["consumer_frame"] = 101
        with self.assertRaisesRegex(ValueError, "Consumer frame changed"):
            self.parse(fixture)
        fixture = sample()
        fixture[7]["consumer_frame"] = 100
        fixture[8]["consumer_frame"] = 100
        with self.assertRaisesRegex(ValueError, "did not advance"):
            self.parse(fixture)

    def test_false_time_and_boolean_fields_rejected(self):
        fixture = sample()
        fixture[2]["submit_cpu_us"] = True
        with self.assertRaisesRegex(ValueError, "submit_cpu_us"):
            self.parse(fixture)
        fixture = sample()
        fixture[-1]["records"] += 1
        with self.assertRaisesRegex(ValueError, "count mismatch"):
            self.parse(fixture)

    def test_raw_seal_requires_actual_files_and_full_sequence(self):
        source = sample()
        payload = "# aurora-vita-view-draw-raw-v2\n" + "\n".join(
            json.dumps(e) for e in source[1:-1]) + "\n# end records=8 dropped=0 capacity=32\n"
        raw = self.file.with_name("raw.jsonl")
        raw.write_text(payload)
        binary = self.file.with_name("eboot.bin")
        ini = self.file.with_name("strikers.ini")
        cache = self.file.with_name("shader-cache.bin")
        for item in (binary, ini, cache):
            item.write_bytes(item.name.encode())
        result = seal(raw, self.file, binary, ini, cache, "fixture-capture")
        self.assertEqual(result["views"]["Characters"]["draws"], 1)
        self.assertEqual(result["identities"]["self_sha256"], __import__("hashlib").sha256(binary.read_bytes()).hexdigest())
        raw.write_text(payload.replace("dropped=0", "dropped=2"))
        with self.assertRaisesRegex(ValueError, "lost records"):
            seal(raw, self.file, binary, ini, cache, "bad-capture")

    def test_structural_comparison_accepts_only_identical_draw_sequences(self):
        control = self.file.with_name("control.jsonl")
        candidate = self.file.with_name("candidate.jsonl")

        def write(path, source):
            path.write_text("\n".join(json.dumps(row) for row in source) + "\n")

        write(control, sample())
        altered = sample()
        # Different capture timing and producer IDs are harmless for a
        # deterministic workload with otherwise identical draw submissions.
        for row in altered[1:-1]:
            row["producer_frame"] += 140
            if "consumer_frame" in row:
                row["consumer_frame"] += 270
            if row["type"] == "draw":
                row["submit_cpu_us"] += 99
                row["uniform_bytes"] += 64
        write(candidate, altered)
        self.assertTrue(compare(control, candidate)["structurally_equivalent"])

        altered[4]["fragment_hash"] = "0000aaaa00000000"
        write(candidate, altered)
        differences = compare(control, candidate)
        self.assertFalse(differences["structurally_equivalent"])
        self.assertEqual(differences["first_different_event"], 4)
        altered = sample()
        altered[0]["shader_cache_sha256"] = "d" * 64
        write(candidate, altered)
        with self.assertRaisesRegex(ValueError, "shader_cache_sha256"):
            compare(control, candidate)

    def test_ini_change_requires_explicit_gate(self):
        control = self.file.with_name("control.jsonl")
        candidate = self.file.with_name("candidate.jsonl")
        control.write_text("\n".join(json.dumps(row) for row in sample()) + "\n")
        altered = sample()
        altered[0]["ini_sha256"] = "e" * 64
        candidate.write_text("\n".join(json.dumps(row) for row in altered) + "\n")
        with self.assertRaisesRegex(ValueError, "INI differs"):
            compare(control, candidate)
        self.assertTrue(compare(control, candidate, allow_ini_change=True)["structurally_equivalent"])

    def test_optional_payload_gate_detects_skinning_changes(self):
        control = self.file.with_name("control.jsonl")
        candidate = self.file.with_name("candidate.jsonl")
        def write(path, rows):
            path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
        write(control, sample())
        with self.assertRaisesRegex(ValueError, "Payload hashing required"):
            compare(control, control, require_payloads=True)
        write(control, sample(payload=True))
        baseline = compare(control, control, require_payloads=True)
        self.assertTrue(baseline["structurally_equivalent"])
        self.assertTrue(baseline["payload_hashes_compared"])
        altered = sample(payload=True)
        altered[2]["vertex_payload_hash"] = "5555555555555555"
        write(candidate, altered)
        mismatch = compare(control, candidate, require_payloads=True)
        self.assertFalse(mismatch["structurally_equivalent"])
        self.assertEqual(mismatch["first_different_event"], 2)
        altered[2]["vertex_payload_hash"] = "1000000000000001"
        altered[2]["uniform_payload_hash"] = "6666666666666666"
        write(candidate, altered)
        self.assertFalse(compare(control, candidate, require_payloads=True)["structurally_equivalent"])

        write(candidate, sample())
        with self.assertRaisesRegex(ValueError, "mode differs"):
            compare(control, candidate)

    def test_capture_rejects_inconsistent_or_missing_payload_hashes(self):
        fixture = sample(payload=True)
        fixture[4]["payload_hashes_present"] = 0
        with self.assertRaisesRegex(ValueError, "Inconsistent payload hash coverage"):
            self.parse(fixture)
        fixture = sample(payload=True)
        fixture[4]["index_payload_hash"] = "0" * 16
        with self.assertRaisesRegex(ValueError, "Missing enabled payload"):
            self.parse(fixture)
        fixture = sample()
        fixture[2]["payload_hashes_present"] = 1
        with self.assertRaisesRegex(ValueError, "Missing enabled payload"):
            self.parse(fixture)


if __name__ == "__main__":
    unittest.main()
