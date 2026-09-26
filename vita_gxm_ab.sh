#!/usr/bin/env bash
# Deploy/run/collect for the gxm-optimization testbed through vitacompanion.
#
#   ./vita_gxm_ab.sh run <label> <run#> [seconds]  install ab-artifacts/<label>, benchmark, fetch CSV
#   ./vita_gxm_ab.sh restore                        reinstall the eboot backed up before testing
#
# vitacompanion: FTP on :1337, command server on :1338. Only ux0:app/SMSVITA01/eboot.bin
# and ux0:data/strikersVita/strikers.ini are written; no game data or saves are touched.
set -euo pipefail

VITA_IP="${VITA_IP:-192.168.1.79}"
FTP="ftp://$VITA_IP:1337"
TITLE=SMSVITA01
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ART="$ROOT_DIR/ab-artifacts"
BACKUP="$ART/device-backup/eboot.bin.installed-20260926"
RESULTS="$ART/results"

cmd() { printf '%s\n' "$*" | nc -w 3 "$VITA_IP" 1338 >/dev/null 2>&1 || true; }
put() { curl -sS --max-time 180 -T "$1" "$FTP/$2"; }
get() { curl -sS --max-time 180 -o "$2" "$FTP/$1"; }
sha() { shasum -a 256 "$1" | cut -d' ' -f1; }

install_eboot() {
  local self="$1" check
  cmd destroy; sleep 2
  put "$self" "ux0:/app/$TITLE/eboot.bin"
  check="$(mktemp)"; get "ux0:/app/$TITLE/eboot.bin" "$check"
  if [[ "$(sha "$check")" != "$(sha "$self")" ]]; then
    rm -f "$check"; echo "eboot read-back hash mismatch" >&2; exit 1
  fi
  rm -f "$check"
  echo "installed eboot $(sha "$self")"
}

case "${1:-}" in
run)
  LABEL="${2:?label}"; RUN="${3:?run number}"; SECONDS_RUN="${4:-150}"
  SELF="$ART/$LABEL/strikers_vita.self"; [[ -f "$SELF" ]] || { echo "missing $SELF" >&2; exit 1; }
  CSV="bench-$LABEL-$RUN.csv"
  mkdir -p "$RESULTS"
  install_eboot "$SELF"
  ini="$(mktemp)"
  printf 'benchmark = 1\nbenchmark_seconds = %s\nbench_record = ux0:data/strikersVita/%s\n' \
    "$SECONDS_RUN" "$CSV" > "$ini"
  # EXTRA_INI: additional ini lines separated by ';' (e.g. "cpu_mhz = 444;gpu_mhz = 222").
  [[ -n "${EXTRA_INI:-}" ]] && tr ';' '\n' <<<"$EXTRA_INI" >> "$ini"
  put "$ini" "ux0:/data/strikersVita/strikers.ini"; rm -f "$ini"
  curl -sS --max-time 10 -Q "DELE ux0:/data/strikersVita/$CSV" "$FTP/" >/dev/null 2>&1 || true
  # Diagnostic logs (profiling builds only): start from empty files for this run.
  curl -sS --max-time 10 -Q "DELE ux0:/data/strikersVita/aurora_telemetry.log" "$FTP/" >/dev/null 2>&1 || true
  collect_logs() {
    mkdir -p "$RESULTS/$LABEL-$RUN"
    get "ux0:/data/strikersVita/runtime.log" "$RESULTS/$LABEL-$RUN/runtime.log" 2>/dev/null || true
    get "ux0:/data/strikersVita/aurora_telemetry.log" "$RESULTS/$LABEL-$RUN/aurora_telemetry.log" 2>/dev/null || true
  }
  cmd launch "$TITLE"
  echo "launched $LABEL run $RUN; waiting for $CSV (benchmark ${SECONDS_RUN}s + boot/loading)"
  deadline=$(( $(date +%s) + SECONDS_RUN + 600 ))
  while (( $(date +%s) < deadline )); do
    sleep 15
    if curl -s --max-time 10 "$FTP/ux0:/data/strikersVita/" | grep -q " $CSV\$"; then
      sleep 10  # let the writer close the file
      get "ux0:/data/strikersVita/$CSV" "$RESULTS/$CSV"
      { echo "# label=$LABEL run=$RUN eboot_sha256=$(sha "$SELF") collected=$(date -u +%FT%TZ)"
        cat "$RESULTS/$CSV"; } > "$RESULTS/$CSV.tmp" && mv "$RESULTS/$CSV.tmp" "$RESULTS/$CSV"
      echo "collected $RESULTS/$CSV"
      collect_logs
      exit 0
    fi
  done
  collect_logs
  echo "timed out waiting for $CSV (crash or match not reached?); logs in $RESULTS/$LABEL-$RUN" >&2; exit 1 ;;
restore)
  install_eboot "$BACKUP"
  curl -sS --max-time 10 -Q "DELE ux0:/data/strikersVita/strikers.ini" "$FTP/" >/dev/null 2>&1 || true
  echo "restored original eboot and removed strikers.ini" ;;
*)
  sed -n '2,8p' "$0"; exit 2 ;;
esac
