#!/usr/bin/env python3
"""Compatibility entry point; the production SD-driver host and tests live in SDK."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[3] / 'sdk/tests/devices/run.py'), run_name='__main__')
