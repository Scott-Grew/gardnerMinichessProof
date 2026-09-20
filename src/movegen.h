#pragma once

#include <array>
#include <cassert>
#include <cstdint>

#include "board.h"

namespace gardner {

// Upper bound on legal moves in one position on this 5x5
// board; overflow would mean a modelling bug.
constexpr int kMaximumLegalMoves = 160;

// Fixed-capacity move buffer (see kMaximumLegalMoves) backed
// by a plain array; avoids heap allocation per node.
class MoveList {
 public:
  // Appends a move; asserts if the fixed-size buffer is full.
  void push_back(Move move) {
    assert(move_count_ < kMaximumLegalMoves);
    moves_[move_count_] = move;
    ++move_count_;
  }

  // Number of moves currently stored.
  int size() const { return move_count_; }

  // Pointer to the first move, for range-based iteration.
  const Move* begin() const { return moves_.data(); }
  // Pointer one past the last move, for range-based iteration.
  const Move* end() const { return moves_.data() + move_count_; }

  // The move at index, unchecked.
  Move operator[](int index) const { return moves_[index]; }

 private:
  std::array<Move, kMaximumLegalMoves> moves_{};
  int move_count_ = 0;
};

bool is_square_attacked(const Position& position, int square_index,
                        Color attacking_color);
bool is_in_check(const Position& position, Color king_color);
MoveList generate_legal_moves(const Position& position);
std::uint64_t perft(const Position& position, int depth);

}
