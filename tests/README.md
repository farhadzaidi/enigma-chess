# Test Layout

Test sources are organized by domain:

```text
tests/
  core/
  search/
  protocol/
```

Selectors are logical and independent of file paths:

- `core`: board legality, hashing, state invariants, and table behavior.
- `search`: move ordering, eval, limits, and outcome-oriented search behavior.
- `protocol`: SAN/UCI/file parsing and command semantics.
- `behavior`: compatibility alias for `search`.

## Running

Run all tests:

```bash
./build/enigma test
```

Run by group:

```bash
./build/enigma test core
./build/enigma test search
./build/enigma test protocol
./build/enigma test behavior
```

Run by full selector:

```bash
./build/enigma test core/zobrist
./build/enigma test search/move_selector
./build/enigma test protocol/uci_helpers
```

Multiple selectors are supported in one command.
