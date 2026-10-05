import sqlite3
import tempfile
from pathlib import Path
import unittest

from analyze_vita_cpu_trace import analyze


class TraceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "trace #1.db"
        with sqlite3.connect(self.path) as db:
            db.executescript("""
                CREATE TABLE CpuCapture(CaptureTitle, ExecutablePath, CaptureId, ClockRate,
                    CpuClockRate, CaptureLength, CaptureFileCreationTime, GameProcessId);
                INSERT INTO CpuCapture VALUES('fixture','sample.self',1,1000,333000000,2000,0,10);
                CREATE TABLE ObjectType(ObjectTypeId,ObjectTypeName);
                INSERT INTO ObjectType VALUES(6,'Function');
                CREATE TABLE ScheduledObject(ObjectId,ProcessId,ObjectName);
                INSERT INTO ScheduledObject VALUES(10,10,'game'),(11,10,'main'),(12,20,'other');
                CREATE TABLE FunctionRecord(FunctionRecordId,ParentFunctionRecordId,ObjectId,
                    ObjectName,ObjectTypeId,ScheduledObjectId,ExclusiveTime,InclusiveTime,ExclusiveCallCount);
                INSERT INTO FunctionRecord VALUES
                    (1,NULL,100,'parent',6,11,200,600,1),
                    (2,1,101,'child',6,11,400,400,2),
                    (3,NULL,101,'child',6,12,999,999,3),
                    (4,NULL,999,'syscall',9,11,900,900,4),
                    (5,NULL,11,'thread record',3,11,900,900,4);
                CREATE TABLE Thread(ProcessThreadId,ProcessId,ThreadName,IsKernelIdle);
                INSERT INTO Thread VALUES(11,10,'main',0),(12,20,'idle',1);
                CREATE TABLE ThreadBar(CpuCoreId,ProcessThreadId,Start,Stop);
                INSERT INTO ThreadBar VALUES(0,11,-20,1000),(0,12,1000,2010);
                CREATE TABLE FrameType(FrameTypeId,FrameTypeName);
                INSERT INTO FrameType VALUES(2,'Controller Sync');
                CREATE TABLE Frame(FrameTypeId);
                INSERT INTO Frame VALUES(2),(2),(2);
            """)

    def test_scope_units_and_parent(self):
        before = self.path.read_bytes()
        result = analyze(self.path)
        self.assertEqual(before, self.path.read_bytes())
        self.assertEqual(result["capture"]["duration_seconds"], 2)
        self.assertEqual([f["name"] for f in result["functions"]], ["child", "parent"])
        self.assertEqual(result["functions"][0]["exclusive_ms"], 400)
        self.assertEqual(result["functions"][1]["inclusive_ms"], 600)
        self.assertEqual(result["caller_edges"][0]["parent"], "parent")
        self.assertEqual(result["caller_edges"][0]["calls"], 2)

    def test_schedule_clip_and_no_invented_fps(self):
        result = analyze(self.path)
        self.assertEqual(result["cores"][0]["game_percent"], 50)
        self.assertEqual(result["cores"][0]["idle_percent"], 50)
        self.assertIsNone(result["fps"])
        self.assertEqual(result["sync_records"], [dict(type="Controller Sync", count=3)])

    def test_missing_input_does_not_create_database(self):
        path = self.path.with_name("missing.db")
        with self.assertRaises(FileNotFoundError):
            analyze(path)
        self.assertFalse(path.exists())

    def test_bad_clock_rejected(self):
        with sqlite3.connect(self.path) as db:
            db.execute("UPDATE CpuCapture SET ClockRate=0")
        with self.assertRaises(ValueError):
            analyze(self.path)


if __name__ == "__main__":
    unittest.main()
