#!/usr/bin/env bash
set -euo pipefail

solver_binary="$1"
checker_binary="$2"

normalize() {
  grep -E \
    '^[a-e][1-5][a-e][1-5][qrbn]?: [0-9]+$|^Nodes searched: [0-9]+$' \
    | sort
}

pairs=(
  "6|rnbqk/ppppp/5/PPPPP/RNBQK w - - 0 1"
  "5|1n2k/P4/5/5/4K w - - 0 1"
  "5|4k/5/5/p4/1N2K b - - 0 1"
  "5|r3k/1pq2/2P2/1B1Q1/R3K w - - 0 1"
  "5|4k/5/5/3q1/4K w - - 0 1"
)

if ! command -v fairy-stockfish >/dev/null 2>&1; then
  echo "fairy-stockfish not found on PATH" >&2
  exit 1
fi

for pair in "${pairs[@]}"; do
  depth="${pair%%|*}"
  fen="${pair#*|}"

  solver_output="$("$solver_binary" perft "$depth" \
      "$fen" | normalize)"
  checker_output="$("$checker_binary" "$depth" "$fen" \
      | normalize)"
  reference_output="$(printf \
      'uci\n'\
'setoption name UCI_Variant value gardner\n'\
'position fen %s\n'\
'go perft %s\n'\
'quit\n' \
      "$fen" "$depth" \
      | fairy-stockfish | normalize)"

  if [[ "$solver_output" != "$checker_output" ]]; then
    echo "mismatch solver vs checker" \
        "at depth $depth fen $fen" >&2
    diff <(echo "$solver_output") \
        <(echo "$checker_output") >&2
    exit 1
  fi

  if [[ "$solver_output" != "$reference_output" ]]; then
    echo "mismatch solver vs fairy-stockfish" \
        "at depth $depth fen $fen" >&2
    diff <(echo "$solver_output") \
        <(echo "$reference_output") >&2
    exit 1
  fi
done

echo "perft_agreement: all pairs match"
