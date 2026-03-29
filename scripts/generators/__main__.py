from generators.book import main as generate_book
from generators.nnue_weights import main as generate_nnue_weights
from generators.tm_weights import main as generate_tm_weights
from generators.search_params import main as generate_search_params


def main():
    generate_book()
    generate_nnue_weights()
    generate_tm_weights()
    generate_search_params()


main()