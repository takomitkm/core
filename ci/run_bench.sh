#!/bin/bash
# Convert the benchmark documents N times into a directory, recording wall time.
# Usage: run_bench.sh <output-dir>
set -euo pipefail

OUT="$1"
mkdir -p "$OUT"
TIMES="$OUT/times.txt"
: > "$TIMES"

DOCS="oversized-as-char-fly-grid fit-as-char-fly-grid bench-short-row bench-long-row"
REPS="${2:-3}"

for doc in $DOCS; do
  for i in $(seq 1 "$REPS"); do
    /usr/bin/time -f "$doc %e" -o "$TIMES" -a \
      timeout -k 15 600 instdir/program/soffice --headless --convert-to pdf \
        --outdir "$OUT" "sw/qa/core/layout/data/$doc.docx"
    test -f "$OUT/$doc.pdf"
  done
done

ls -la "$OUT"
echo "--- times ---"
cat "$TIMES"