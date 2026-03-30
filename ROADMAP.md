# Roadmap

## Pruning

- **Multi-cut** — piggyback on the SE verification search: if the reduced search without the TT move still returns a score >= beta, multiple moves fail high and the node can be pruned immediately. No extra search needed.
- **Negative extensions** — when the SE verification search shows the TT move is not singular, reduce it instead of extending. Two cases: reduce aggressively (e.g. -3) when the TT score itself is >= beta (the move was strong elsewhere but isn't special here), and moderately (e.g. -2) on cut nodes where the TT move didn't prove singular.
- **Double/triple extensions** — extend by 2 or 3 plies instead of 1 when the SE verification score is far below singularBeta, indicating the TT move is overwhelmingly dominant.
- **PV-aware reduced search margins** — use different margin formulas depending on whether the TT entry came from a PV node (ttPv). PV entries deserve a wider singularity margin since they carry higher-quality information, similar to Stockfish's `(60 + 66 * (ttPv && !PvNode)) * depth / 55`.

## Move Ordering

- **Countermove heuristic** — track which move refuted the opponent's last move and use it as a move ordering hint, similar to killers but keyed on the previous move.
- **Continuation history** — history scores keyed on the previous move's piece+square, capturing sequential move patterns across plies.
- **MVV-LVA ordering in qsearch** — order captures in qsearch by MVV-LVA like we do in the move selector's tactical phase.

## Search

- **Disable TT cutoffs in PV nodes** — experiment with only using TT entries for move ordering on PV nodes, never for cutoffs, to preserve search accuracy on the principal variation.

