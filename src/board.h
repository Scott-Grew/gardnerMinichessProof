#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gardner {

// Board bounds; squares are indexed rank_index * 5 +
// file_index, so a1 = 0.
inline constexpr int kBoardFiles = 5;
inline constexpr int kBoardRanks = 5;
inline constexpr int kSquareCount = 25;

// A side to play; positions are normalised elsewhere so the
// defender is always White.
enum class Color : std::uint8_t { White, Black };

// The six piece kinds plus None, used for an empty square and
// for a non-promoting move's promotion field.
enum class PieceType : std::uint8_t {
  Pawn,
  Knight,
  Bishop,
  Rook,
  Queen,
  King,
  None
};

// One move: source square, destination square, and the
// promotion piece, or None when it is not a promotion.
struct Move {
  std::uint8_t from_square;
  std::uint8_t to_square;
  PieceType promotion;

  bool operator==(const Move&) const = default;
};

// A board position: bitboards by piece type and by color plus
// the side to move; a square's piece has bits set in both.
struct Position {
  std::array<std::uint32_t, 6> pieces_by_type;
  std::array<std::uint32_t, 2> pieces_by_color;
  Color side_to_move;

  bool operator==(const Position&) const = default;
};

// Exact identity of a position as two 64-bit words: a 4-bit
// piece code per square, plus a bit for side to move.
struct PositionKey {
  std::uint64_t low;
  std::uint64_t high;

  auto operator<=>(const PositionKey&) const = default;
};

// The file (column) of a square index, 0 = a-file.
constexpr int file_of(int square_index) {
  return square_index % kBoardFiles;
}

// The rank (row) of a square index, 0 = rank 1.
constexpr int rank_of(int square_index) {
  return square_index / kBoardFiles;
}

// Square index for a file and rank: rank * 5 + file.
constexpr int square_of(int file_index, int rank_index) {
  return rank_index * kBoardFiles + file_index;
}

// True when a file index lies within the board.
constexpr bool is_valid_file(int file_index) {
  return file_index >= 0 && file_index < kBoardFiles;
}

// True when a rank index lies within the board.
constexpr bool is_valid_rank(int rank_index) {
  return rank_index >= 0 && rank_index < kBoardRanks;
}

Color opposite_color(Color color);
std::uint32_t occupancy(const Position& position);
PieceType piece_type_on(const Position& position, int square_index);
Position make_move(const Position& position, Move move);
std::optional<Position> position_from_fen(std::string_view fen);
std::string fen_from_position(const Position& position);
std::string uci_from_move(Move move);
PositionKey exact_key(const Position& position);

}
