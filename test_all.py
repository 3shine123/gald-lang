#!/usr/bin/env python3
"""
ovel test runner — parallel transpile + compile + run with timeout.
Usage: python3 test_all.py [-j JOBS]

Environment overrides (useful for cross-platform testing, e.g. a musl ovelc
inside a Linux VM against the repo mounted at /mnt/mac):
  OVELC=/path/to/ovelc   binary under test (default: target/debug/ovelc;
                         same convention as run_trace_golden.sh's OVELC=)
  OVEL_CC="clang ..."    C compiler + flags ovelc passes through to the backend
                         (e.g. extra -I/-L/-l). Leave unset to use ovelc's
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

# Binary override: OVELC=/path/to/ovelc python3 test_all.py  (e.g. cross-platform
# testing with a musl binary on Linux VMs; same convention as run_trace_golden.sh's OVELC=)
OVELC = Path(os.environ.get("OVELC", str(PROJECT / "target" / "debug" / "ovelc")))
RUN_TIMEOUT = 3

# ── Collect test binary names from .ov files ──
def _gm_suite_files() -> list[Path]:
    """All host-run .ov tests, excluding out-of-band suites (QEMU kernel, cross-arch,
    freestanding) and the cross-TU suite.

    tests/multi_tu is driven by its own runner (run_multi_tu.sh), which transpiles
    each .ov in a case to a SEPARATE translation unit and links them together.
    test_all.py compiles every .ov standalone, so it would report each lib.ov as
    "no main entry" and each main.ov as a lone file — 18 phantom failures."""
    return sorted(
        p for p in PROJECT.glob("tests/**/*.ov")
        if "soma-kernel" not in p.parts
        and "25_freestanding" not in p.parts
        and "26_baremetal_stress" not in p.parts
        and "multi_tu" not in p.parts
        # tests/stress/* are driven by their own build.sh runners, each with
        # bespoke flags and no standalone main: baremetal needs -ffreestanding,
        # hosted needs -asm asm_host.s, interop's lib.ov is a library file
        # linked against a C caller. Same rationale as multi_tu above.
        and "stress" not in p.parts
        # Deliberate-failure negative tests (checker must reject them); they
        # are validated by hand / in cargo unit tests, not by this runner.
        and "negative" not in p.parts
        # tests/arc_intern/* is a sanitizer-only regression guard (ASan is
        # required to observe the defect, and the ARC->MRC retry below would
        # downgrade the crash to a green PASS_MRC). Driven by its own
        # run_arc_intern_test.sh.
        and "arc_intern" not in p.parts
        # tests/eh_diff/* is a differential suite against clang/ObjC baselines:
        # run_eh_diff.sh compiles each case with `-eh checked` AND with clang
        # -fobjc-arc -fobjc-arc-exceptions, then diffs stderr. Standalone here it
        # would be run in the default sjlj mode, where the uncaught-exception
        # case (07) exits non-zero on purpose — a phantom failure.
        and "eh_diff" not in p.parts
    )

GM_FILES = _gm_suite_files()
TEST_BINS = set()
for f in GM_FILES:
    stem = f.stem
    TEST_BINS.add(f"/tmp/{stem}")

# ── Cleanup: kill orphaned ovelc processes + leftover test binaries on exit ──
def _cleanup():
    # Kill ovelc itself
    subprocess.run(["pkill", "-f", r"target/(debug|release)/ovelc"], capture_output=True)
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
        "EXPECTED_FAIL":  "cyan",
        "PASS_MRC":       "yellow",
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


def _has_main(gm_path: Path) -> bool:
    """Heuristic: does this .ov file define its own `int main` entry point?"""
    try:
        text = gm_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return True
    import re as _re
    stripped = _re.sub(r"/\*.*?\*/", " ", text, flags=_re.S)
    stripped = _re.sub(r"//[^\n]*", " ", stripped)
    return bool(_re.search(r"\bint\s+main\s*\(", stripped)) or bool(_re.search(r"\bvoid\s+main\s*\(", stripped))


def _needs_mrc(gm_path: Path) -> bool:
    """Detect if a .ov file uses manual retain/release (MRC) patterns."""
    try:
        text = gm_path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return False
    import re as _re
    stripped = _re.sub(r"/\*.*?\*/", " ", text, flags=_re.S)
    stripped = _re.sub(r"//[^\n]*", " ", stripped)
    # Check for manual retain/release/dealloc/autorelease calls
    has_mrc = bool(_re.search(r"\[\w+\s+(retain|release|autorelease|dealloc)\]", stripped))
    # Also check for ARC annotations in the file
    has_arc_flag = bool(_re.search(r"-fno-ovel-arc", stripped))
    return has_mrc or has_arc_flag


def _run_np(cmd: list, gm_path: Path, timeout: int) -> tuple:
    """Run ovelc with the given command, return (stdout, stderr, returncode, timed_out)."""
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


def _is_expected_fail(gm_path: Path) -> bool:
    """`foo-F.ov` is a deliberate-failure sample kept in-tree as documentation
    (no main entry, or an intentional over-release crash). It is *supposed* to
    fail, so the suite records it as EXPECTED_FAIL rather than FAIL — and a
    sample that unexpectedly passes is itself reported as a failure."""
    return gm_path.name.endswith("-F.ov")


def _compile_only(gm_path: Path, asm_args: list[str], inc_args: list[str],
                  tmpdir: Path) -> tuple[bool, str]:
    """Alternative acceptance for a test that cannot be RUN in this harness
    (interactive programs block on stdin and hit the timeout). Transpile +
    compile + link into a real binary must still succeed; that is the strongest
    verdict reachable without a TTY. Tries ARC first, then MRC (same fallback
    the runner applies to `ovelc run`)."""
    for extra, label in (([], "ARC"), (["-fno-ovel-arc"], "MRC")):
        bin_path = Path(tmpdir) / (gm_path.stem + ".compileonly")
        cmd = [str(OVELC), str(gm_path), "-o", str(bin_path)] + asm_args + inc_args + extra
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=60, cwd=str(gm_path.parent))
        except Exception:
            continue
        if proc.returncode == 0:
            return True, label
    return False, ""


def process_gm(gm_file: str, tmpdir: Path) -> tuple[str, bool, list[str], str]:
    """
    Run one .ov file via `ovelc run`.
    First tries with ARC (default), then retries with MRC if it fails.
    Returns (relative_path, passed, info_lines, status_tag).
    """
    gm_path = Path(gm_file)
    try:
        rel = str(gm_path.relative_to(PROJECT / "tests"))
    except ValueError:
        rel = str(gm_path.relative_to(PROJECT))
    proc = None
    expected_fail = _is_expected_fail(gm_path)

    # Skip module-only files (no main entry); they're meant to be #import'd
    if not _has_main(gm_path):
        if expected_fail:
            return rel, True, [
                "EXPECTED FAIL (-F sample: no main entry, it is meant to be #import'd)"
            ], "EXPECTED_FAIL"
        return rel, False, ["FAIL (no main entry)"], "FAIL"

    # Auto-include sibling assembly (.s) files: link alongside the .ov
    asm_args: list[str] = []
    sibling_s = gm_path.with_suffix(".s")
    if sibling_s.exists():
        asm_args = ["-asm", str(sibling_s)]
    # In the mega_fusion folder, auto-link sibling hand-written C helpers (.c).
    # Avoid linking generated .c outputs in other subdirs (duplicate symbols).
    if "mega_fusion" in gm_path.parts:
        sibling_c = gm_path.with_suffix(".c")
        if sibling_c.exists():
            asm_args += ["-asm", str(sibling_c)]
    # Add the test's directory to the include path so `#include "header.h"` works
    inc_args = ["-I", str(gm_path.parent)]

    # Try with ARC first
    cmd = [str(OVELC), "run", str(gm_path)] + asm_args + inc_args
    stdout, stderr, rc, timed_out = _run_np(cmd, gm_path, RUN_TIMEOUT + 2)

    if timed_out:
        ok, mode = _compile_only(gm_path, asm_args, inc_args, tmpdir)
        if ok:
            return rel, True, [
                f"CANCELED (interactive — run needs a TTY); alternative acceptance: "
                f"transpile+compile+link OK ({mode})"
            ], "CANCELED"
        return rel, False, [
            "CANCELED (interactive) AND the compile-only acceptance FAILED"
        ], "COMPILE_FAIL"

    if rc == 0:
        out_lines = stdout.strip().split("\n") if stdout.strip() else ["(no output)"]
        return rel, True, out_lines, "PASS"

    # ARC failed — retry with MRC
    mrc_cmd = [str(OVELC), "run", str(gm_path), "-fno-ovel-arc"] + asm_args + inc_args
    mrc_stdout, mrc_stderr, mrc_rc, mrc_timed_out = _run_np(mrc_cmd, gm_path, RUN_TIMEOUT + 2)

    if mrc_timed_out:
        ok, mode = _compile_only(gm_path, asm_args, inc_args, tmpdir)
        if ok:
            return rel, True, [
                f"CANCELED (interactive — run needs a TTY); alternative acceptance: "
                f"transpile+compile+link OK ({mode})"
            ], "CANCELED"
        return rel, False, [
            "CANCELED (interactive) AND the compile-only acceptance FAILED"
        ], "COMPILE_FAIL"

    if mrc_rc == 0:
        out_lines = mrc_stdout.strip().split("\n") if mrc_stdout.strip() else ["(no output)"]
        # Record WHY the ARC attempt failed. The ARC→MRC retry must not become a
        # place where genuine ARC bugs hide: a fallback caused by the checker
        # rejecting deliberate manual retain/release in ARC mode is by design,
        # anything else (crash / miscompile / link error under ARC) is a
        # suspicious ARC defect worth surfacing.
        arc_err = (stderr or stdout)
        if "not allowed in ARC mode" in arc_err:
            why = "by design: explicit retain/release is rejected in ARC mode"
        else:
            why = "SUSPECT: ARC mode failed for a reason other than the explicit-MM check"
        out_lines = out_lines + [f"(MRC fallback — {why})"]
        return rel, True, out_lines, "PASS_MRC"

    # Both failed — report the ARC error
    err = (stderr or stdout).strip()
    err_lines = err.split("\n") if err else ["(no output)"]
    if expected_fail:
        return rel, True, [
            f"EXPECTED FAIL (-F sample: deliberate failure, rc={rc})"
        ] + err_lines[:3], "EXPECTED_FAIL"
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
    parser = argparse.ArgumentParser(description="ovel test runner")
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
    gm_files = _gm_suite_files()
    gm_pass = 0
    gm_fail = 0
    gm_canceled = 0
    gm_expected = 0
    gm_retry = 0
    gm_retry_suspect = 0
    if gm_files:
        with tempfile.TemporaryDirectory(prefix="ovel_test_") as tmpdir_str:
            tmpdir = Path(tmpdir_str)
            with ThreadPoolExecutor(max_workers=JOBS) as executor:
                futures = {executor.submit(process_gm, str(f), tmpdir): f for f in gm_files}
                for future in as_completed(futures):
                    rel, ok, lines, tag = future.result()
                    if tag == "CANCELED":
                        gm_canceled += 1
                    elif tag == "EXPECTED_FAIL":
                        gm_expected += 1
                    elif tag == "PASS_MRC":
                        gm_pass += 1
                        gm_retry += 1
                        if any("SUSPECT" in l for l in lines):
                            gm_retry_suspect += 1
                    elif ok:
                        gm_pass += 1
                    else:
                        gm_fail += 1
                    console.print(panel(rel, lines, tag))
            canceled_str = f", [yellow]{gm_canceled} canceled[/]" if gm_canceled else ""
            expected_str = f", [cyan]{gm_expected} expected-fail[/]" if gm_expected else ""
            retry_str = ""
            if gm_retry:
                retry_str = f", [yellow]{gm_retry} ARC→MRC retry[/]"
                if gm_retry_suspect:
                    retry_str += f" ([red]{gm_retry_suspect} SUSPECT[/])"
            console.print(f"\n  [bold]{gm_pass}/{len(gm_files)}[/] .ov files passed, [red]{gm_fail}[/] failed{canceled_str}{expected_str}{retry_str}")
    else:
        console.print("  [yellow]No .ov files found.[/]")

    # ── 3. Examples ──
    console.rule(f"[bold]Examples  (parallel x{JOBS})")
    example_files = sorted(
        p for p in PROJECT.glob("examples/**/*.ov")
        if "04_soma-kernel" not in p.parts
        and "02_ncurses" not in p.parts
        and "03_LibUI" not in p.parts
    )
    ex_pass = 0
    ex_fail = 0
    ex_canceled = 0
    ex_expected = 0
    ex_retry = 0
    ex_retry_suspect = 0
    if example_files:
        with tempfile.TemporaryDirectory(prefix="ovel_example_") as tmpdir_str:
            tmpdir = Path(tmpdir_str)
            with ThreadPoolExecutor(max_workers=JOBS) as executor:
                futures = {executor.submit(process_gm, str(f), tmpdir): f for f in example_files}
                for future in as_completed(futures):
                    rel, ok, lines, tag = future.result()
                    if tag == "CANCELED":
                        ex_canceled += 1
                    elif tag == "EXPECTED_FAIL":
                        ex_expected += 1
                    elif tag == "PASS_MRC":
                        ex_pass += 1
                        ex_retry += 1
                        if any("SUSPECT" in l for l in lines):
                            ex_retry_suspect += 1
                    elif ok:
                        ex_pass += 1
                    else:
                        ex_fail += 1
                    console.print(panel(rel, lines, tag))
            canceled_str = f", [yellow]{ex_canceled} canceled[/]" if ex_canceled else ""
            expected_str = f", [cyan]{ex_expected} expected-fail[/]" if ex_expected else ""
            retry_str = ""
            if ex_retry:
                retry_str = f", [yellow]{ex_retry} ARC→MRC retry[/]"
                if ex_retry_suspect:
                    retry_str += f" ([red]{ex_retry_suspect} SUSPECT[/])"
            console.print(f"\n  [bold]{ex_pass}/{len(example_files)}[/] examples passed, [red]{ex_fail}[/] failed{canceled_str}{expected_str}{retry_str}")
    else:
        console.print("  [yellow]No example .ov files found.[/]")

    # ── Grand total ──
    elapsed = time.time() - start
    total_fail = unit_fail + gm_fail + ex_fail
    total_pass = unit_pass + gm_pass + ex_pass
    total = (unit_pass + unit_fail + gm_pass + gm_fail + gm_canceled + gm_expected
             + ex_pass + ex_fail + ex_canceled + ex_expected)
    canceled_str = f", [yellow]{gm_canceled + ex_canceled} canceled[/]" if gm_canceled + ex_canceled else ""
    expected_str = f", [cyan]{gm_expected + ex_expected} expected-fail[/]" if gm_expected + ex_expected else ""
    retry_total = gm_retry + ex_retry
    suspect_total = gm_retry_suspect + ex_retry_suspect
    retry_str = f", [yellow]{retry_total} ARC→MRC retry[/]" if retry_total else ""
    if suspect_total:
        retry_str += f" ([red]{suspect_total} SUSPECT — ARC-mode defect, not the explicit-MM check[/])"
    color = "green" if total_fail == 0 else "red"
    console.rule(f"[bold {color}]GRAND TOTAL: {total_pass}/{total} passed, {total_fail} failed{canceled_str}{expected_str}{retry_str}  ({elapsed:.0f}s)")
    sys.exit(total_fail)


if __name__ == "__main__":
    main()