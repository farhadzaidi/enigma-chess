#!/usr/bin/env python3
"""
Shared concurrency helpers used by other scripts.
"""

import psutil

PHYSICAL_CORES = psutil.cpu_count(logical=False) or 1
RESERVED_CORES = 2


def calc_concurrency(threads, ponder=False):
    """Calculate how many concurrent games can run given thread count and ponder mode."""
    engines_per_game = 2 if ponder else 1
    usable_cores = max(1, PHYSICAL_CORES - RESERVED_CORES)
    return max(1, usable_cores // (threads * engines_per_game))
