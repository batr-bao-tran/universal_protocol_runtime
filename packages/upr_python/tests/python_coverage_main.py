"""Runs pytest under coverage.py and writes an LCOV report."""

from __future__ import annotations

import argparse
from pathlib import Path

import coverage
import pytest

TEST_FILE = Path(__file__).with_name("python_codec_test.py")
PACKAGE_INCLUDE = "*/universal_protocol_runtime/*.py"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lcov", required=True, help="Output path for the LCOV report.")
    args = parser.parse_args()

    cov = coverage.Coverage(include=[PACKAGE_INCLUDE], omit=["*/tests/*"])
    cov.start()
    exit_code = pytest.main([str(TEST_FILE)])
    cov.stop()
    cov.save()
    cov.lcov_report(outfile=args.lcov)
    return int(exit_code)


if __name__ == "__main__":
    raise SystemExit(main())
