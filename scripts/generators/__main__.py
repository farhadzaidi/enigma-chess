from generators.book import main as generate_book
from generators.nnue import main as generate_nnue
from generators.params import main as generate_params


def main():
    generate_book()
    generate_nnue()
    generate_params()


main()
