import argparse

from tune.spsa import run_spsa

parser = argparse.ArgumentParser(description='SPSA parameter tuner')
parser.add_argument(
    '--resume',
    action='store_true',
    help='resume from last checkpoint (restore iteration, RNG, win rate history)',
)
args = parser.parse_args()
run_spsa(resume=args.resume)
