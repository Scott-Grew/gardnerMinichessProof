#include "canonical.h"

#include <array>
#include <cstdint>

namespace gardner {

namespace {

// Builds a lookup table that reverses the low 5 bits of a
// byte, for mirroring a rank's file order.
constexpr std::array<std::uint8_t, 32>
build_five_bit_reversal_table() {
  std::array<std::uint8_t, 32> table{};
  for (int pattern = 0; pattern < 32; ++pattern) {
    std::uint8_t reversed_pattern = 0;
    for (int bit_index = 0; bit_index < kBoardFiles; ++bit_index) {
      if (pattern & (1 << bit_index)) {
        reversed_pattern |= static_cast<std::uint8_t>(
            1 << (kBoardFiles - 1 - bit_index));
      }
    }
    table[pattern] = reversed_pattern;
  }
  return table;
}

// The 5-bit file-reversal table, computed once at compile
// time.
constexpr std::array<std::uint8_t, 32> kFiveBitReversalTable =
    build_five_bit_reversal_table();

// Mirrors a bitboard left-right, one 5-bit rank row at a
// time, via the reversal table.
std::uint32_t mirror_files_bitboard(std::uint32_t bitboard) {
  std::uint32_t result = 0;
  for (int rank_index = 0; rank_index < kBoardRanks; ++rank_index) {
    const std::uint32_t row =
        (bitboard >> (rank_index * kBoardFiles)) & 0x1F;
    result |= static_cast<std::uint32_t>(kFiveBitReversalTable[row])
              << (rank_index * kBoardFiles);
  }
  return result;
}

// Flips a bitboard top-to-bottom, moving each 5-bit rank row
// to its mirrored rank.
std::uint32_t flip_ranks_bitboard(std::uint32_t bitboard) {
  std::uint32_t result = 0;
  for (int rank_index = 0; rank_index < kBoardRanks; ++rank_index) {
    const std::uint32_t row =
        (bitboard >> (rank_index * kBoardFiles)) & 0x1F;
    const int mirrored_rank_index = kBoardRanks - 1 - rank_index;
    result |= row << (mirrored_rank_index * kBoardFiles);
  }
  return result;
}

// File-mirrors a single square index, same rank.
std::uint8_t mirror_files_square(std::uint8_t square_index) {
  const int file_index = file_of(square_index);
  const int rank_index = rank_of(square_index);
  return static_cast<std::uint8_t>(
      square_of(kBoardFiles - 1 - file_index, rank_index));
}

// Rank-flips a single square index, same file.
std::uint8_t swap_colors_square(std::uint8_t square_index) {
  const int file_index = file_of(square_index);
  const int rank_index = rank_of(square_index);
  return static_cast<std::uint8_t>(
      square_of(file_index, kBoardRanks - 1 - rank_index));
}

}

// Left-right mirror of a position: every bitboard file-
// mirrored; side to move and piece identity unchanged.
Position mirror_files(const Position& position) {
  Position result = position;
  for (std::uint32_t& bitboard : result.pieces_by_type) {
    bitboard = mirror_files_bitboard(bitboard);
  }
  for (std::uint32_t& bitboard : result.pieces_by_color) {
    bitboard = mirror_files_bitboard(bitboard);
  }
  return result;
}

// Recolors a position: ranks flip and White/Black bitboards
// swap, so a Black-to-move position becomes White's mirror.
Position swap_colors(const Position& position) {
  Position result{};
  for (int piece_type_index = 0; piece_type_index < 6;
       ++piece_type_index) {
    result.pieces_by_type[piece_type_index] = flip_ranks_bitboard(
        position.pieces_by_type[piece_type_index]);
  }
  result.pieces_by_color[static_cast<int>(Color::White)] =
      flip_ranks_bitboard(
          position.pieces_by_color[static_cast<int>(Color::Black)]);
  result.pieces_by_color[static_cast<int>(Color::Black)] =
      flip_ranks_bitboard(
          position.pieces_by_color[static_cast<int>(Color::White)]);
  result.side_to_move = opposite_color(position.side_to_move);
  return result;
}

// File-mirrors both squares of a move.
Move mirror_files_move(Move move) {
  return Move{mirror_files_square(move.from_square),
              mirror_files_square(move.to_square), move.promotion};
}

// Rank-flips both squares of a move.
Move swap_colors_move(Move move) {
  return Move{swap_colors_square(move.from_square),
              swap_colors_square(move.to_square), move.promotion};
}

// Returns the position as seen with the defender playing
// White, colour-swapping only when the defender is Black.
Position defender_as_white(const Position& position, Color defender) {
  if (defender == Color::White) return position;
  return swap_colors(position);
}

// Chooses the file-mirror or the original, whichever has the
// smaller exact key, folding mirror-symmetric positions.
CanonicalForm canonical_under_mirror(const Position& position) {
  const Position mirrored = mirror_files(position);
  const PositionKey original_key = exact_key(position);
  const PositionKey mirrored_key = exact_key(mirrored);
  if (mirrored_key < original_key) {
    return CanonicalForm{mirrored, true};
  }
  return CanonicalForm{position, false};
}

}

// The two symmetries, shown on the starting position.
//
//   position          mirror_files        swap_colors
//   r n b q k         k q b n r           r n b q k
//   p p p p p         p p p p p           p p p p p
//   . . . . .         . . . . .           . . . . .
//   P P P P P         P P P P P           P P P P P
//   R N B Q K         K Q B N R           R N B Q K
//   White to move     White to move       Black to move
//
// mirror_files sends file f to 4 - f. swap_colors sends rank r to
// 4 - r, changes every piece's colour and flips the side to move.
// The canonical form is whichever of a position and its mirror has
// the smaller exact key.
