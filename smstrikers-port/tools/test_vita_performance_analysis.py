import csv
import tempfile
import unittest
from pathlib import Path
from analyze_vita_performance import analyze


class PerformanceAnalysisTest(unittest.TestCase):
    def capture(self, rows):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "capture.csv"
        with path.open("w") as file:
            file.write("# strikers-consumer-capture-v1 diagnostics=1\n")
            writer = csv.DictWriter(file, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        return path

    def rows(self):
        return [dict(frame=f, gx_total_us=g, producer_wait_us=w, consumer_wait_us=2,
                     core3_total_pct_x100=p, core3_telemetry_valid=v)
                for f, g, w, p, v in [(100, 1000, 100, 1000, 1),
                                      (100, 1000, 100, 1000, 1),
                                      (102, 1200, 200, 2000, 0),
                                      (105, 2000, 300, 3000, 1)]]

    def test_repeated_skipped_frames_and_counter_semantics(self):
        result = analyze(self.capture(self.rows()))
        self.assertEqual(result["repeated_snapshots"], 1)
        self.assertEqual(result["unsampled_frames"], 3)
        self.assertEqual(result["cumulative_deltas"]["gx_total_us"], 1000)
        self.assertEqual(result["cumulative_endpoints"]["gx_total_us"],
                         {"first": 1000, "last": 2000})
        self.assertEqual(result["per_completed_frame"]["gx_total_us"], 200)
        self.assertEqual(result["frame_distributions"]["producer_wait_us"]["median"], 200)
        self.assertEqual(result["core3"]["maximum_observed_total_percent"], 30)
        self.assertFalse(result["core3"]["all_samples_valid"])
        self.assertNotIn("elapsed_fps", result)

    def test_counter_reset_and_backwards_frame_rejected(self):
        rows = self.rows()
        rows[-1]["gx_total_us"] = 1
        with self.assertRaisesRegex(ValueError, "counter reset"):
            analyze(self.capture(rows))
        rows = self.rows()
        rows[-1]["frame"] = 99
        with self.assertRaisesRegex(ValueError, "backwards"):
            analyze(self.capture(rows))


if __name__ == "__main__":
    unittest.main()
