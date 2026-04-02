#!/usr/bin/env python3
"""
Quantizes trained NNUE weights into fixed-point integers for the C++ engine
and writes them to a binary file.

Quantization scheme:

Q1 (127) — Feature transformer scale. Chosen to fill the int8 output range
after ClippedReLU clamps the accumulator to [0, 127]. Weights are int16
because the scaled values exceed int8's [-128, 127] range.

Q2 (64) — Hidden layer weight scale. Power of 2 so dequantization is a
bit shift (>> 6) instead of a division. Weights fit comfortably in int8.

Layer 1 has no matmul — it sums embedding rows into an int16 accumulator,
so weights and biases share the same scale (Q1) and are both int16.

Layers 2/3 have a real matmul where inputs (scaled by Q1) multiply weights
(scaled by Q2), so the accumulator is scaled by Q1*Q2. Biases are int32 at
that combined scale so they can be added directly to the int32 matmul
accumulator. After adding the bias, >> 6 brings it back to Q1 scale.

The output layer uses a custom weight scale qeval = Q2 / (Q1 * eval_scale)
instead of Q2, where eval_scale is the sigmoid constant from training. This
bakes the centipawn conversion into the weights so that >> 6 on the int32
result yields centipawns directly, with no float math in the engine.

Binary layout (little-endian, all arrays row-major):
  L1 weights  — int16[40960][256]
  L1 bias     — int16[256]
  L2 weights  — int8[32][512]
  L2 bias     — int32[32]
  L3 weights  — int8[32][32]
  L3 bias     — int32[32]
  Out weights — int16[32]
  Out bias    — int32 (scalar)
"""

import torch
import numpy as np
from lib.versioned import resolve
from nnue.paths import WEIGHTS_DIR
from lib.path import PROJECT_ROOT
OUTPUT_PATH = PROJECT_ROOT / 'src' / 'data' / 'nnue.bin'

Q1 = 127
Q2 = 64


def _quantize(tensor, scale, dtype):
    """Scale a tensor, round to integers, and check for overflow before casting to dtype."""
    arr = (tensor * scale).round().numpy()
    info = np.iinfo(dtype)
    if arr.min() < info.min or arr.max() > info.max:
        raise OverflowError(f'Quantized values [{arr.min():.0f}, {arr.max():.0f}] exceed {dtype.__name__} range [{info.min}, {info.max}]')
    return arr.astype(dtype)


def _check_accumulator_overflow(weights, biases):
    """Verify int16 accumulators can't overflow. Upper bound assumes all 30 non-king
    pieces activate the largest weight on the same accumulator element."""
    max_accumulator = int(np.max(np.abs(biases))) + 30 * int(np.max(np.abs(weights)))
    if max_accumulator > np.iinfo(np.int16).max:
        raise OverflowError(f'Worst-case accumulator value {max_accumulator} exceeds int16 range')


def _load_and_quantize_parameters():
    """Loads and quantizes learned weights and biases. Returns the learned parameters along with
    the fitted evaluation scaling factor in a dict"""
    params = {}

    checkpoint = torch.load(resolve(WEIGHTS_DIR, 'weights', '.pt'), weights_only=False, map_location='cpu')
    parameters = checkpoint['model']
    eval_scaling_factor = checkpoint['eval_scaling_factor']
    qeval = Q2 / (Q1 * eval_scaling_factor)

    params['qlayer1_weight'] = _quantize(parameters['layer1.weight'], Q1, np.int16)
    params['qlayer1_bias'] = _quantize(parameters['layer1_bias'], Q1, np.int16)
    _check_accumulator_overflow(params['qlayer1_weight'], params['qlayer1_bias'])

    params['qlayer2_weight'] = _quantize(parameters['layer2.weight'], Q2, np.int8)
    params['qlayer2_bias'] = _quantize(parameters['layer2.bias'], Q1 * Q2, np.int32)

    params['qlayer3_weight'] = _quantize(parameters['layer3.weight'], Q2, np.int8)
    params['qlayer3_bias'] = _quantize(parameters['layer3.bias'], Q1 * Q2, np.int32)

    params['qoutput_weight'] = _quantize(parameters['output.weight'].flatten(), qeval, np.int16)
    params['qoutput_bias'] = _quantize(parameters['output.bias'], Q1 * qeval, np.int32)

    return params


def main():
    """Quantize the latest trained weights and write them to src/data/nnue.bin."""
    params = _load_and_quantize_parameters()

    with open(OUTPUT_PATH, 'wb') as file:
        params['qlayer1_weight'].tofile(file)
        params['qlayer1_bias'].tofile(file)
        params['qlayer2_weight'].tofile(file)
        params['qlayer2_bias'].tofile(file)
        params['qlayer3_weight'].tofile(file)
        params['qlayer3_bias'].tofile(file)
        params['qoutput_weight'].tofile(file)
        params['qoutput_bias'].tofile(file)

    print(f'Wrote NNUE weights to {OUTPUT_PATH}')

if __name__ == '__main__':
    main()
