#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from analyze_vita_frames import analyze

HEADER="# strikers-frame-capture-v1 diagnostics=0 fps_overlay=0\nsample,match,match_frame,frame_us,tasks_us,present_us,sleep_us\n"
class FrameCaptureTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.path=Path(self.tmp.name)/"frames.csv"
    def put(self,rows):
        self.path.write_text(HEADER+rows)
    def test_rate_and_tails(self):
        self.put("0,1,600,20000,19000,1000,0\n1,1,601,40000,38000,2000,0\n")
        r=analyze(self.path,2);self.assertAlmostEqual(r["elapsed_fps"],100./3.);self.assertEqual(r["median_ms"],30.)
        self.assertEqual(r["p95_ms"],40.);self.assertEqual(r["deadline_60_pct"],0.)
        self.assertEqual(r["deadline_30_pct"],50.);self.assertTrue(r["all_gameplay"])
    def test_incomplete(self):
        self.put("0,1,600,20000,19000,1000,0\n")
        with self.assertRaisesRegex(ValueError,"Incomplete"):analyze(self.path,2)
    def test_live_play_provenance(self):
        self.put("0,1,600,20000,19000,1000,0\n")
        with self.assertRaisesRegex(ValueError,"live-play state"):analyze(self.path,1,True)
        self.path.write_text("# play_only=1 counter=live_play\n"+self.path.read_text())
        self.assertTrue(analyze(self.path,1,True)["live_play_only"])
    def test_malformed_or_wrong_sequence(self):
        for row in ["zu,1,601,40000,38000,2000,0","2,1,601,40000,38000,2000,0","1,1,605,40000,38000,2000,0",
                    "1,1,601,40000,38000,2000,0,9","1,1,601,0,0,0,0","1,1,601,4,38000,2000,0"]:
            with self.subTest(row=row):
                self.put("0,1,600,20000,19000,1000,0\n"+row+"\n")
                with self.assertRaises(ValueError):analyze(self.path,2)
    def test_bad_columns(self):
        self.path.write_text("sample,frame_us\n0,2\n")
        with self.assertRaises(ValueError):analyze(self.path)
if __name__=="__main__":unittest.main()
