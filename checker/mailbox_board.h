#ifndef GARDNER_PROOF_CHECKER_MAILBOX_BOARD_H_
#define GARDNER_PROOF_CHECKER_MAILBOX_BOARD_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace checker {

// Colour to move or to act. Not tied to attacker or defender
// role; that mapping is applied by the caller.
enum class Side : std::uint8_t { White, Black };

// Piece codes packed as 4-bit nibbles: Empty is 0, White pieces
// are 1 to 6, Black pieces 7 to 12, same kind order for both.
enum class Piece : std::int8_t {
  Empty = 0,
  WhitePawn = 1,
  WhiteKnight,
  WhiteBishop,
  WhiteRook,
  WhiteQueen,
  WhiteKing,
  BlackPawn = 7,
  BlackKnight,
  BlackBishop,
  BlackRook,
  BlackQueen,
  BlackKing
};

// A full 5x5 position. Square index is rank_index * 5 +
// file_index, with a1 = 0 and e5 = 24.
struct Board {
  std::array<Piece, 25> squares;
  Side side_to_move;
};

// One move in mailbox square-index form. promotion_piece is
// Piece::Empty unless the move promotes a pawn.
struct BoardMove {
  int from_square;
  int to_square;
  Piece promotion_piece;
};

std::optional<Board> board_from_fen(std::string_view fen_text);

Board apply_move(const Board& board, const BoardMove& move);

bool is_square_attacked_by(const Board& board, int square_index,
                           Side attacking_side);

bool is_king_attacked(const Board& board, Side king_side);

std::vector<BoardMove> legal_moves(const Board& board);

std::uint64_t count_leaf_positions(const Board& board, int depth);

std::string move_text(const BoardMove& move);

}

#endif

// Index of each square in Board::squares, from White's side.
// index = rank_index * 5 + file_index.
//
//   rank 5 | 20 21 22 23 24
//   rank 4 | 15 16 17 18 19
//   rank 3 | 10 11 12 13 14
//   rank 2 |  5  6  7  8  9
//   rank 1 |  0  1  2  3  4
//          +---------------
//             a  b  c  d  e
