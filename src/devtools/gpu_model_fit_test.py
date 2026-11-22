#!/usr/bin/env python3
"""Run the engine-free C++ matcher tests and per-vendor leave-one-out report.

In a VS Developer Command Prompt (or with c++ on PATH):
    python src/devtools/gpu_model_fit_test.py path/to/gpus.csv

The fixture is the user's ranking, not scraped or supplemented. Prints the digest
of the exact input; missing benchmarks are zero only at the runtime API boundary.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
VENDOR = {"Nvidia": "NVIDIA", "AMD": "AMD", "Intel": "INTEL", "Apple": "APPLE"}
DB_PATH = HERE.parents[1] / "game" / "mod_l4d2" / "scripts" / "gpu_database.txt"
SCORE_NAMES = {"rating": "rating", "gaming": "gaming", "render": "render", "tflops": "tflops_fp32",
               "opencl": "geekbench_opencl", "timespy": "timespy"}


def check_database(rows):
    """Check all shipped names, all columns, blank handling and rank order."""
    text = DB_PATH.read_text(encoding="ascii")
    entries = re.findall(r'(?m)^\s*"([^"]+)"\s*\{([^{}]+)\}\s*$', text)
    actual = [(name, dict(re.findall(r'"([^"]+)"\s+"([^"]*)"', fields))) for name, fields in entries]
    expected = [(row["name"], {"vendor": row["vendor"], **{
        key: row[column] for key, column in SCORE_NAMES.items() if row[column]
    }}) for row in rows]
    if actual != expected:
        raise ValueError("Shipped GPU database differs from the ranking table.")
    print(f"Shipped KeyValues matches all {len(rows)} CSV rows.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--cxx", help="Compiler executable (cl, clang++ or g++)")
    args = parser.parse_args()
    cxx = args.cxx or shutil.which("cl") or shutil.which("c++") or shutil.which("clang++")
    if not cxx:
        parser.error("No C++ compiler on PATH; use a VS Developer Command Prompt or --cxx.")
    print(f"{args.csv}: SHA-256 {hashlib.sha256(args.csv.read_bytes()).hexdigest()}", flush=True)
    with args.csv.open(encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    check_database(rows)
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        with (work / "gpu_model_rows.inc").open("w", encoding="ascii") as f:
            for row in rows:
                scores = [float(row[key] or 0) for key in ("timespy", "gaming", "tflops_fp32")]
                fields = [VENDOR[row["vendor"]], json.dumps(row["name"])]
                fields.extend(f"{x!r}f" for x in scores)
                print("{" + ", ".join(fields) + "},", file=f)
        inc = HERE.parent / "game" / "client"
        source = HERE / "gpu_model_test.cpp"
        exe = work / "gpu_model_test.exe"
        if Path(cxx).stem.lower() in ("cl", "clang-cl"):
            cmd = [cxx, "/nologo", "/EHsc", f"/I{inc}", f"/I{work}", str(source), f"/Fe{exe}"]
        else:
            cmd = [cxx, "-std=c++11", "-I", str(inc), "-I", tmp, str(source), "-o", str(exe)]
        subprocess.run(cmd, cwd=work, check=True)
        subprocess.run([str(exe)], cwd=work, check=True)


if __name__ == "__main__":
    main()
