#include "certificate.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

#include "canonical.h"
#include "movegen.h"

namespace gardner {

namespace {

// Certificate promotion code: piece_type + 1 for the four
// promotable pieces, 0 for a non-promotion.
std::uint8_t promotion_code_from_piece_type(PieceType piece_type) {
  switch (piece_type) {
    case PieceType::Knight:
      return 2;
    case PieceType::Bishop:
      return 3;
    case PieceType::Rook:
      return 4;
    case PieceType::Queen:
      return 5;
    default:
      return 0;
  }
}

// Recursively walks the proof tree from position into
// entries, one row per canonical position (mirror-folded).
bool walk_certificate(
    ProofSearch& search, const Position& position,
    std::unordered_set<PositionKey, PositionKeyHash>& visited,
    std::vector<CertificateEntry>& entries) {
  const CanonicalForm canonical_form =
      canonical_under_mirror(position);
  const PositionKey canonical_key =
      exact_key(canonical_form.position);
  if (visited.contains(canonical_key)) return true;
  visited.insert(canonical_key);

  if (position.side_to_move == Color::Black) {
    entries.push_back(CertificateEntry{canonical_key, 255, 255, 0});
    for (const Move move : generate_legal_moves(position)) {
      if (!walk_certificate(search, make_move(position, move),
                            visited, entries)) {
        return false;
      }
    }
    return true;
  }

  std::optional<Move> move_to_play = search.proving_move(position);
  // Move was evicted from the table; reprove this position.
  if (!move_to_play.has_value()) {
    search.prove(position);
    move_to_play = search.proving_move(position);
  }
  if (!move_to_play.has_value()) return false;

  const Move move_in_canonical_frame =
      canonical_form.was_mirrored ? mirror_files_move(*move_to_play)
                                  : *move_to_play;
  entries.push_back(CertificateEntry{
      canonical_key, move_in_canonical_frame.from_square,
      move_in_canonical_frame.to_square,
      promotion_code_from_piece_type(
          move_in_canonical_frame.promotion)});
  return walk_certificate(search, make_move(position, *move_to_play),
                          visited, entries);
}

}

// Extracts a certificate as entries sorted by key from a
// completed proof, or nullopt if a move could not be resolved.
std::optional<std::vector<CertificateEntry>> extract_certificate(
    ProofSearch& search, const Position& normalised_root) {
  std::unordered_set<PositionKey, PositionKeyHash> visited;
  std::vector<CertificateEntry> entries;
  if (!walk_certificate(search, normalised_root, visited, entries)) {
    return std::nullopt;
  }
  std::sort(entries.begin(), entries.end(),
            [](const CertificateEntry& left,
               const CertificateEntry& right) {
              return left.key < right.key;
            });
  return entries;
}

// Writes entries to path as a GARDNER1-tagged binary file:
// a header then one fixed 20-byte record per entry.
bool write_certificate(const std::string& path,
                       const std::vector<CertificateEntry>& entries) {
  std::ofstream output_stream(path, std::ios::binary);
  if (!output_stream) return false;

  output_stream.write("GARDNER1", 8);
  const std::uint64_t entry_count = entries.size();
  output_stream.write(reinterpret_cast<const char*>(&entry_count),
                      sizeof(entry_count));

  for (const CertificateEntry& entry : entries) {
    output_stream.write(reinterpret_cast<const char*>(&entry.key.low),
                        sizeof(entry.key.low));
    output_stream.write(
        reinterpret_cast<const char*>(&entry.key.high),
        sizeof(entry.key.high));
    output_stream.write(
        reinterpret_cast<const char*>(&entry.from_square), 1);
    output_stream.write(
        reinterpret_cast<const char*>(&entry.to_square), 1);
    output_stream.write(
        reinterpret_cast<const char*>(&entry.promotion_code), 1);
    const std::uint8_t padding_byte = 0;
    output_stream.write(reinterpret_cast<const char*>(&padding_byte),
                        1);
  }

  return output_stream.good();
}

// Counts certificate entries by piece count on the board (0
// to kSquareCount men), read directly from each key's nibbles.
std::array<std::uint64_t, 26> men_histogram(
    const std::vector<CertificateEntry>& entries) {
  std::array<std::uint64_t, 26> histogram{};
  for (const CertificateEntry& entry : entries) {
    int men_count = 0;
    for (int square_index = 0; square_index < kSquareCount;
         ++square_index) {
      // Unpacks each square's nibble: 0-15 from key.low, 16-24
      // from key.high.
      const std::uint64_t nibble =
          square_index < 16
              ? (entry.key.low >> (square_index * 4)) & 0xF
              : (entry.key.high >> ((square_index - 16) * 4)) & 0xF;
      if (nibble != 0) ++men_count;
    }
    ++histogram[static_cast<std::size_t>(men_count)];
  }
  return histogram;
}

}

// Certificate file, little-endian.
//
//   offset 0     8 bytes    "GARDNER1"
//   offset 8     8 bytes    entry count
//   offset 16    20 bytes per entry, ascending by (low, high)
//
//   entry  | key.low 8 | key.high 8 | from 1 | to 1 | promo 1 | 0 |
//
// Black to move: from and to are 255. White to move: the stored
// move in the frame of the stored position, with promo 0 or 2 to 5
// for knight, bishop, rook, queen.
