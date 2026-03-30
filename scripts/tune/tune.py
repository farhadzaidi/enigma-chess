#!/usr/bin/env python3
"""
Generic Optuna-based parameter tuner for Enigma chess engine.

Uses Bayesian optimization (TPE) to find better UCI parameter values by playing
matches against the default configuration. Supports tuning both search and time
management parameters via a subcommand.
"""

import argparse
import sys
import warnings
from pathlib import Path
from dataclasses import dataclass

import optuna
from optuna.exceptions import ExperimentalWarning

from lib.path import PROJECT_ROOT
from lib.versioned import save_pickle, load_pickle, next_path, resolve
from tune.match import MatchConfig, play_match

PATIENCE = 50
GAMES_PER_TRIAL = 400


@dataclass
class Parameter:
    name: str
    min_val: float
    max_val: float
    is_int: bool


@dataclass
class ParameterSet:
    """A named group of tunable parameters with its storage location."""
    name: str
    params: list['Parameter']
    data_dir: Path
    prefix: str
    study_name: str
    groups: dict[str, list[str]] = None


SEARCH_PARAMS = ParameterSet(
    name="search",
    params=[
        Parameter("AspirationWindow", 1, 200, True),
        Parameter("ScoreDropThreshold", 1, 500, True),
        Parameter("NullMoveBaseReduction", 1, 4, True),
        Parameter("NullMoveDeeperThreshold", 2, 12, True),
        Parameter("NullMoveMinDepth", 1, 6, True),
        Parameter("ReverseFutilityMarginPerDepth", 20, 200, True),
        Parameter("ReverseFutilityMarginBase", 0, 100, True),
        Parameter("ReverseFutilityMaxDepth", 3, 10, True),
        Parameter("FutilityMarginPerDepth", 10, 300, True),
        Parameter("FutilityMarginBase", 0, 200, True),
        Parameter("FutilityMaxDepth", 1, 8, True),
        Parameter("LMPBase", 1, 10, True),
        Parameter("LMPMaxDepth", 1, 8, True),
        Parameter("RazoringMargin", 50, 600, True),
        Parameter("RazoringMaxDepth", 1, 4, True),
        Parameter("SEEPruningMaxDepth", 1, 8, True),
        Parameter("ReducedSearchMinDepth", 3, 10, True),
        Parameter("ReducedSearchDepthDivisor", 1, 4, True),
        Parameter("ReducedSearchMarginMultiplier", 1, 6, True),
        Parameter("ReducedSearchTTDepthMargin", 1, 6, True),
        Parameter("SEECutoff", -500, 0, True),
        Parameter("LMRTuningConstant", 0.5, 5.0, False),
        Parameter("MinimumIIDDepth", 1, 8, True),
        Parameter("IIDDepthDivisor", 2, 4, True),
        Parameter("LMRPVReduction", 0, 3, True),
        Parameter("BestMoveMinStability", 0, 5, True),
        Parameter("NullMoveDeepReduction", 1, 8, True),
        Parameter("HistoryMalusDivisor", 1, 8, True),
    ],
    groups={
        "null_move": [
            "NullMoveBaseReduction",
            "NullMoveDeeperThreshold",
            "NullMoveMinDepth",
            "NullMoveDeepReduction",
        ],
        "futility": [
            "ReverseFutilityMarginPerDepth",
            "ReverseFutilityMarginBase",
            "ReverseFutilityMaxDepth",
            "FutilityMarginPerDepth",
            "FutilityMarginBase",
            "FutilityMaxDepth",
        ],
        "pruning": [
            "LMPBase",
            "LMPMaxDepth",
            "RazoringMargin",
            "RazoringMaxDepth",
            "SEEPruningMaxDepth",
            "SEECutoff",
        ],
        "reductions": [
            "LMRTuningConstant",
            "LMRPVReduction",
            "ReducedSearchMinDepth",
            "ReducedSearchDepthDivisor",
            "ReducedSearchMarginMultiplier",
            "ReducedSearchTTDepthMargin",
        ],
        "misc": [
            "AspirationWindow",
            "ScoreDropThreshold",
            "BestMoveMinStability",
            "HistoryMalusDivisor",
            "MinimumIIDDepth",
            "IIDDepthDivisor",
        ],
    },
    data_dir=PROJECT_ROOT / 'scripts' / 'tune' / 'data' / 'search_params',
    prefix='search_params',
    study_name='enigma-search-tune',
)

TM_PARAMS = ParameterSet(
    name="tm",
    params=[
        Parameter("MovesLeftBase", 5, 30, True),
        Parameter("MovesLeftPhaseScale", 10, 120, True),
        Parameter("MinMovesNoIncrement", 15, 60, True),
        Parameter("IncrementFraction", 0.1, 1.0, False),
        Parameter("SoftFactorNoIncrement", 0.2, 1.5, False),
        Parameter("SoftFactorIncrement", 0.2, 1.5, False),
        Parameter("HardFactor", 1.0, 8.0, False),
        Parameter("HardCapDivisor", 1, 6, True),
        Parameter("EmergencyTrigger", 2, 16, True),
        Parameter("EmergencySoftDivisor", 4, 30, True),
        Parameter("EmergencyHardDivisor", 4, 40, True),
    ],
    groups={
        "alloc": [
            "MovesLeftBase",
            "MovesLeftPhaseScale",
            "MinMovesNoIncrement",
            "IncrementFraction",
            "SoftFactorNoIncrement",
            "SoftFactorIncrement",
        ],
        "limits": [
            "HardFactor",
            "HardCapDivisor",
            "EmergencyTrigger",
            "EmergencySoftDivisor",
            "EmergencyHardDivisor",
        ],
    },
    data_dir=PROJECT_ROOT / 'scripts' / 'tune' / 'data' / 'tm_params',
    prefix='tm_params',
    study_name='enigma-tm-tune',
)

PARAM_SETS = {ps.name: ps for ps in [SEARCH_PARAMS, TM_PARAMS]}


def _render(params, patience, baseline, best_params, trial_num, best_trial_num=None, score="", first=False):
    """Redraw the in-place terminal display with current best params."""
    num_lines = len(params) + 1

    if not first:
        sys.stdout.write(f"\033[{num_lines}F")

    cl = "\033[2K"
    best_str = f" (trial {best_trial_num})" if best_trial_num is not None else ""
    stale = trial_num - best_trial_num if best_trial_num is not None else 0
    sys.stdout.write(f"{cl}trial {trial_num}  |  best score {score}{best_str}  |  patience {patience - stale}/{patience}\n")

    name_w = max(len(p.name) for p in params)
    for p in params:
        val = best_params.get(p.name, baseline[p.name])
        base = baseline[p.name]

        if p.is_int:
            val = round(val)
            base = round(base)
            diff = val - base
            base_str = f"{base:>7d}"
            val_str = f"{val:>7d}"
            diff_str = f"{diff:+d}"
        else:
            diff = val - base
            base_str = f"{base:>7.3f}"
            val_str = f"{val:>7.3f}"
            diff_str = f"{diff:+.3f}"

        sys.stdout.write(f"{cl}  {p.name:<{name_w}}  {base_str} --> {val_str}  ({diff_str:>7s})\n")

    sys.stdout.write(f"{cl}")
    sys.stdout.flush()


def _save_params(param_set, best_params, baseline_params, path):
    """Save params to pickle — only writes baseline + tuned overrides."""
    params = dict(baseline_params)
    for p in param_set.params:
        if p.name in best_params:
            val = best_params[p.name]
            params[p.name] = round(val) if p.is_int else val
    save_pickle(param_set.data_dir, param_set.prefix, params, path=path)


def _load_baseline(param_set, baseline_arg):
    """Load baseline parameters from pickle."""
    baseline_path = resolve(param_set.data_dir, param_set.prefix, '.pkl', n=baseline_arg)
    if not baseline_path:
        print(f'No params pickle found in {param_set.data_dir}. Run the generator first.')
        sys.exit(1)
    baseline_params = load_pickle(baseline_path)

    missing = [p.name for p in param_set.params if p.name not in baseline_params]
    if missing:
        print(f'Baseline pickle is missing params: {", ".join(missing)}')
        print('Regenerate the baseline pickle to include all current params.')
        sys.exit(1)

    print(f'Baseline from {baseline_path}\n')
    return baseline_params


def _tune_group(param_set, baseline_params, only, match_config, save_path):
    """Run TPE optimization for a subset of params.

    Returns (merged_params, interrupted) where merged_params is the full
    parameter dict with the best values folded in.
    """
    display_params = [p for p in param_set.params if not only or p.name in only]

    def objective(trial):
        params = {}
        for p in param_set.params:
            if only and p.name not in only:
                params[p.name] = baseline_params[p.name]
            elif p.is_int:
                params[p.name] = trial.suggest_int(p.name, int(p.min_val), int(p.max_val))
            else:
                params[p.name] = trial.suggest_float(p.name, p.min_val, p.max_val)
        return play_match(params, baseline_params, match_config)

    prev_best = None

    def after_trial(study, trial):
        nonlocal prev_best
        best = study.best_trial
        trial_num = trial.number + 1
        best_num = best.number + 1
        _render(
            display_params, PATIENCE, baseline_params, best.params,
            trial_num, best_trial_num=best_num, score=f"{best.value:.3f}",
        )

        if best_num != prev_best:
            prev_best = best_num
            _save_params(param_set, best.params, baseline_params, save_path)

        if trial_num - best_num >= PATIENCE:
            study.stop()

    sampler = optuna.samplers.TPESampler(multivariate=True, group=True)
    study = optuna.create_study(
        direction="maximize", sampler=sampler,
        study_name=param_set.study_name,
    )

    enqueue = {
        p.name: baseline_params[p.name]
        for p in param_set.params
        if not only or p.name in only
    }
    study.enqueue_trial(enqueue)

    _render(display_params, PATIENCE, baseline_params, baseline_params, 0, score="—", first=True)

    interrupted = False
    try:
        study.optimize(objective, callbacks=[after_trial])
    except KeyboardInterrupt:
        interrupted = True

    # Merge best trial params into baseline
    merged = dict(baseline_params)
    if len(study.trials) > 0:
        best = study.best_trial.params
        for p in param_set.params:
            if p.name in best:
                val = best[p.name]
                merged[p.name] = round(val) if p.is_int else val

    return merged, interrupted


def _run(param_set, args):
    """Run the Optuna tuner for a given parameter set."""
    warnings.filterwarnings("ignore", category=ExperimentalWarning)
    optuna.logging.set_verbosity(optuna.logging.ERROR)

    match_config = MatchConfig(args.games)
    save_path = next_path(param_set.data_dir, param_set.prefix, '.pkl')
    baseline_params = _load_baseline(param_set, args.baseline)

    only = set(args.only) if args.only else None
    if only:
        unknown = only - {p.name for p in param_set.params}
        if unknown:
            print(f'Unknown params: {", ".join(sorted(unknown))}')
            sys.exit(1)

    _tune_group(param_set, baseline_params, only, match_config, save_path)


def main():
    parser = argparse.ArgumentParser(description="Tune Enigma engine parameters")
    parser.add_argument("target", choices=list(PARAM_SETS.keys()), help="which parameter set to tune")
    parser.add_argument("--games", type=int, default=GAMES_PER_TRIAL, help="games per trial")
    parser.add_argument("--only", nargs="+", default=None, help="only tune these parameters (freeze the rest at baseline)")
    parser.add_argument("--group", default=None, help="tune a named parameter group (use --list-groups to see available)")
    parser.add_argument("--list-groups", action="store_true", help="list available groups for the target and exit")

    parser.add_argument("--baseline", default=None, help='baseline pickle number to load (default: latest, "none" for fresh)')
    args = parser.parse_args()

    param_set = PARAM_SETS[args.target]

    if args.list_groups:
        if not param_set.groups:
            print(f"No groups defined for '{param_set.name}'")
        else:
            for name, members in param_set.groups.items():
                print(f"  {name} ({len(members)}): {', '.join(members)}")
        sys.exit(0)

    if args.group:
        if args.only:
            print("Cannot use --group and --only together")
            sys.exit(1)
        if not param_set.groups or args.group not in param_set.groups:
            available = ', '.join(param_set.groups) if param_set.groups else 'none'
            print(f"Unknown group '{args.group}'. Available: {available}")
            sys.exit(1)
        args.only = param_set.groups[args.group]

    _run(param_set, args)


if __name__ == "__main__":
    main()
