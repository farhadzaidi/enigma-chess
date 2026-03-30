# Roadmap

## Move Ordering

- **Continuation history** — history scores keyed on the previous move's piece+square, capturing sequential move patterns across plies.

## Search

- **Disable TT cutoffs in PV nodes** — experiment with only using TT entries for move ordering on PV nodes, never for cutoffs, to preserve search accuracy on the principal variation.


