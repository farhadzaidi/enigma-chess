# Roadmap

## Pruning

- **Singular extensions** — if one move is clearly better than all alternatives (by re-searching with a reduced window excluding it), extend it by one ply to resolve it more accurately.
- **Multi-cut** — if multiple moves beat beta in a reduced search, cut the node immediately without a full-depth search.

## Move Ordering

- **Countermove heuristic** — track which move refuted the opponent's last move and use it as a move ordering hint, similar to killers but keyed on the previous move.
- **Continuation history** — history scores keyed on the previous move's piece+square, capturing sequential move patterns across plies.
- **MVV-LVA ordering in qsearch** — order captures in qsearch by MVV-LVA like we do in the move selector's tactical phase.

## Search

- **Disable TT cutoffs in PV nodes** — experiment with only using TT entries for move ordering on PV nodes, never for cutoffs, to preserve search accuracy on the principal variation.
