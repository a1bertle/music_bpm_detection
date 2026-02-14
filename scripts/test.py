#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import subprocess
import sys


ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
BPM_DETECT = os.path.join(BUILD_DIR, "bpm_detect")
YT_LIST = os.path.join(ROOT_DIR, "test_samples", "yt_testlist.txt")


LINE_RE = re.compile(
    r"""^\s*
    (?:-\s*)?
    (?P<url>(?:https?://)?(?:www\.)?\S+)
    \s*\((?P<label>.*)\)\s*
    \[(?P<bpm>\d+(?:\.\d+)?)\s+BPM[^\]]*\]
    \s*$""",
    re.VERBOSE,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run BPM detection tests (YouTube list by default)."
    )
    parser.add_argument(
        "--include-offline",
        action="store_true",
        help="Also run the offline MP3 test",
    )
    parser.add_argument(
        "--offline-only",
        action="store_true",
        help="Skip YouTube tests (useful without network access)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Do not build before running tests",
    )
    parser.add_argument(
        "--tolerance-pct",
        type=float,
        default=float(os.environ.get("TOLERANCE_PCT", "3.0")),
        help="Percent error tolerance (default: 3.0 or TOLERANCE_PCT env var)",
    )
    return parser.parse_args()


def normalize_url(url: str) -> str:
    if url.startswith("http://") or url.startswith("https://"):
        return url
    return f"https://{url}"


def run_cmd(cmd: list[str]) -> subprocess.CompletedProcess:
    print(f'CMD: {" ".join(f"{c!s}" for c in cmd)}')
    return subprocess.run(cmd, text=True, capture_output=True)


def extract_detected_bpm(output: str) -> str | None:
    for line in output.splitlines():
        if line.startswith("Detected BPM:"):
            parts = line.split()
            if len(parts) >= 3:
                return parts[2]
    return None


def run_detect(
    bpm_detect: str,
    input_arg: str,
    expected: float,
    label: str,
    tolerance_pct: float,
) -> tuple[bool, str]:
    cmd = [bpm_detect, "-v", "-o", "/dev/null", input_arg]
    result = run_cmd(cmd)
    if result.returncode != 0:
        return False, f"FAILED: bpm_detect error for {label}\n{result.stderr.strip()}"

    detected_str = extract_detected_bpm(result.stdout)
    if not detected_str:
        return False, f"FAILED: No detected BPM reported for {label}"

    try:
        detected = float(detected_str)
    except ValueError:
        return False, f"FAILED: Invalid detected BPM '{detected_str}' for {label}"

    if expected <= 0:
        return False, f"FAILED: Invalid expected BPM ({expected}) for {label}"

    pct_error = abs(detected - expected) / expected * 100.0
    print(
        f"Detected: {detected:.3f} BPM | Expected: {expected:.3f} BPM | "
        f"Error: {pct_error:.2f}% (tolerance {tolerance_pct:.2f}%)"
    )
    if pct_error <= tolerance_pct:
        return True, ""
    return False, f"FAILED: {label} outside tolerance"


def main() -> int:
    args = parse_args()

    if not args.skip_build:
        build_script = os.path.join(ROOT_DIR, "scripts", "build.sh")
        result = run_cmd([build_script])
        if result.returncode != 0:
            print(result.stderr.strip(), file=sys.stderr)
            return result.returncode

    if not (os.path.isfile(BPM_DETECT) and os.access(BPM_DETECT, os.X_OK)):
        print(f"ERROR: {BPM_DETECT} not found. Build first.", file=sys.stderr)
        return 1

    pass_count = 0
    fail_count = 0

    if args.include_offline or args.offline_only:
        print("==> Offline MP3 test")
        ok, msg = run_detect(
            BPM_DETECT,
            os.path.join(ROOT_DIR, "test_samples", "Foals - My Number (Official Audio).mp3"),
            128.0,
            "Foals - My Number (local MP3)",
            args.tolerance_pct,
        )
        if ok:
            pass_count += 1
        else:
            print(msg, file=sys.stderr)
            fail_count += 1

    run_yt = not args.offline_only
    if run_yt:
        if shutil.which("yt-dlp") is None:
            print("SKIP: yt-dlp not found. Install with: brew install yt-dlp", file=sys.stderr)
            run_yt = False
        if shutil.which("ffmpeg") is None:
            print("SKIP: ffmpeg not found. Install with: brew install ffmpeg", file=sys.stderr)
            run_yt = False

    if run_yt:
        print(f"==> YouTube test list ({YT_LIST})")
        with open(YT_LIST, "r", encoding="utf-8") as f:
            for raw_line in f:
                line = raw_line.strip()
                if not line or line.startswith("#"):
                    continue
                match = LINE_RE.match(line)
                if not match:
                    print(
                        "SKIP: Unable to parse line (expected: "
                        "'- <URL> (<track name>) [<actual bpm> BPM<(optional time signature)]'): "
                        f"{line}",
                        file=sys.stderr,
                    )
                    continue

                url = normalize_url(match.group("url"))
                label = match.group("label")
                expected = float(match.group("bpm"))

                ok, msg = run_detect(BPM_DETECT, url, expected, label, args.tolerance_pct)
                if ok:
                    pass_count += 1
                else:
                    print(msg, file=sys.stderr)
                    fail_count += 1
    else:
        print("SKIP: YouTube tests disabled. Run on a machine with network access.")

    print(f"==> Summary: {pass_count} passed, {fail_count} failed")
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
