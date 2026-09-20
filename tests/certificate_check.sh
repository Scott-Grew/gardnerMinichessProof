#!/usr/bin/env bash
set -euo pipefail

solver_binary="$1"
checker_binary="$2"

output_directory="$HOME/build/gardner-proof/test_output"
mkdir -p "$output_directory"

queen_mate_fen="k4/5/1K3/5/3Q1 w - - 0 1"
queen_mate_certificate="$output_directory/queen_mate.certificate"
missing_entry_certificate="$output_directory/missing_entry.certificate"
bad_move_certificate="$output_directory/bad_move.certificate"

if ! "$solver_binary" prove-win --defender white \
    --table-megabytes 64 --fen "$queen_mate_fen" \
    --output "$queen_mate_certificate"; then
  echo "step prove_win: solver did not exit 0" >&2
  exit 1
fi

if ! accept_output="$("$checker_binary" "$queen_mate_certificate" \
    --defender white --fen "$queen_mate_fen")"; then
  echo "step checker_accept: checker did not exit 0" >&2
  exit 1
fi
if [[ "$accept_output" != ACCEPT* ]]; then
  echo "step checker_accept: did not print ACCEPT" >&2
  exit 1
fi

python3 - "$queen_mate_certificate" "$missing_entry_certificate" \
    <<'PYTHON'
import struct
import sys

source_path, destination_path = sys.argv[1], sys.argv[2]
with open(source_path, "rb") as source_file:
    data = source_file.read()

header = data[:8]
entry_count = struct.unpack_from("<Q", data, 8)[0]
entries = data[16:]
truncated_entries = entries[:-20]
with open(destination_path, "wb") as destination_file:
    destination_file.write(header)
    destination_file.write(struct.pack("<Q", entry_count - 1))
    destination_file.write(truncated_entries)
PYTHON

if "$checker_binary" "$missing_entry_certificate" --defender white \
    --fen "$queen_mate_fen"; then
  echo "step missing_entry: checker accepted a broken certificate" >&2
  exit 1
fi

python3 - "$queen_mate_certificate" "$bad_move_certificate" \
    <<'PYTHON'
import struct
import sys

source_path, destination_path = sys.argv[1], sys.argv[2]
with open(source_path, "rb") as source_file:
    data = bytearray(source_file.read())

entry_count = struct.unpack_from("<Q", data, 8)[0]
for entry_index in range(entry_count):
    entry_offset = 16 + entry_index * 20
    from_square = data[entry_offset + 16]
    if from_square != 255:
        data[entry_offset + 17] = from_square
        break

with open(destination_path, "wb") as destination_file:
    destination_file.write(data)
PYTHON

if "$checker_binary" "$bad_move_certificate" --defender white \
    --fen "$queen_mate_fen"; then
  echo "step bad_move: checker accepted a broken certificate" >&2
  exit 1
fi

echo "certificate_check: all steps passed"
