from generators.book import main as generate_book
from generators.nnue_weights import main as generate_nnue_weights
from generators.params import generate, TARGETS


def main():
    generate_book()
    generate_nnue_weights()
    for config in TARGETS.values():
        generate(config)


main()