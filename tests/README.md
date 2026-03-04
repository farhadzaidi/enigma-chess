# Test Layout

Tests are grouped by intent:

- `core`: board correctness invariants and state integrity.
- `search`: move ordering, eval, quiescence, and outcome-oriented search behavior.
- `protocol`: SAN/UCI/file parsing and command semantics.
- `behavior`: compatibility alias retained for historical selectors.

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

Run by full path selector or short name:

```bash
./build/enigma test core/zobrist
./build/enigma test zobrist
```

Multiple selectors are supported in one command.
