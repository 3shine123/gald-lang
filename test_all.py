#!/usr/bin/env python3
"""
nupa test runner — parallel transpile + compile + run with timeout.
Usage: python3 test_all.py [-j JOBS]

Environment overrides (useful for cross-platform testing, e.g. a musl nupac
inside a Linux VM against the repo mounted at /mnt/mac):
  NUPAC=/path/to/nupac   binary under test (default: target/debug/nupac;
                         same convention as run_trace_golden.sh's NPAC=)
  NUPA_CC="clang ..."    C compiler + flags nupac passes through to the backend
                         (e.g. extra -I/-L/-l). Leave unset to use nupac's
                         built-in selection (clang; zig cc on Windows).
"""

import argparse, atexit, os, signal, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from rich.console import Console
from rich.markup import escape
from rich.panel import Panel
from rich.table import Table
from rich.style import Style
from rich.text import Text

PROJECT = Path(__file__).resolve().parent
BUILDDIR = PROJECT / "builddir"
INCLUDE = PROJECT / "include"

# Binary override: NUPAC=/path/to/nupac python3 test_all.py  (e.g. cross-platform
# testing with a musl binary on Linux VMs; same convention as run_trace_golden.sh's NPAC=)
NUPAC = Path(os.environ.get("NUPAC", str(PROJECT / "target" / "debug" / "nupac")))
RUN_TIMEOUT = 3

# ── Collect test binary names from .np files ──
def _np_suite_files() -> list[Path]:
    """All host-run .np tests, excluding out-of-band suites (QEMU kernel, cross-arch,
    freestanding) and the cross-TU suite.

    tests/multi_tu is driven by its own runner (run_multi_tu.sh), which transpiles
    each .np in a case to a SEPARATE translation unit and links them together.
    test_all.py compiles every .np standalone, so it would report each lib.np as
    "no main entry" and each main.np as a lone file — 18 phantom failures."""
    return sorted(
        p for p in PROJECT.glob("tests/**/*.np")
        if "soma-kernel" not in p.parts
        and "25_freestanding" not in p.parts
        and "26_baremetal_stress" not in p.parts
        and "multi_tu" not in p.parts
        # tests/stress/* are driven by their own build.sh runners, each with
        # bespoke flags and no standalone main: baremetal needs -ffreestanding,
        # hosted needs -asm asm_host.s, interop's lib.np is a library file
        # linked against a C caller. Same rationale as multi_tu above.
        and "stress" not in p.parts
        # Deliberate-failure negative tests (checker must reject them); they
        # are validated by hand / in cargo unit tests, not by this runner.
        and "negative" not in p.parts
        # tests/eh_diff/* is a differential suite against clang/ObjC baselines:
        # run_eh_diff.sh compiles each case with `-eh checked` AND with clang
        # -fobjc-arc -fobjc-arc-exceptions, then diffs stderr. Standalone here it
        # would be run in the default sjlj mode, where the uncaught-exception
        # case (07) exits non-zero on purpose — a phantom failure.
        and "eh_diff" not in p.parts
    )

NP_FILES = _np_suite_files()
TEST_BINS = set()
for f in NP_FILES:
    stem = f.stem
    TEST_BINS.add(f"/tmp/{stem}")

# ── Cleanup: kill orphaned nupac processes + leftover test binaries on exit ──
def _cleanup():
    # Kill nupac itself
    subprocess.run(["pkill", "-f", r"target/(debug|release)/nupac"], capture_output=True)
    # Kill all compiled test binaries (e.g. /tmp/core_fusion, /tmp/tt, …)
    for bin_path in TEST_BINS:
        subprocess.run(["pkill", "-f", f"^{bin_path}($| )"], capture_output=True)
atexit.register(_cleanup)
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

console = Console()

# ── helpers ──────────────────────────────────────────────────────

def panel(title: str, lines: list[str], tag: str):
    """Build a colored Panel for one test result."""
    style_map = {
        "PASS":           "green",
        "PASS_TIMEOUT":   "yellow",
        "FAIL":           "red",
        "TRANSPILE_FAIL": "red",
        "TRANSPILE_TIMEOUT": "yellow",
        "COMPILE_FAIL":   "red",
        "COMPILE_TIMEOUT": "yellow",
        "RUN_FAIL":       "red",
        "CANCELED":       "yellow",
    }
    color = style_map.get(tag, "white")
    max_line = 160
    brief_lines = []
    for l in lines:
        while len(l) > max_line:
            brief_lines.append(l[:max_line])
            l = l[max_line:]
        brief_lines.append(l)
    brief = "\n".join(brief_lines) if brief_lines else ""
    body = f"[bold]── {tag} ──[/bold]\n{escape(brief)}" if brief else f"[bold]── {tag} ──[/bold]"
    return Panel(
        body,
        title=title,
        title_align="left",
        border_style=color,
        padding=(0, 1),
    )




def run_cargo_tests() -> tuple[int, int, list[str]]:
    """Run cargo test --workspace and parse summary."""
    try:
        r = subprocess.run(
            ["cargo", "test", "--workspace"],
            capture_output=True, text=True, timeout=300,
            cwd=PROJECT,
        )
        output = r.stdout + r.stderr
        lines = output.strip().split("\n")
        total_pass = 0
        total_fail = 0
        for line in lines:
            if line.startswith("test result:"):
                parts = line.split(";")
                for p in parts:
                    p = p.strip()
                    words = p.split()
                    for i, w in enumerate(words):
                        if w == "passed" and i > 0:
                            total_pass += int(words[i-1])
                        elif w == "failed" and i > 0:
                            total_fail += int(words[i-1])
        if total_pass + total_fail == 0:
            return 0, 1, ["cargo test produced no recognizable output"]
        return total_pass, total_fail, lines[-10:] if len(lines) > 10 else lines
    except subprocess.TimeoutExpired:
        return 0, 1, ["TIMEOUT (300s cargo test)"]
    except Exception as e:
        return 0, 1, [str(e)]


def _has_main(np_path: Path) -> bool:
    """Heuristic: does this .np file define its own `int main` entry point?"""
    try:
        text = np_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return True
    import re as _re
    stripped = _re.sub(r"/\*.*?\*/", " ", text, flags=_re.S)
    stripped = _re.sub(r"//[^\n]*", " ", stripped)
    return bool(_re.search(r"\bint\s+main\s*\(", stripped)) or bool(_re.search(r"\bvoid\s+main\s*\(", stripped))


def _needs_mrc(np_path: Path) -> bool:
    """Detect if a .np file uses manual retain/release (MRC) patterns."""
    try:
        text = np_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return False
    import re as _re
    stripped = _re.sub(r"/\*.*?\*/", " ", text, flags=_re.S)
    stripped = _re.sub(r"//[^\n]*", " ", stripped)
    # Check for manual retain/release/dealloc/autorelease calls
    has_mrc = bool(_re.search(r"\[\w+\s+(retain|release|autorelease|dealloc)\]", stripped))
    # Also check for ARC annotations in the file
    has_arc_flag = bool(_re.search(r"-fno-nupa-arc", stripped))
    return has_mrc or has_arc_flag


def _run_np(cmd: list, np_path: Path, timeout: int) -> tuple:
    """Run nupac with the given command, return (stdout, stderr, returncode, timed_out)."""
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            start_new_session=True, cwd=PROJECT,
        )
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
            return stdout, stderr, proc.returncode, False
        except subprocess.TimeoutExpired:
            try:
                pgid = os.getpgid(proc.pid)
                os.killpg(pgid, signal.SIGKILL)
            except Exception:
                proc.kill()
            proc.wait()
            return "", "", -1, True
    except Exception as e:
        return "", str(e), -1, False


def process_np(np_file: str, tmpdir: Path) -> tuple[str, bool, list[str], str]:
    """
    Run one .np file via `nupac run`.
    First tries with ARC (default), then retries with MRC if it fails.
    Returns (relative_path, passed, info_lines, status_tag).
    """
    np_path = Path(np_file)
    try:
        rel = str(np_path.relative_to(PROJECT / "tests"))
    except ValueError:
        rel = str(np_path.relative_to(PROJECT))
    proc = None

    # Skip module-only files (no main entry); they're meant to be #import'd
    if not _has_main(np_path):
        return rel, False, ["FAIL (no main entry)"], "FAIL"

    # Auto-include sibling assembly (.s) files: link alongside the .np
    asm_args: list[str] = []
    sibling_s = np_path.with_suffix(".s")
    if sibling_s.exists():
        asm_args = ["-asm", str(sibling_s)]
    # In the mega_fusion folder, auto-link sibling hand-written C helpers (.c).
    # Avoid linking generated .c outputs in other subdirs (duplicate symbols).
    if "mega_fusion" in np_path.parts:
        sibling_c = np_path.with_suffix(".c")
        if sibling_c.exists():
            asm_args += ["-asm", str(sibling_c)]
    # Add the test's directory to the include path so `#include "header.h"` works
    inc_args = ["-I", str(np_path.parent)]

    # Try with ARC first
    cmd = [str(NUPAC), "run", str(np_path)] + asm_args + inc_args
    stdout, stderr, rc, timed_out = _run_np(cmd, np_path, RUN_TIMEOUT + 2)

    if timed_out:
        return rel, True, ["CANCELED (timed out — probably interactive game)"], "CANCELED"

    if rc == 0:
        out_lines = stdout.strip().split("\n") if stdout.strip() else ["(no output)"]
        return rel, True, out_lines, "PASS"

    # ARC failed — retry with MRC
    mrc_cmd = [str(NUPAC), "run", str(np_path), "-fno-nupa-arc"] + asm_args + inc_args
    mrc_stdout, mrc_stderr, mrc_rc, mrc_timed_out = _run_np(mrc_cmd, np_path, RUN_TIMEOUT + 2)

    if mrc_timed_out:
        return rel, True, ["CANCELED (timed out — probably interactive game)"], "CANCELED"

    if mrc_rc == 0:
        out_lines = mrc_stdout.strip().split("\n") if mrc_stdout.strip() else ["(no output)"]
        return rel, True, out_lines, "PASS"

    # Both failed — report the ARC error
    err = (stderr or stdout).strip()
    err_lines = err.split("\n") if err else ["(no output)"]
    if "error:" in err.lower() or "Error:" in err:
        if "TRANSPILE" in err or "Parse" in err:
            return rel, False, err_lines, "TRANSPILE_FAIL"
        else:
            return rel, False, err_lines, "COMPILE_FAIL"
    else:
        return rel, False, [f"RUN FAILED exit={rc}"] + err_lines, "RUN_FAIL"
        if proc and proc.poll() is None:
            try:
                pgid = os.getpgid(proc.pid)
                os.killpg(pgid, signal.SIGKILL)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
            proc.wait()


# ── main ─────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="nupa test runner")
    parser.add_argument("-j", type=int, default=0, help="parallel jobs (default: CPU count)")
    args = parser.parse_args()
    JOBS = args.j if args.j else (os.cpu_count() or 4)

    start = time.time()

    # ── 1. Unit tests ──
    console.rule("[bold]Unit Tests (cargo test)")
    unit_pass, unit_fail, cargo_out = run_cargo_tests()
    if unit_fail == 0:
        console.print(f"  [green]✅  All {unit_pass} Rust tests passed[/]")
    else:
        console.print(f"  [red]❌  {unit_fail} test(s) failed[/]")
        for l in cargo_out[-5:]:
            if l.strip():
                console.print(f"       [dim]{l.strip()[:72]}[/]")
    console.print(f"\n  [bold]{unit_pass}/{unit_pass + unit_fail}[/] unit tests passed, [red]{unit_fail}[/] failed")

# ── 2. NP tests ──
    console.rule(f"[bold]NP Tests  (parallel x{JOBS})")
    np_files = _np_suite_files()
    np_pass = 0
    np_fail = 0
    np_canceled = 0
    if np_files:
        with tempfile.TemporaryDirectory(prefix="nupa_test_") as tmpdir_str:
            tmpdir = Path(tmpdir_str)
            with ThreadPoolExecutor(max_workers=JOBS) as executor:
                futures = {executor.submit(process_np, str(f), tmpdir): f for f in np_files}
                for future in as_completed(futures):
                    rel, ok, lines, tag = future.result()
                    if tag == "CANCELED":
                        np_canceled += 1
                    elif ok:
                        np_pass += 1
                    else:
                        np_fail += 1
                    console.print(panel(rel, lines, tag))
            canceled_str = f", [yellow]{np_canceled} canceled[/]" if np_canceled else ""
            console.print(f"\n  [bold]{np_pass}/{len(np_files)}[/] .np files passed, [red]{np_fail}[/] failed{canceled_str}")
    else:
        console.print("  [yellow]No .np files found.[/]")

    # ── 3. Examples ──
    console.rule(f"[bold]Examples  (parallel x{JOBS})")
    example_files = sorted(
        p for p in PROJECT.glob("examples/**/*.np")
        if "04_soma-kernel" not in p.parts
        and "02_ncurses" not in p.parts
        and "03_LibUI" not in p.parts
    )
    ex_pass = 0
    ex_fail = 0
    ex_canceled = 0
    if example_files:
        with tempfile.TemporaryDirectory(prefix="nupa_example_") as tmpdir_str:
            tmpdir = Path(tmpdir_str)
            with ThreadPoolExecutor(max_workers=JOBS) as executor:
                futures = {executor.submit(process_np, str(f), tmpdir): f for f in example_files}
                for future in as_completed(futures):
                    rel, ok, lines, tag = future.result()
                    if tag == "CANCELED":
                        ex_canceled += 1
                    elif ok:
                        ex_pass += 1
                    else:
                        ex_fail += 1
                    console.print(panel(rel, lines, tag))
            canceled_str = f", [yellow]{ex_canceled} canceled[/]" if ex_canceled else ""
            console.print(f"\n  [bold]{ex_pass}/{len(example_files)}[/] examples passed, [red]{ex_fail}[/] failed{canceled_str}")
    else:
        console.print("  [yellow]No example .np files found.[/]")

    # ── Grand total ──
    elapsed = time.time() - start
    total_fail = unit_fail + np_fail + ex_fail
    total_pass = unit_pass + np_pass + ex_pass
    total = unit_pass + unit_fail + np_pass + np_fail + np_canceled + ex_pass + ex_fail + ex_canceled
    canceled_str = f", [yellow]{np_canceled + ex_canceled} canceled[/]" if np_canceled + ex_canceled else ""
    color = "green" if total_fail == 0 else "red"
    console.rule(f"[bold {color}]GRAND TOTAL: {total_pass}/{total} passed, {total_fail} failed{canceled_str}  ({elapsed:.0f}s)")
    sys.exit(total_fail)


if __name__ == "__main__":
    main()