"""Compare the A-only and A+B benchmark runs.

For every document: page count and extracted text (pdftotext -layout) must be
identical, which is the semantic check for the precede-set change. Wall times
are reported as medians; they are not gated because CI runners are noisy.
Exit code 1 on any page-count or text mismatch.
"""

import argparse
import hashlib
import statistics
import subprocess
import sys
from pathlib import Path


def page_count(pdf: Path) -> int:
    out = subprocess.run(["pdfinfo", str(pdf)], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("Pages:"):
            return int(line.split(":", 1)[1].strip())
    raise SystemExit(f"no Pages field in {pdf}")


def text_hash(pdf: Path) -> str:
    out = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], check=True, capture_output=True).stdout
    return hashlib.sha256(out).hexdigest()


def read_times(path: Path) -> dict[str, list[float]]:
    times: dict[str, list[float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        name, value = line.split()
        times.setdefault(name, []).append(float(value))
    return times


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True, type=Path)
    parser.add_argument("--b", required=True, type=Path)
    parser.add_argument("--docs", required=True, nargs="+")
    args = parser.parse_args()

    times_a = read_times(args.a / "times.txt")
    times_b = read_times(args.b / "times.txt")
    failed = False

    print(f"{'document':32} {'pages':>5} {'A med':>8} {'B med':>8} {'B/A':>6}  layout")
    for doc in args.docs:
        pdf_a = args.a / f"{doc}.pdf"
        pdf_b = args.b / f"{doc}.pdf"
        pages_a, pages_b = page_count(pdf_a), page_count(pdf_b)
        hash_a, hash_b = text_hash(pdf_a), text_hash(pdf_b)
        same = pages_a == pages_b and hash_a == hash_b
        failed = failed or not same
        med_a = statistics.median(times_a[doc])
        med_b = statistics.median(times_b[doc])
        ratio = med_b / med_a if med_a else 0.0
        print(
            f"{doc:32} {pages_a:5d} {med_a:8.2f} {med_b:8.2f} {ratio:6.2f}  "
            f"{'identical' if same else 'DIFFERENT'}"
        )
        if not same:
            print(f"  pages: A={pages_a} B={pages_b}")
            print(f"  text:  A={hash_a} B={hash_b}")

    print("\nA med / B med are medians of the recorded conversions (seconds).")
    print("Ratios above 1 mean A+B was slower on this runner; timing is reported, not gated.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())