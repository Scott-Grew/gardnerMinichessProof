#pragma once

#include <cstddef>
#include <cstdint>

#include "board.h"

namespace gardner {

std::uint32_t knight_attacks(int square_index);
std::uint32_t king_attacks(int square_index);
std::uint32_t pawn_attacks(Color pawn_color, int square_index);
std::uint32_t bishop_attacks(int square_index,
                             std::uint32_t occupancy);
std::uint32_t rook_attacks(int square_index, std::uint32_t occupancy);
std::uint32_t queen_attacks(int square_index,
                            std::uint32_t occupancy);

std::uint32_t extract_bits(std::uint32_t value, std::uint32_t mask);

std::size_t rook_table_entry_count();
std::size_t bishop_table_entry_count();

}
