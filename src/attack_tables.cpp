#include "attack_tables.h"

#include <array>
#include <bit>
#include <utility>
#include <vector>

#if defined(__BMI2__)
#include <immintrin.h>
#endif

namespace gardner {

namespace {

using LeaperOffsets = std::array<std::pair<int, int>, 8>;
using DirectionList = std::array<std::pair<int, int>, 4>;

// Knight leap offsets, checked against the board before use.
constexpr LeaperOffsets kKnightOffsets{{{1, 2},
                                        {2, 1},
                                        {2, -1},
                                        {1, -2},
                                        {-1, -2},
                                        {-2, -1},
                                        {-2, 1},
                                        {-1, 2}}};

// King step offsets, checked against the board before use.
constexpr LeaperOffsets kKingOffsets{{{1, 0},
                                      {1, 1},
                                      {0, 1},
                                      {-1, 1},
                                      {-1, 0},
                                      {-1, -1},
                                      {0, -1},
                                      {1, -1}}};

// Diagonal ray directions for bishops and queens.
constexpr DirectionList kBishopDirections{
    {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}}};

// Orthogonal ray directions for rooks and queens.
constexpr DirectionList kRookDirections{
    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Attack bitboard for a leaper (knight or king) from a square,
// given its list of (file, rank) offsets.
std::uint32_t leaper_attacks(int square_index,
                             const LeaperOffsets& offsets) {
  std::uint32_t attacks = 0;
  const int origin_file = file_of(square_index);
  const int origin_rank = rank_of(square_index);
  for (const auto& [file_offset, rank_offset] : offsets) {
    const int target_file = origin_file + file_offset;
    const int target_rank = origin_rank + rank_offset;
    if (is_valid_file(target_file) && is_valid_rank(target_rank)) {
      attacks |= std::uint32_t{1}
                 << square_of(target_file, target_rank);
    }
  }
  return attacks;
}

// Squares reachable in these directions on an empty board;
// exclude_final_square drops the last square (edge) for masks.
std::uint32_t ray_squares(int square_index,
                          const DirectionList& directions,
                          bool exclude_final_square) {
  std::uint32_t squares = 0;
  const int origin_file = file_of(square_index);
  const int origin_rank = rank_of(square_index);
  for (const auto& [file_step, rank_step] : directions) {
    int current_file = origin_file + file_step;
    int current_rank = origin_rank + rank_step;
    int final_square = -1;
    while (is_valid_file(current_file) &&
           is_valid_rank(current_rank)) {
      final_square = square_of(current_file, current_rank);
      squares |= std::uint32_t{1} << final_square;
      current_file += file_step;
      current_rank += rank_step;
    }
    if (exclude_final_square && final_square != -1) {
      squares &= ~(std::uint32_t{1} << final_square);
    }
  }
  return squares;
}

// Attack bitboard for a slider from a square given actual
// occupancy, stopping at (and including) the first blocker.
std::uint32_t ray_attacks(int square_index,
                          const DirectionList& directions,
                          std::uint32_t occupied_squares) {
  std::uint32_t attacks = 0;
  const int origin_file = file_of(square_index);
  const int origin_rank = rank_of(square_index);
  for (const auto& [file_step, rank_step] : directions) {
    int current_file = origin_file + file_step;
    int current_rank = origin_rank + rank_step;
    while (is_valid_file(current_file) &&
           is_valid_rank(current_rank)) {
      const int current_square =
          square_of(current_file, current_rank);
      attacks |= std::uint32_t{1} << current_square;
      if (occupied_squares & (std::uint32_t{1} << current_square)) {
        break;
      }
      current_file += file_step;
      current_rank += rank_step;
    }
  }
  return attacks;
}

// Precomputed slider attack table: per-square mask, the mask's
// start offset into table, and the packed table itself.
struct SliderTables {
  std::array<std::uint32_t, kSquareCount> masks{};
  std::array<std::size_t, kSquareCount> offsets{};
  std::vector<std::uint32_t> table;
};

// Builds a magic-free slider table: one entry per subset of
// each square's mask, indexed by PEXT of the occupancy.
SliderTables build_slider_tables(const DirectionList& directions) {
  SliderTables tables;
  std::size_t running_offset = 0;
  for (int square_index = 0; square_index < kSquareCount;
       ++square_index) {
    const std::uint32_t mask =
        ray_squares(square_index, directions, true);
    tables.masks[square_index] = mask;
    tables.offsets[square_index] = running_offset;
    running_offset += std::size_t{1} << std::popcount(mask);
  }
  tables.table.resize(running_offset);

  for (int square_index = 0; square_index < kSquareCount;
       ++square_index) {
    const std::uint32_t mask = tables.masks[square_index];
    const std::size_t offset = tables.offsets[square_index];
    std::uint32_t subset = 0;
    do {
      const std::uint32_t index = extract_bits(subset, mask);
      tables.table[offset + index] =
          ray_attacks(square_index, directions, subset);
      // Steps subset through every submask of mask, back to 0
      // last.
      subset = (subset - mask) & mask;
    } while (subset != 0);
  }
  return tables;
}

// The shared, lazily built bishop slider table.
const SliderTables& bishop_tables() {
  static const SliderTables tables =
      build_slider_tables(kBishopDirections);
  return tables;
}

// The shared, lazily built rook slider table.
const SliderTables& rook_tables() {
  static const SliderTables tables =
      build_slider_tables(kRookDirections);
  return tables;
}

}

// Knight attack bitboard from a square.
std::uint32_t knight_attacks(int square_index) {
  return leaper_attacks(square_index, kKnightOffsets);
}

// King attack bitboard from a square.
std::uint32_t king_attacks(int square_index) {
  return leaper_attacks(square_index, kKingOffsets);
}

// Pawn capture squares from a square for the given pawn
// color; White pawns capture toward rank 5.
std::uint32_t pawn_attacks(Color pawn_color, int square_index) {
  const int origin_file = file_of(square_index);
  const int origin_rank = rank_of(square_index);
  const int rank_step = pawn_color == Color::White ? 1 : -1;
  std::uint32_t attacks = 0;
  for (const int file_step : {-1, 1}) {
    const int target_file = origin_file + file_step;
    const int target_rank = origin_rank + rank_step;
    if (is_valid_file(target_file) && is_valid_rank(target_rank)) {
      attacks |= std::uint32_t{1}
                 << square_of(target_file, target_rank);
    }
  }
  return attacks;
}

// Bishop attack bitboard from a square under the given
// occupancy, looked up via the PEXT-indexed slider table.
std::uint32_t bishop_attacks(int square_index,
                             std::uint32_t occupancy) {
  const SliderTables& tables = bishop_tables();
  const std::uint32_t mask = tables.masks[square_index];
  const std::uint32_t index = extract_bits(occupancy, mask);
  return tables.table[tables.offsets[square_index] + index];
}

// Rook attack bitboard from a square under the given
// occupancy, looked up via the PEXT-indexed slider table.
std::uint32_t rook_attacks(int square_index,
                           std::uint32_t occupancy) {
  const SliderTables& tables = rook_tables();
  const std::uint32_t mask = tables.masks[square_index];
  const std::uint32_t index = extract_bits(occupancy, mask);
  return tables.table[tables.offsets[square_index] + index];
}

// Queen attack bitboard: the union of bishop and rook attacks
// from the same square and occupancy.
std::uint32_t queen_attacks(int square_index,
                            std::uint32_t occupancy) {
  return bishop_attacks(square_index, occupancy) |
         rook_attacks(square_index, occupancy);
}

// Gathers the mask's set bits out of value into a dense
// low-order result; uses PEXT when the target has BMI2.
std::uint32_t extract_bits(std::uint32_t value, std::uint32_t mask) {
#if defined(__BMI2__)
  return _pext_u32(value, mask);
#else
  std::uint32_t result = 0;
  std::uint32_t result_bit = 1;
  std::uint32_t remaining_mask = mask;
  while (remaining_mask != 0) {
    // Isolates the lowest set bit of remaining_mask.
    const std::uint32_t lowest_mask_bit =
        remaining_mask & (~remaining_mask + 1);
    if (value & lowest_mask_bit) {
      result |= result_bit;
    }
    remaining_mask &= remaining_mask - 1;
    result_bit <<= 1;
  }
  return result;
#endif
}

// Size of the rook slider table, for reporting.
std::size_t rook_table_entry_count() {
  return rook_tables().table.size();
}

// Size of the bishop slider table, for reporting.
std::size_t bishop_table_entry_count() {
  return bishop_tables().table.size();
}

}

// Slider lookup, shown for a rook on c3 (square 12). The last square
// of each ray is left out of the mask: a blocker there hides nothing.
//
//   rank 5 |  .  .  -  .  .       R  the rook
//   rank 4 |  .  .  x  .  .       x  square in the occupancy mask
//   rank 3 |  -  x  R  x  -       -  ray square outside the mask
//   rank 2 |  .  .  x  .  .
//   rank 1 |  .  .  -  .  .
//             a  b  c  d  e
//
//   index   = extract_bits(occupancy, mask[12])     4 bits here
//   attacks = table[offset[12] + index]
//
// A corner rook has 6 mask bits and the centre has 4. A bishop has
// at most 4.
