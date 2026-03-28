#!/usr/bin/env python3
"""
Path constants for NNUE scripts.
"""

from pathlib import Path

NNUE_DATA_DIR = Path('nnue/data')
WEIGHTS_DIR = NNUE_DATA_DIR / 'weights'
WEIGHTS_GLOB = 'weights_*.pt'
TRAINING_DATA_DIR = NNUE_DATA_DIR / 'training'
TRAINING_DATA_GLOB = 'training_*.bin'
TRAINING_DATA_FILENAME = 'training_{}.bin'
VALIDATION_DATA_DIR = NNUE_DATA_DIR / 'validation'
VALIDATION_DATA_GLOB = 'validation_*.bin'
VALIDATION_DATA_FILENAME = 'validation_{}.bin'
