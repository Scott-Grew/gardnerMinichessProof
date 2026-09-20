#include "movegen.h"

#include <bit>

#include "attack_tables.h"

namespace gardner {

namespace {

// Appends one pawn move to move_list, expanding into all
// four promotion pieces when is_promotion is true.
void add_pawn_destination(int from_square, int to_square,
                          bool is_promotion, MoveList& move_list) {
  if (!is_promotion) {
    move_list.push_back(Move{static_cast<std::uint8_t>(from_square),
                             static_cast<std::uint8_t>(to_square),
                             PieceType::None});
    return;
  }
  for (const PieceType promotion_piece_type :
       {PieceType::Queen, PieceType::Rook, PieceType::Bishop,
        PieceType::Knight}) {
    move_list.push_back(Move{static_cast<std::uint8_t>(from_square),
                             static_cast<std::uint8_t>(to_square),
                             promotion_piece_type});
  }
}

// Appends this pawn's pushes and captures; White pushes
// toward higher ranks, Black toward lower.
void generate_pawn_moves(const Position& position, int square_index,
                         MoveList& move_list) {
  const Color side_to_move = position.side_to_move;
  const int origin_file = file_of(square_index);
  const int origin_rank = rank_of(square_index);
  const int forward_rank_step = side_to_move == Color::White ? 1 : -1;
  const int promotion_rank =
      side_to_move == Color::White ? kBoardRanks - 1 : 0;
  const std::uint32_t board_occupancy = occupancy(position);
  const std::uint32_t opponent_pieces =
      position.pieces_by_color[static_cast<int>(
          opposite_color(side_to_move))];

  const int forward_rank = origin_rank + forward_rank_step;
  if (!is_valid_rank(forward_rank)) return;

  const int forward_square = square_of(origin_file, forward_rank);
  const bool forward_is_promotion = forward_rank == promotion_rank;
  if (!(board_occupancy & (std::uint32_t{1} << forward_square))) {
    add_pawn_destination(square_index, forward_square,
                         forward_is_promotion, move_list);
  }

  for (const int file_step : {-1, 1}) {
    const int capture_file = origin_file + file_step;
    if (!is_valid_file(capture_file)) continue;
    const int capture_square = square_of(capture_file, forward_rank);
    if (opponent_pieces & (std::uint32_t{1} << capture_square)) {
      add_pawn_destination(square_index, capture_square,
                           forward_is_promotion, move_list);
    }
  }
}

// Turns an attack bitboard into moves, popping each
// destination bit and skipping ones occupied by own pieces.
void generate_moves_from_attack_bitboard(
    const Position& position, int square_index,
    std::uint32_t attack_bitboard, MoveList& move_list) {
  const std::uint32_t own_pieces =
      position
          .pieces_by_color[static_cast<int>(position.side_to_move)];
  std::uint32_t destinations = attack_bitboard & ~own_pieces;
  while (destinations != 0) {
    const int destination_square = std::countr_zero(destinations);
    move_list.push_back(
        Move{static_cast<std::uint8_t>(square_index),
             static_cast<std::uint8_t>(destination_square),
             PieceType::None});
    // Clears the lowest set destination bit each iteration.
    destinations &= destinations - 1;
  }
}

}

// True if attacking_color attacks square_index, checked
// piece type by piece type (knight, king, pawn, then sliders).
bool is_square_attacked(const Position& position, int square_index,
                        Color attacking_color) {
  const int attacking_color_index = static_cast<int>(attacking_color);
  const std::uint32_t attacker_pieces =
      position.pieces_by_color[attacking_color_index];

  const std::uint32_t knights =
      attacker_pieces &
      position.pieces_by_type[static_cast<int>(PieceType::Knight)];
  if (knight_attacks(square_index) & knights) return true;

  const std::uint32_t kings =
      attacker_pieces &
      position.pieces_by_type[static_cast<int>(PieceType::King)];
  if (king_attacks(square_index) & kings) return true;

  const std::uint32_t pawns =
      attacker_pieces &
      position.pieces_by_type[static_cast<int>(PieceType::Pawn)];
  if (pawn_attacks(opposite_color(attacking_color), square_index) &
      pawns) {
    return true;
  }

  const std::uint32_t board_occupancy = occupancy(position);

  const std::uint32_t bishops_and_queens =
      attacker_pieces &
      (position.pieces_by_type[static_cast<int>(PieceType::Bishop)] |
       position.pieces_by_type[static_cast<int>(PieceType::Queen)]);
  if (bishop_attacks(square_index, board_occupancy) &
      bishops_and_queens) {
    return true;
  }

  const std::uint32_t rooks_and_queens =
      attacker_pieces &
      (position.pieces_by_type[static_cast<int>(PieceType::Rook)] |
       position.pieces_by_type[static_cast<int>(PieceType::Queen)]);
  if (rook_attacks(square_index, board_occupancy) &
      rooks_and_queens) {
    return true;
  }

  return false;
}

// True if king_color's king is attacked by the other side.
bool is_in_check(const Position& position, Color king_color) {
  const std::uint32_t king_bitboard =
      position.pieces_by_color[static_cast<int>(king_color)] &
      position.pieces_by_type[static_cast<int>(PieceType::King)];
  const int king_square = std::countr_zero(king_bitboard);
  return is_square_attacked(position, king_square,
                            opposite_color(king_color));
}

// Legal moves for the side to move: pseudo-legal moves that
// do not leave that side's own king in check.
MoveList generate_legal_moves(const Position& position) {
  MoveList pseudo_legal_moves;
  const Color side_to_move = position.side_to_move;
  const std::uint32_t own_pieces =
      position.pieces_by_color[static_cast<int>(side_to_move)];
  const std::uint32_t board_occupancy = occupancy(position);

  for (int square_index = 0; square_index < kSquareCount;
       ++square_index) {
    if (!(own_pieces & (std::uint32_t{1} << square_index))) {
      continue;
    }
    switch (piece_type_on(position, square_index)) {
      case PieceType::Pawn:
        generate_pawn_moves(position, square_index,
                            pseudo_legal_moves);
        break;
      case PieceType::Knight:
        generate_moves_from_attack_bitboard(
            position, square_index, knight_attacks(square_index),
            pseudo_legal_moves);
        break;
      case PieceType::King:
        generate_moves_from_attack_bitboard(
            position, square_index, king_attacks(square_index),
            pseudo_legal_moves);
        break;
      case PieceType::Bishop:
        generate_moves_from_attack_bitboard(
            position, square_index,
            bishop_attacks(square_index, board_occupancy),
            pseudo_legal_moves);
        break;
      case PieceType::Rook:
        generate_moves_from_attack_bitboard(
            position, square_index,
            rook_attacks(square_index, board_occupancy),
            pseudo_legal_moves);
        break;
      case PieceType::Queen:
        generate_moves_from_attack_bitboard(
            position, square_index,
            queen_attacks(square_index, board_occupancy),
            pseudo_legal_moves);
        break;
      case PieceType::None:
        break;
    }
  }

  MoveList legal_moves;
  for (const Move move : pseudo_legal_moves) {
    const Position resulting_position = make_move(position, move);
    if (!is_in_check(resulting_position, side_to_move)) {
      legal_moves.push_back(move);
    }
  }
  return legal_moves;
}

// Recursively counts leaf positions reached from position in
// exactly depth plies.
std::uint64_t perft(const Position& position, int depth) {
  if (depth <= 0) return 1;
  const MoveList legal_moves = generate_legal_moves(position);
  std::uint64_t node_count = 0;
  for (const Move move : legal_moves) {
    node_count += perft(make_move(position, move), depth - 1);
  }
  return node_count;
}

}
