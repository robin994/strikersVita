#!/usr/bin/env python3
"""Read a Razor CPU SQLite export without treating nested times or syncs as FPS."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import sqlite3


def analyze(path):
    path = Path(path).resolve(strict=True)
    # mode=ro also prevents accidentally creating an empty database on a typo.
    with sqlite3.connect(path.as_uri() + "?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row
        captures = db.execute("SELECT * FROM CpuCapture").fetchall()
        if len(captures) != 1:
            raise ValueError("Expected exactly one CpuCapture")
        capture = dict(captures[0])
        rate, length = capture["ClockRate"], capture["CaptureLength"]
        if rate <= 0 or length <= 0:
            raise ValueError("ClockRate and CaptureLength must be positive")
        function_row = db.execute(
            "SELECT ObjectTypeId FROM ObjectType WHERE ObjectTypeName='Function'").fetchone()
        if function_row is None:
            raise ValueError("Missing Function object type")
        function_type = function_row[0]
        params = (function_type, capture["GameProcessId"])
        scope = """r.ObjectTypeId=? AND r.ScheduledObjectId IN
            (SELECT ObjectId FROM ScheduledObject WHERE ProcessId=? AND ObjectId!=ProcessId)"""
        functions = []
        for row in db.execute("""SELECT r.ObjectId AS address, MAX(r.ObjectName) AS name,
                SUM(r.ExclusiveTime) AS exclusive_ticks, SUM(r.InclusiveTime) AS inclusive_ticks,
                SUM(r.ExclusiveCallCount) AS calls FROM FunctionRecord r WHERE """ + scope +
                " GROUP BY r.ObjectId ORDER BY exclusive_ticks DESC", params):
            item = dict(row)
            item.update(exclusive_ms=item["exclusive_ticks"] * 1000 / rate,
                        inclusive_ms=item["inclusive_ticks"] * 1000 / rate)
            functions.append(item)
        edges = []
        for row in db.execute("""SELECT r.ObjectId AS address, MAX(r.ObjectName) AS name,
                p.ObjectId AS parent_address, MAX(p.ObjectName) AS parent,
                r.ScheduledObjectId AS thread_id, MAX(s.ObjectName) AS thread,
                SUM(r.ExclusiveTime) AS exclusive_ticks, SUM(r.ExclusiveCallCount) AS calls
                FROM FunctionRecord r LEFT JOIN FunctionRecord p
                  ON p.FunctionRecordId=r.ParentFunctionRecordId
                LEFT JOIN ScheduledObject s ON s.ObjectId=r.ScheduledObjectId
                WHERE """ + scope + """ GROUP BY r.ObjectId,p.ObjectId,r.ScheduledObjectId
                ORDER BY exclusive_ticks DESC""", params):
            item = dict(row)
            item["exclusive_ms"] = item["exclusive_ticks"] * 1000 / rate
            edges.append(item)
        scheduled = []
        for row in db.execute("""SELECT b.CpuCoreId AS core, t.ProcessId AS process_id,
                t.ProcessThreadId AS thread_id, t.ThreadName AS thread, t.IsKernelIdle AS idle,
                SUM(MAX(0,MIN(b.Stop,?)-MAX(b.Start,0))) AS ticks, COUNT(*) AS slices
                FROM ThreadBar b JOIN Thread t ON t.ProcessThreadId=b.ProcessThreadId
                GROUP BY b.CpuCoreId,t.ProcessThreadId ORDER BY ticks DESC""", (length,)):
            item = dict(row)
            item.update(milliseconds=item["ticks"] * 1000 / rate,
                        capture_percent=item["ticks"] * 100 / length,
                        game=item["process_id"] == capture["GameProcessId"])
            scheduled.append(item)
        cores = []
        for core in sorted({item["core"] for item in scheduled}):
            items = [item for item in scheduled if item["core"] == core]
            cores.append(dict(core=core,
                              game_percent=sum(i["capture_percent"] for i in items if i["game"]),
                              idle_percent=sum(i["capture_percent"] for i in items if i["idle"]),
                              total_recorded_percent=sum(i["capture_percent"] for i in items)))
        syncs = [dict(row) for row in db.execute("""SELECT t.FrameTypeName AS type, COUNT(*) AS count
                FROM Frame f JOIN FrameType t ON t.FrameTypeId=f.FrameTypeId
                GROUP BY f.FrameTypeId ORDER BY f.FrameTypeId""")]
    return dict(schema_version=1, trace_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                capture=dict(title=capture["CaptureTitle"], executable=capture["ExecutablePath"],
                             capture_id=capture["CaptureId"], clock_hz=rate,
                             cpu_clock_hz=capture["CpuClockRate"], duration_seconds=length / rate,
                             created_utc=datetime.datetime.fromtimestamp(
                                 capture["CaptureFileCreationTime"], datetime.timezone.utc).isoformat()),
                fps=None, notes=["Inclusive times contain children; do not sum them.",
                                 "Scheduled CPU time includes instrumentation and kernel work while scheduled.",
                                 "Frame rows are sync records, not proof of presented game frames.",
                                 "The CPU export does not measure GPU execution or identify the source revision."],
                functions=functions, caller_edges=edges, scheduled_threads=scheduled,
                cores=cores, sync_records=syncs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("--output", type=Path, help="Write JSON to this file; default is stdout")
    args = parser.parse_args()
    if args.output and args.output.resolve() == args.database.resolve():
        parser.error("Output must not overwrite the input database")
    try:
        result = analyze(args.database)
    except (OSError, sqlite3.Error, ValueError, TypeError) as error:
        parser.exit(1, f"CPU trace analysis failed: {error}\n")
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
        print(f"Wrote {args.output}: {result['capture']['duration_seconds']:.6f}s, "
              f"{len(result['functions'])} functions")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
