#!/usr/bin/env python3
"""Preserve Catch2 failures and report unavailable GPU suites as Meson skips."""

import re
import subprocess
import sys


def main():
    result = subprocess.run(sys.argv[1:], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False)
    sys.stdout.write(result.stdout)
    # Catch2 3.14 exits 4 when every case skips; Meson expects 77. Check the
    # summary too: older Catch2 versions can return 4 for failed assertions.
    if result.returncode == 4 and re.search(
            r"^test cases:\s+(\d+)\s+\|\s+\1 skipped\s*$", result.stdout, re.MULTILINE):
        return 77
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
