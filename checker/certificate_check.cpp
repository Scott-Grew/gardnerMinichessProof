#include "certificate_check.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>

namespace checker {

namespace {

// Certificate file layout: an 8-byte magic string, an 8-byte
// little-endian entry count, then fixed 20-byte entries.
constexpr std::size_t kMagicLength = 8;
constexpr std::size_t kHeaderLength = kMagicLength + 8;
constexpr std::size_t kEntryLength = 20;
constexpr std::string_view kMagicText = "GARDNER1";

// Reads 8 little-endian bytes starting at source_bytes.
std::uint64_t read_uint64_le(const unsigned char* source_bytes) {
  std::uint64_t value = 0;
  for (int byte_index = 7; byte_index >= 0; byte_index--) {
    value = (value << 8) | source_bytes[byte_index];
  }
  return value;
}

// Maps a piece to the same kind for the other colour, using the
// White/Black split at codes 1-6 versus 7-12.
Piece colour_flipped_piece(Piece piece) {
  if (piece == Piece::Empty) {
    return Piece::Empty;
  }
  int piece_value = static_cast<int>(piece);
  int flipped_value =
      piece_value <= 6 ? piece_value + 6 : piece_value - 6;
  return static_cast<Piece>(flipped_value);
}

// Binary searches the sorted entries for target_key; entries
// must already be sorted ascending by key.
std::optional<std::size_t> find_entry_index(
    const std::vector<CertificateEntry>& entries,
    const BoardKey& target_key) {
  auto found_iterator = std::lower_bound(
      entries.begin(), entries.end(), target_key,
      [](const CertificateEntry& entry, const BoardKey& key) {
        return entry.key < key;
      });
  if (found_iterator == entries.end() ||
      !(found_iterator->key == target_key)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found_iterator - entries.begin());
}

}

// Orders keys by low word first, then high word.
bool operator<(const BoardKey& left, const BoardKey& right) {
  if (left.low != right.low) {
    return left.low < right.low;
  }
  return left.high < right.high;
}

// True if both words match exactly.
bool operator==(const BoardKey& left, const BoardKey& right) {
  return left.low == right.low && left.high == right.high;
}

// Packs board into a key: squares 0 to 15 as nibbles in low,
// squares 16 to 24 in high, side to move as bit 36 of high.
BoardKey key_from_board(const Board& board) {
  BoardKey key{0, 0};
  for (int square_index = 0; square_index < 16; square_index++) {
    std::uint64_t nibble_value =
        static_cast<std::uint64_t>(board.squares[square_index]);
    key.low |= nibble_value << (4 * square_index);
  }
  for (int square_index = 16; square_index < 25; square_index++) {
    std::uint64_t nibble_value =
        static_cast<std::uint64_t>(board.squares[square_index]);
    key.high |= nibble_value << (4 * (square_index - 16));
  }
  if (board.side_to_move == Side::Black) {
    key.high |= std::uint64_t{1} << 36;
  }
  return key;
}

// Inverse of key_from_board. Rejects a key with any stray high
// bit, an out-of-range nibble, or not one king per side.
std::optional<Board> board_from_key(const BoardKey& key) {
  if ((key.high >> 37) != 0) {
    return std::nullopt;
  }

  Board board{};
  int white_king_count = 0;
  int black_king_count = 0;

  for (int square_index = 0; square_index < 25; square_index++) {
    std::uint64_t nibble_value =
        square_index < 16
            ? (key.low >> (4 * square_index)) & 0xF
            : (key.high >> (4 * (square_index - 16))) & 0xF;
    if (nibble_value > 12) {
      return std::nullopt;
    }
    Piece piece_value = static_cast<Piece>(nibble_value);
    board.squares[square_index] = piece_value;
    if (piece_value == Piece::WhiteKing) {
      white_king_count++;
    }
    if (piece_value == Piece::BlackKing) {
      black_king_count++;
    }
  }

  if (white_king_count != 1 || black_king_count != 1) {
    return std::nullopt;
  }

  board.side_to_move =
      ((key.high >> 36) & 1) == 1 ? Side::Black : Side::White;
  return board;
}

// Reflects the board left to right, file index f becomes 4-f,
// leaving rank and side to move unchanged.
Board mirror_board(const Board& board) {
  Board mirrored{};
  for (int rank_index = 0; rank_index < 5; rank_index++) {
    for (int file_index = 0; file_index < 5; file_index++) {
      int source_square = rank_index * 5 + file_index;
      int mirrored_square = rank_index * 5 + (4 - file_index);
      mirrored.squares[mirrored_square] =
          board.squares[source_square];
    }
  }
  mirrored.side_to_move = board.side_to_move;
  return mirrored;
}

// Reverses ranks, flips every piece's colour, and flips the side
// to move, giving the position with White and Black exchanged.
Board swap_colours(const Board& board) {
  Board swapped{};
  for (int rank_index = 0; rank_index < 5; rank_index++) {
    for (int file_index = 0; file_index < 5; file_index++) {
      int source_square = rank_index * 5 + file_index;
      int destination_square = (4 - rank_index) * 5 + file_index;
      swapped.squares[destination_square] =
          colour_flipped_piece(board.squares[source_square]);
    }
  }
  swapped.side_to_move =
      board.side_to_move == Side::White ? Side::Black : Side::White;
  return swapped;
}

// The key the certificate stores a position under: the smaller
// of the position's own key and its left-right mirror's key.
BoardKey canonical_key(const Board& board) {
  BoardKey direct_key = key_from_board(board);
  BoardKey mirrored_key = key_from_board(mirror_board(board));
  return mirrored_key < direct_key ? mirrored_key : direct_key;
}

// Loads and validates a certificate file's shape: magic bytes,
// declared size, zero padding, and strictly ascending keys.
std::optional<std::vector<CertificateEntry>> read_certificate(
    const std::string& path, std::string& failure_reason) {
  std::ifstream file_stream(path, std::ios::binary);
  if (!file_stream) {
    failure_reason = "cannot open file";
    return std::nullopt;
  }

  std::vector<unsigned char> file_bytes(
      (std::istreambuf_iterator<char>(file_stream)),
      std::istreambuf_iterator<char>());

  if (file_bytes.size() < kHeaderLength) {
    failure_reason = "file too short";
    return std::nullopt;
  }
  std::string_view magic_bytes(
      reinterpret_cast<const char*>(file_bytes.data()), kMagicLength);
  if (magic_bytes != kMagicText) {
    failure_reason = "bad magic";
    return std::nullopt;
  }

  std::uint64_t entry_count =
      read_uint64_le(file_bytes.data() + kMagicLength);
  std::size_t expected_size =
      kHeaderLength + entry_count * kEntryLength;
  if (file_bytes.size() != expected_size) {
    failure_reason = "size mismatch";
    return std::nullopt;
  }

  std::vector<CertificateEntry> entries;
  entries.reserve(entry_count);
  std::size_t cursor = kHeaderLength;
  std::optional<BoardKey> previous_key;

  for (std::uint64_t entry_index = 0; entry_index < entry_count;
       entry_index++) {
    const unsigned char* entry_bytes = file_bytes.data() + cursor;
    BoardKey key{read_uint64_le(entry_bytes),
                 read_uint64_le(entry_bytes + 8)};
    int from_square = entry_bytes[16];
    int to_square = entry_bytes[17];
    int promotion_code = entry_bytes[18];
    unsigned char padding_byte = entry_bytes[19];

    if (padding_byte != 0) {
      failure_reason = "nonzero padding byte at entry " +
                       std::to_string(entry_index);
      return std::nullopt;
    }
    if (previous_key.has_value() && !(*previous_key < key)) {
      failure_reason = "keys not ascending at entry " +
                       std::to_string(entry_index);
      return std::nullopt;
    }

    entries.push_back({key, from_square, to_square, promotion_code});
    previous_key = key;
    cursor += kEntryLength;
  }

  return entries;
}

// Confirms S is closed: the root is in S, attacker replies
// stay in S, and each defender's stored move is legal and in S.
CheckOutcome check_certificate(
    const std::vector<CertificateEntry>& entries,
    const Board& real_root, Side defender) {
  Board normalised_root =
      defender == Side::Black ? swap_colours(real_root) : real_root;
  if (!find_entry_index(entries, canonical_key(normalised_root))
           .has_value()) {
    return {false, "root missing", entries.size()};
  }

  for (std::size_t entry_index = 0; entry_index < entries.size();
       entry_index++) {
    const CertificateEntry& entry = entries[entry_index];
    std::optional<Board> decoded_board = board_from_key(entry.key);
    if (!decoded_board.has_value()) {
      return {false,
              "undecodable entry " + std::to_string(entry_index),
              entries.size()};
    }
    const Board& board = *decoded_board;

    if (board.side_to_move == Side::Black) {
      for (const BoardMove& attacker_move : legal_moves(board)) {
        Board resulting_board = apply_move(board, attacker_move);
        if (!find_entry_index(entries, canonical_key(resulting_board))
                 .has_value()) {
          return {false,
                  "attacker move escapes at entry " +
                      std::to_string(entry_index) + ": " +
                      move_text(attacker_move),
                  entries.size()};
        }
      }
      continue;
    }

    std::vector<BoardMove> defender_moves = legal_moves(board);
    if (defender_moves.empty()) {
      if (is_king_attacked(board, Side::White)) {
        return {false,
                "defender checkmated at entry " +
                    std::to_string(entry_index),
                entries.size()};
      }
      continue;
    }

    Piece stored_promotion_piece =
        entry.promotion_code == 0
            ? Piece::Empty
            : static_cast<Piece>(entry.promotion_code);

    const BoardMove* matched_move = nullptr;
    for (const BoardMove& candidate_move : defender_moves) {
      if (candidate_move.from_square == entry.from_square &&
          candidate_move.to_square == entry.to_square &&
          candidate_move.promotion_piece == stored_promotion_piece) {
        matched_move = &candidate_move;
        break;
      }
    }
    if (matched_move == nullptr) {
      return {false,
              "stored move illegal at entry " +
                  std::to_string(entry_index),
              entries.size()};
    }

    Board resulting_board = apply_move(board, *matched_move);
    if (!find_entry_index(entries, canonical_key(resulting_board))
             .has_value()) {
      return {false,
              "stored move escapes at entry " +
                  std::to_string(entry_index),
              entries.size()};
    }
  }

  return {true, "", entries.size()};
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
// What acceptance means. S is the set of positions in the file, and
// the defender is White in every one of them.
//
//               root  (must be in S)
//                 |
//     +-----------v---------------------------------+
//     |  Black to move           White to move      |
//     |  every legal move        the stored move is |
//     |  lands in S              legal, lands in S  |
//     |          \                   /              |
//     |           v                 v               |
//     |            other positions in S             |
//     +---------------------------------------------+
//
// No move leaves S and White is never checkmated inside it, so
// White cannot be checkmated from the root.
