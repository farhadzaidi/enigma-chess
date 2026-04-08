#!/usr/bin/env python3
"""
SPSA-based parameter tuner for Enigma chess engine.

Uses Simultaneous Perturbation Stochastic Approximation to find better UCI
parameter values by playing matches between perturbed configurations. Unlike
the TPE tuner, SPSA perturbs all parameters simultaneously and uses fewer
games per iteration — noise averages out over many gradient updates.
"""

import random
import sys
from dataclasses import dataclass

from lib.path import PROJECT_ROOT
from lib.versioned import save_pickle, load_pickle, next_path, resolve
from tune.match import MatchConfig, play_match

DATA_DIR = PROJECT_ROOT / 'scripts' / 'tune' / 'data'
PREFIX = 'params'

GAMES_PER_TC = 60
A_INIT = 1.0
ALPHA = 0.602
GAMMA = 0.101
A = 10  # stability constant, controls step decay rate
SAVE_INTERVAL = 10
RNG_SEED = 42


C_FRAC = 0.075  # perturbation = 7.5% of range
A_OVER_C2 = 20  # a = 20 * c² — step ∝ range, ~1.4% of range per iteration


@dataclass
class Param:
    name: str
    min_val: float
    max_val: float
    is_int: bool

    @property
    def c(self):
        raw = (self.max_val - self.min_val) * C_FRAC
        return max(1, round(raw)) if self.is_int else raw

    @property
    def a(self):
        return self.c * self.c * A_OVER_C2


PARAMS = [
    # Search
    Param('AspirationWindow', 1, 200, True),
    Param('NullMoveBaseReduction', 1, 4, True),
    Param('NullMoveDeeperThreshold', 2, 12, True),
    Param('NullMoveMinDepth', 1, 6, True),
    Param('ReverseFutilityMarginPerDepth', 20, 200, True),
    Param('ReverseFutilityMarginBase', 0, 100, True),
    Param('ReverseFutilityMaxDepth', 3, 10, True),
    Param('FutilityMarginPerDepth', 10, 300, True),
    Param('FutilityMarginBase', 0, 200, True),
    Param('FutilityMaxDepth', 1, 8, True),
    Param('LMPBase', 1, 10, True),
    Param('LMPMaxDepth', 1, 8, True),
    Param('RazoringMargin', 50, 600, True),
    Param('RazoringMaxDepth', 1, 4, True),
    Param('SEEPruningMaxDepth', 1, 8, True),
    Param('ReducedSearchMinDepth', 3, 10, True),
    Param('ReducedSearchDepthDivisor', 1, 4, True),
    Param('ReducedSearchMarginMultiplier', 1, 6, True),
    Param('ReducedSearchTTDepthMargin', 1, 6, True),
    Param('SEECutoff', -500, 0, True),
    Param('LMRTuningConstant', 0.5, 5.0, False),
    Param('MinimumIIDDepth', 1, 8, True),
    Param('IIDDepthDivisor', 2, 4, True),
    Param('LMRPVReduction', 0, 3, True),
    Param('NullMoveDeepReduction', 1, 8, True),
    Param('HistoryMalusDivisor', 1, 8, True),
]


def _perturb(params, theta, rng, k):
    """Generate theta+c and theta-c with Bernoulli ±1 deltas.

    Returns (theta_plus, theta_minus, realized_half_diff) where
    realized_half_diff accounts for clamping and rounding.
    """
    realized = {}
    theta_plus = {}
    theta_minus = {}

    for p in params:
        d = rng.choice([-1, 1])

        c_k = p.c / (k + 1) ** GAMMA
        if p.is_int:
            c_k = max(1.0, c_k)

        plus_val = theta[p.name] + c_k * d
        minus_val = theta[p.name] - c_k * d

        plus_val = max(p.min_val, min(p.max_val, plus_val))
        minus_val = max(p.min_val, min(p.max_val, minus_val))

        if p.is_int:
            plus_val = round(plus_val)
            minus_val = round(minus_val)

        theta_plus[p.name] = plus_val
        theta_minus[p.name] = minus_val
        realized[p.name] = (plus_val - minus_val) / 2

    return theta_plus, theta_minus, realized


def _render(params, baseline, theta, iteration, max_iter, first=False):
    """Redraw the in-place terminal display with current theta vs baseline."""
    num_lines = len(params) + 1

    if not first:
        sys.stdout.write(f'\033[{num_lines}F')

    cl = '\033[2K'
    iter_str = f'iteration {iteration}' if max_iter is None else f'iteration {iteration}/{max_iter}'
    sys.stdout.write(f'{cl}{iter_str}\n')

    name_w = max(len(p.name) for p in params)
    for p in params:
        val = theta[p.name]
        base = baseline[p.name]

        if p.is_int:
            display_val = round(val)
            base = round(base)
            diff = display_val - base
            base_str = f'{base:>7d}'
            val_str = f'{display_val:>7d}'
            diff_str = f'{diff:+d}'
        else:
            diff = val - base
            base_str = f'{base:>7.3f}'
            val_str = f'{val:>7.3f}'
            diff_str = f'{diff:+.3f}'

        sys.stdout.write(
            f'{cl}  {p.name:<{name_w}}'
            f'  {base_str} --> {val_str}'
            f'  ({diff_str:>7s})\n'
        )

    sys.stdout.write(f'{cl}')
    sys.stdout.flush()


def _load_params():
    """Load latest parameter values."""
    path = resolve(DATA_DIR, PREFIX, '.pkl')
    if not path:
        print(f'No pickle found in {DATA_DIR}.')
        print('Create one by merging the search and TM baselines.')
        sys.exit(1)
    params = load_pickle(path)

    missing = [p.name for p in PARAMS if p.name not in params]
    if missing:
        print(f'Missing params: {", ".join(missing)}')
        sys.exit(1)

    print(f'Loaded from {path}')
    return params, path


def _save_checkpoint(theta, baseline, rng, k, path):
    """Save theta + SPSA state to pickle."""
    data = {}
    for p in PARAMS:
        val = theta[p.name]
        data[p.name] = round(val) if p.is_int else val
    data['_spsa_state'] = {
        'k': k,
        'rng_state': rng.getstate(),
        'baseline': baseline,
    }
    save_pickle(DATA_DIR, PREFIX, data, path=path)


def run_spsa(resume=False):
    """Run SPSA optimization."""
    match_config = MatchConfig(GAMES_PER_TC)
    loaded_params, loaded_path = _load_params()

    # Internal theta is float (continuous relaxation, even for ints)
    theta = {p.name: float(loaded_params[p.name]) for p in PARAMS}

    # Baseline is the starting point for diff display
    baseline = dict(theta)

    rng = random.Random(RNG_SEED)
    start_k = 0

    # Restore SPSA state if resuming
    spsa_state = loaded_params.get('_spsa_state')
    if resume:
        if spsa_state:
            start_k = spsa_state['k'] + 1
            rng.setstate(spsa_state['rng_state'])
            if 'baseline' in spsa_state:
                baseline = spsa_state['baseline']
            print(f'Resuming from iteration {start_k}')
        else:
            print('Warning: --resume given but no SPSA state in pickle, starting fresh')

    save_path = loaded_path if resume else next_path(DATA_DIR, PREFIX, '.pkl')

    _render(PARAMS, baseline, theta, start_k, None, first=True)

    k = start_k
    try:
        while True:
            a_k = A_INIT / (k + 1 + A) ** ALPHA

            theta_plus, theta_minus, realized = _perturb(PARAMS, theta, rng, k)
            win_rate = play_match(theta_plus, theta_minus, match_config)
            signal = win_rate - 0.5

            for p in PARAMS:
                half_diff = realized[p.name]
                if half_diff == 0:
                    continue
                g = signal / half_diff
                theta[p.name] += p.a * a_k * g
                theta[p.name] = max(p.min_val, min(p.max_val, theta[p.name]))

            _render(PARAMS, baseline, theta, k + 1, None)

            if (k + 1) % SAVE_INTERVAL == 0:
                _save_checkpoint(
                    theta, baseline, rng, k, save_path,
                )

            k += 1

    except KeyboardInterrupt:
        pass

    _save_checkpoint(theta, baseline, rng, k - 1, save_path)
    print(f'\nSaved to {save_path}')
