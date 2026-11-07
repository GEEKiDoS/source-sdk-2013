#!/usr/bin/env python3
"""Run the production Vulkan local visibility kernel against analytic fixtures.

The probe links the four restir_vulkan*.obj objects from a built Release/x64 baker
plus tier1/tier0/vstdlib/mathlib/vulkan-1 libraries. Use the same compiler defines
as hybrid_visibility_test.py, include public/tier0, public/tier1, utils/vrad_restir
and thirdparty/vulkan_sdk/Include, and place the generated vrad_restir_shaders
folder beside the probe executable. No shipping BSPs or engine launches.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--dll-dir', type=Path, action='append', default=[])
    options = parser.parse_args()
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p) for p in [ROOT/'../game/bin/x64', *options.dll_dir]) + os.pathsep + env['PATH']
    results = []
    for backend, args in (('hardware-rt', ()), ('compute-bvh', ('--software',))):
        completed = subprocess.run([str(options.probe.resolve()), *args], env=env, capture_output=True, text=True)
        observed = re.findall(r'local_visibility case=(\d+) value=([^ ]+) expected=([^\s]+)', completed.stdout)
        passed = completed.returncode == 0 and len(observed) == 5 and f'PASS production local visibility backend={backend}' in completed.stdout
        row = dict(backend=backend, scope='production GPU kernel/service; alpha127/128, immutable-only finite segments, R0/open/blocked, R4/half-penumbra, no-self and blocked invalid receiver',
                   passed=passed, observations=observed, exit_code=completed.returncode, stdout=completed.stdout, stderr=completed.stderr)
        results.append(row)
        print(json.dumps(row))
    report = dict(passed=all(r['passed'] for r in results), cases=results)
    if options.report:
        options.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'], backends=len(results), cases=10)))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
