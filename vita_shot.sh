#!/usr/bin/env bash
# Capture the PS Vita picture from the OBS preview (OBS window only, never the
# whole desktop) and crop it to the preview area.   ./vita_shot.sh out.png
set -euo pipefail
OUT="${1:?output png}"
WID="$(cat > "${TMPDIR:-/tmp}/obs_wid.swift" <<'SW'
import CoreGraphics
let l = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String:Any]]
for w in l where (w["kCGWindowOwnerName"] as? String) == "OBS Studio" {
  if let n = w["kCGWindowName"] as? String, n.hasPrefix("OBS ") { print(w["kCGWindowNumber"]!); break }
}
SW
swift "${TMPDIR:-/tmp}/obs_wid.swift" 2>/dev/null)"
[[ -n "$WID" ]] || { echo "OBS main window not found" >&2; exit 1; }
RAW="$(mktemp -t vitashot).png"
screencapture -x -o -l "$WID" "$RAW"
# Preview rectangle for the current OBS layout (1920x1050 window, 16:9 preview).
ffmpeg -hide_banner -loglevel error -y -i "$RAW" -vf "crop=${CROP:-1130:640:388:40},scale=960:-1" "$OUT"
rm -f "$RAW"
