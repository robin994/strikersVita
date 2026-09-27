#!/usr/bin/env bash
# ./vita_bench_watch.sh <label> <run#> [seconds]   (EXTRA_INI passed through)
# Runs vita_gxm_ab.sh and watches the OBS picture: an identical frame for
# FREEZE_S seconds during the run counts as a hang and the app is destroyed
# (before the console goes down).
set -uo pipefail
LABEL="$1"; RUN="$2"; SECS="${3:-150}"; FREEZE_S="${FREEZE_S:-60}"
ROOT="$(cd "$(dirname "$0")" && pwd)"; SHOTS="$ROOT/ab-artifacts/results/$LABEL-$RUN-shots"
mkdir -p "$SHOTS"; LOG="$ROOT/ab-artifacts/run-$LABEL-$RUN.log"
"$ROOT/vita_gxm_ab.sh" run "$LABEL" "$RUN" "$SECS" > "$LOG" 2>&1 &
AB=$!
last=""; same=0; i=0
while kill -0 $AB 2>/dev/null; do
  sleep 10; i=$((i+1))
  "$ROOT/vita_shot.sh" "$SHOTS/s$(printf %03d $i).png" 2>/dev/null || continue
  h="$(ffmpeg -hide_banner -loglevel error -i "$SHOTS/s$(printf %03d $i).png" -f md5 - 2>/dev/null)"
  st="$(ffmpeg -hide_banner -i "$SHOTS/s$(printf %03d $i).png" -vf "signalstats,metadata=print" -f null - 2>&1)"
  ymin="$(sed -n 's/.*YMIN=\([0-9]*\).*/\1/p' <<<"$st" | head -1)"; ymax="$(sed -n 's/.*YMAX=\([0-9]*\).*/\1/p' <<<"$st" | head -1)"
  # A uniform picture (black/grey) means no capture signal: not evidence of a hang.
  if (( ${ymax:-0} - ${ymin:-0} < 24 )); then same=0; last=""; continue; fi
  if [[ "$h" == "$last" ]]; then same=$((same+10)); else same=0; last="$h"; fi
  if (( same >= FREEZE_S )); then
    echo "FREEZE detected at shot $i (${same}s identical); destroying app" | tee -a "$LOG"
    printf 'destroy\n' | nc -w 3 192.168.1.79 1338 >/dev/null 2>&1
    kill $AB 2>/dev/null; break
  fi
done
wait $AB 2>/dev/null
cat "$LOG"
