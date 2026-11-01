#!/usr/bin/env python3
"""Run a small, explicit real-emulator baseline; reject skipped or zero-test runs."""

import argparse
import fnmatch
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import tomllib
import xml.etree.ElementTree as ET

DEFAULT_TESTS = [
    "HostBufferTest.*",
    "MeshDeviceFixture.CB_Reservation_ExactCapacity_NoViolation",
    "MeshDeviceFixture.CB_Boundary_Wraparound_NoViolation",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metal", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New directory; existing evidence is never overwritten",
    )
    parser.add_argument(
        "--filter", action="append", dest="filters", help="One gtest name/glob per option"
    )
    parser.add_argument(
        "--cluster-descriptor", type=Path, help="Explicit mock topology; default is one-device N150"
    )
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    metal, build, output = args.metal.resolve(), args.build.resolve(), args.output.resolve()
    binary = build / "test/tt_metal/unit_tests_api"
    if not binary.is_file():
        parser.error(f"build unit_tests_api first: {binary}")
    pin = tomllib.loads(Path(__file__).with_name("upstream.lock.toml").read_text())["tt_metal"][
        "revision"
    ]
    revision = subprocess.check_output(
        ["git", "-C", str(metal), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != pin:
        parser.error(f"tt-metal pin mismatch: {revision}")
    filters = args.filters or DEFAULT_TESTS
    if any(":" in test or "-" in test for test in filters):
        parser.error("use one positive gtest name/glob per --filter")
    descriptor = (
        args.cluster_descriptor
        or metal / "tt_metal/third_party/umd/tests/cluster_descriptor_examples/wormhole_N150.yaml"
    ).resolve()
    if not descriptor.is_file():
        parser.error(f"cluster descriptor missing: {descriptor}")
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env.update(
        {
            "TT_METAL_MOCK_CLUSTER_DESC_PATH": str(descriptor),
            "TT_METAL_EMULE_MODE": "1",
            "TT_METAL_SLOW_DISPATCH_MODE": "1",
            "TT_METAL_RUNTIME_ROOT": str(metal),
            "TT_METAL_HOME": str(metal),
        }
    )
    results = []
    for number, test in enumerate(filters):
        xml = output / f"{number}.xml"
        command = [str(binary), f"--gtest_filter={test}", f"--gtest_output=xml:{xml}"]
        started = time.monotonic()
        with (output / f"{number}.log").open("w") as log:
            process = subprocess.Popen(
                command,
                cwd=metal,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            timed_out = False
            try:
                code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                code = process.wait()
        reason = ""
        count = 0
        try:
            root = ET.parse(xml).getroot()
            cases = root.findall(".//testcase")
            count = len(cases)
            if not cases:
                reason = "zero test cases"
            elif any(
                case.find("failure") is not None or case.find("error") is not None for case in cases
            ):
                reason = "test failure"
            elif any(
                case.find("skipped") is not None
                or case.get("status") == "notrun"
                or case.get("result") == "skipped"
                for case in cases
            ):
                reason = "skipped tests are not baseline evidence"
            elif any(
                not fnmatch.fnmatchcase(f"{case.get('classname')}.{case.get('name')}", test)
                for case in cases
            ):
                reason = "unexpected test identity"
        except (OSError, ET.ParseError) as error:
            reason = f"missing/invalid XML: {error}"
        if code != 0 or timed_out:
            reason = f"exit {code}, timeout={timed_out}; {reason}"
        result = {
            "filter": test,
            "command": command,
            "exit_code": code,
            "tests": count,
            "seconds": time.monotonic() - started,
            "passed": not reason,
            "reason": reason,
        }
        results.append(result)
        print(json.dumps(result), flush=True)
        (output / "summary.json").write_text(
            json.dumps(
                {
                    "metal_revision": revision,
                    "cluster_descriptor": str(descriptor),
                    "results": results,
                },
                indent=2,
            )
        )
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
