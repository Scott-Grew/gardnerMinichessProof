#include "mailbox_board.h"

#include <cctype>
#include <sstream>
#include <utility>

namespace checker {

namespace {

constexpr int kBoardDimension = 5;

// Piece kind indices, shared across both colours, for indexing
// per-kind move generation and comparing piece types.
constexpr int kPawnKindIndex = 0;
constexpr int kKnightKindIndex = 1;
constexpr int kBishopKindIndex = 2;
constexpr int kRookKindIndex = 3;
constexpr int kQueenKindIndex = 4;
constexpr int kKingKindIndex = 5;

// File, rank offset pairs a knight can jump to from one square.
constexpr std::array<std::pair<int, int>, 8> kKnightStepOffsets = {{
    {1, 2},
    {2, 1},
    {2, -1},
    {1, -2},
    {-1, -2},
    {-2, -1},
    {-2, 1},
    {-1, 2},
}};

// File, rank offset pairs a king can step to from one square.
constexpr std::array<std::pair<int, int>, 8> kKingStepOffsets = {{
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
    {0, -1},
    {1, -1},
}};

// Ray directions for bishops and the diagonal half of a queen.
constexpr std::array<std::pair<int, int>, 4> kDiagonalDirections = {{
    {1, 1},
    {1, -1},
    {-1, 1},
    {-1, -1},
}};

// Ray directions for rooks and the orthogonal half of a queen.
constexpr std::array<std::pair<int, int>, 4> kOrthogonalDirections = {
    {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};

// True if a file, rank pair lands on the 5x5 board.
bool is_within_board(int file_index, int rank_index) {
  return file_index >= 0 && file_index < kBoardDimension &&
         rank_index >= 0 && rank_index < kBoardDimension;
}

// Square index for a file, rank pair: rank_index * 5 +
// file_index, so a1 is 0 and e5 is 24.
int square_from_file_rank(int file_index, int rank_index) {
  return rank_index * kBoardDimension + file_index;
}

// Inverse of square_from_file_rank: the file component.
int file_of_square(int square_index) {
  return square_index % kBoardDimension;
}

// Inverse of square_from_file_rank: the rank component.
int rank_of_square(int square_index) {
  return square_index / kBoardDimension;
}

// White pieces are codes 1 to 6, Black pieces 7 to 12.
Side side_of_piece(Piece piece) {
  return static_cast<int>(piece) <= 6 ? Side::White : Side::Black;
}

// Maps a coloured piece to its kind index, shared across
// colours, matching the kPawnKindIndex..kKingKindIndex order.
int piece_kind_index(Piece piece) {
  int piece_value = static_cast<int>(piece);
  if (piece_value >= 1 && piece_value <= 6) {
    return piece_value - 1;
  }
  return piece_value - 7;
}

// Inverse of piece_kind_index: builds the coloured piece code
// for a side and a kind index.
Piece colored_piece(Side side, int kind_index) {
  int base_value = side == Side::White ? 1 : 7;
  return static_cast<Piece>(base_value + kind_index);
}

// Decodes one FEN letter into a coloured piece: upper case is
// White, lower case Black; nullopt if it names no piece.
std::optional<Piece> piece_from_fen_character(char character) {
  Side piece_side =
      std::isupper(static_cast<unsigned char>(character))
          ? Side::White
          : Side::Black;
  char lowered_character = static_cast<char>(
      std::tolower(static_cast<unsigned char>(character)));
  switch (lowered_character) {
    case 'p':
      return colored_piece(piece_side, kPawnKindIndex);
    case 'n':
      return colored_piece(piece_side, kKnightKindIndex);
    case 'b':
      return colored_piece(piece_side, kBishopKindIndex);
    case 'r':
      return colored_piece(piece_side, kRookKindIndex);
    case 'q':
      return colored_piece(piece_side, kQueenKindIndex);
    case 'k':
      return colored_piece(piece_side, kKingKindIndex);
    default:
      return std::nullopt;
  }
}

// Walks outward from the target square; attacked if the first
// piece hit is attacking_side's queen or slider_kind_index.
template <std::size_t DirectionCount>
bool ray_is_attacked_from(
    const Board& board, int target_file, int target_rank,
    const std::array<std::pair<int, int>, DirectionCount>& directions,
    Side attacking_side, int slider_kind_index) {
  for (const auto& direction : directions) {
    int stepped_file = target_file + direction.first;
    int stepped_rank = target_rank + direction.second;
    while (is_within_board(stepped_file, stepped_rank)) {
      Piece stepped_piece = board.squares[square_from_file_rank(
          stepped_file, stepped_rank)];
      if (stepped_piece != Piece::Empty) {
        if (side_of_piece(stepped_piece) == attacking_side) {
          int stepped_kind_index = piece_kind_index(stepped_piece);
          if (stepped_kind_index == slider_kind_index ||
              stepped_kind_index == kQueenKindIndex) {
            return true;
          }
        }
        break;
      }
      stepped_file += direction.first;
      stepped_rank += direction.second;
    }
  }
  return false;
}

// Appends one pawn move, expanding it into four promotion moves
// when destination_rank is the far rank.
void append_pawn_destination(
    int origin_square, int destination_square, int destination_rank,
    int promotion_rank, Side mover_side,
    std::vector<BoardMove>& generated_moves) {
  if (destination_rank == promotion_rank) {
    generated_moves.push_back(
        {origin_square, destination_square,
         colored_piece(mover_side, kQueenKindIndex)});
    generated_moves.push_back(
        {origin_square, destination_square,
         colored_piece(mover_side, kRookKindIndex)});
    generated_moves.push_back(
        {origin_square, destination_square,
         colored_piece(mover_side, kBishopKindIndex)});
    generated_moves.push_back(
        {origin_square, destination_square,
         colored_piece(mover_side, kKnightKindIndex)});
    return;
  }
  generated_moves.push_back(
      {origin_square, destination_square, Piece::Empty});
}

// Adds this pawn's single-step forward and diagonal capture
// moves; no double step and no en passant are modelled.
void generate_pawn_moves(const Board& board, int origin_square,
                         int origin_file, int origin_rank,
                         Side mover_side,
                         std::vector<BoardMove>& generated_moves) {
  int forward_rank =
      mover_side == Side::White ? origin_rank + 1 : origin_rank - 1;
  int promotion_rank = mover_side == Side::White ? 4 : 0;

  if (is_within_board(origin_file, forward_rank)) {
    int forward_square =
        square_from_file_rank(origin_file, forward_rank);
    if (board.squares[forward_square] == Piece::Empty) {
      append_pawn_destination(origin_square, forward_square,
                              forward_rank, promotion_rank,
                              mover_side, generated_moves);
    }
  }

  for (int file_offset : {-1, 1}) {
    int capture_file = origin_file + file_offset;
    if (!is_within_board(capture_file, forward_rank)) {
      continue;
    }
    int capture_square =
        square_from_file_rank(capture_file, forward_rank);
    Piece captured_piece = board.squares[capture_square];
    if (captured_piece == Piece::Empty) {
      continue;
    }
    if (side_of_piece(captured_piece) == mover_side) {
      continue;
    }
    append_pawn_destination(origin_square, capture_square,
                            forward_rank, promotion_rank, mover_side,
                            generated_moves);
  }
}

// Adds this knight's jumps to empty or enemy-occupied squares.
void generate_knight_moves(const Board& board, int origin_square,
                           int origin_file, int origin_rank,
                           Side mover_side,
                           std::vector<BoardMove>& generated_moves) {
  for (const auto& offset : kKnightStepOffsets) {
    int destination_file = origin_file + offset.first;
    int destination_rank = origin_rank + offset.second;
    if (!is_within_board(destination_file, destination_rank)) {
      continue;
    }
    int destination_square =
        square_from_file_rank(destination_file, destination_rank);
    Piece destination_piece = board.squares[destination_square];
    if (destination_piece != Piece::Empty &&
        side_of_piece(destination_piece) == mover_side) {
      continue;
    }
    generated_moves.push_back(
        {origin_square, destination_square, Piece::Empty});
  }
}

// Adds this king's single steps; castling is not modelled.
void generate_king_moves(const Board& board, int origin_square,
                         int origin_file, int origin_rank,
                         Side mover_side,
                         std::vector<BoardMove>& generated_moves) {
  for (const auto& offset : kKingStepOffsets) {
    int destination_file = origin_file + offset.first;
    int destination_rank = origin_rank + offset.second;
    if (!is_within_board(destination_file, destination_rank)) {
      continue;
    }
    int destination_square =
        square_from_file_rank(destination_file, destination_rank);
    Piece destination_piece = board.squares[destination_square];
    if (destination_piece != Piece::Empty &&
        side_of_piece(destination_piece) == mover_side) {
      continue;
    }
    generated_moves.push_back(
        {origin_square, destination_square, Piece::Empty});
  }
}

// Adds moves along each direction until a piece or the edge is
// hit; an enemy piece there is a capture, a friendly one stops.
template <std::size_t DirectionCount>
void step_sliding_directions(
    const Board& board, int origin_square, int origin_file,
    int origin_rank, Side mover_side,
    const std::array<std::pair<int, int>, DirectionCount>& directions,
    std::vector<BoardMove>& generated_moves) {
  for (const auto& direction : directions) {
    int stepped_file = origin_file + direction.first;
    int stepped_rank = origin_rank + direction.second;
    while (is_within_board(stepped_file, stepped_rank)) {
      int stepped_square =
          square_from_file_rank(stepped_file, stepped_rank);
      Piece stepped_piece = board.squares[stepped_square];
      if (stepped_piece == Piece::Empty) {
        generated_moves.push_back(
            {origin_square, stepped_square, Piece::Empty});
      } else {
        if (side_of_piece(stepped_piece) != mover_side) {
          generated_moves.push_back(
              {origin_square, stepped_square, Piece::Empty});
        }
        break;
      }
      stepped_file += direction.first;
      stepped_rank += direction.second;
    }
  }
}

// Dispatches to the diagonal and/or orthogonal ray walk for a
// bishop, rook or queen, by piece_kind.
void generate_sliding_moves(const Board& board, int origin_square,
                            int origin_file, int origin_rank,
                            Side mover_side, int piece_kind,
                            std::vector<BoardMove>& generated_moves) {
  bool moves_diagonally =
      piece_kind == kBishopKindIndex || piece_kind == kQueenKindIndex;
  bool moves_orthogonally =
      piece_kind == kRookKindIndex || piece_kind == kQueenKindIndex;

  if (moves_diagonally) {
    step_sliding_directions(board, origin_square, origin_file,
                            origin_rank, mover_side,
                            kDiagonalDirections, generated_moves);
  }
  if (moves_orthogonally) {
    step_sliding_directions(board, origin_square, origin_file,
                            origin_rank, mover_side,
                            kOrthogonalDirections, generated_moves);
  }
}

}

// Parses a FEN board and side-to-move field into a Board.
// Rejects a malformed rank, wrong piece count, or bad letter.
std::optional<Board> board_from_fen(std::string_view fen_text) {
  std::istringstream fen_stream{std::string(fen_text)};
  std::string board_field;
  std::string side_field;
  if (!(fen_stream >> board_field)) {
    return std::nullopt;
  }
  if (!(fen_stream >> side_field)) {
    return std::nullopt;
  }

  Board board{};
  board.squares.fill(Piece::Empty);

  std::vector<std::string> rank_fields;
  std::string current_rank_field;
  for (char character : board_field) {
    if (character == '/') {
      rank_fields.push_back(current_rank_field);
      current_rank_field.clear();
      continue;
    }
    current_rank_field += character;
  }
  rank_fields.push_back(current_rank_field);
  if (rank_fields.size() != kBoardDimension) {
    return std::nullopt;
  }

  for (int rank_field_index = 0; rank_field_index < kBoardDimension;
       rank_field_index++) {
    int rank_index = kBoardDimension - 1 - rank_field_index;
    int file_index = 0;
    for (char character : rank_fields[rank_field_index]) {
      if (character >= '1' && character <= '5') {
        file_index += character - '0';
        if (file_index > kBoardDimension) {
          return std::nullopt;
        }
        continue;
      }
      if (file_index >= kBoardDimension) {
        return std::nullopt;
      }
      std::optional<Piece> parsed_piece =
          piece_from_fen_character(character);
      if (!parsed_piece.has_value()) {
        return std::nullopt;
      }
      board.squares[square_from_file_rank(file_index, rank_index)] =
          *parsed_piece;
      file_index++;
    }
    if (file_index != kBoardDimension) {
      return std::nullopt;
    }
  }

  if (side_field == "w") {
    board.side_to_move = Side::White;
  } else if (side_field == "b") {
    board.side_to_move = Side::Black;
  } else {
    return std::nullopt;
  }

  return board;
}

// Returns the board after making move; does not check legality,
// so the caller must have already filtered for check.
Board apply_move(const Board& board, const BoardMove& move) {
  Board result = board;
  Piece moving_piece = board.squares[move.from_square];
  result.squares[move.from_square] = Piece::Empty;
  if (move.promotion_piece != Piece::Empty) {
    result.squares[move.to_square] = move.promotion_piece;
  } else {
    result.squares[move.to_square] = moving_piece;
  }
  result.side_to_move =
      board.side_to_move == Side::White ? Side::Black : Side::White;
  return result;
}

// True if any piece of attacking_side attacks square_index,
// checked by stepping backward from the square per piece kind.
bool is_square_attacked_by(const Board& board, int square_index,
                           Side attacking_side) {
  int target_file = file_of_square(square_index);
  int target_rank = rank_of_square(square_index);

  for (const auto& offset : kKnightStepOffsets) {
    int origin_file = target_file + offset.first;
    int origin_rank = target_rank + offset.second;
    if (!is_within_board(origin_file, origin_rank)) {
      continue;
    }
    Piece origin_piece =
        board
            .squares[square_from_file_rank(origin_file, origin_rank)];
    if (origin_piece == Piece::Empty) {
      continue;
    }
    if (side_of_piece(origin_piece) != attacking_side) {
      continue;
    }
    if (piece_kind_index(origin_piece) == kKnightKindIndex) {
      return true;
    }
  }

  for (const auto& offset : kKingStepOffsets) {
    int origin_file = target_file + offset.first;
    int origin_rank = target_rank + offset.second;
    if (!is_within_board(origin_file, origin_rank)) {
      continue;
    }
    Piece origin_piece =
        board
            .squares[square_from_file_rank(origin_file, origin_rank)];
    if (origin_piece == Piece::Empty) {
      continue;
    }
    if (side_of_piece(origin_piece) != attacking_side) {
      continue;
    }
    if (piece_kind_index(origin_piece) == kKingKindIndex) {
      return true;
    }
  }

  int pawn_origin_rank = attacking_side == Side::White
                             ? target_rank - 1
                             : target_rank + 1;
  for (int file_offset : {-1, 1}) {
    int origin_file = target_file + file_offset;
    if (!is_within_board(origin_file, pawn_origin_rank)) {
      continue;
    }
    Piece origin_piece = board.squares[square_from_file_rank(
        origin_file, pawn_origin_rank)];
    if (origin_piece == Piece::Empty) {
      continue;
    }
    if (side_of_piece(origin_piece) != attacking_side) {
      continue;
    }
    if (piece_kind_index(origin_piece) == kPawnKindIndex) {
      return true;
    }
  }

  if (ray_is_attacked_from(board, target_file, target_rank,
                           kDiagonalDirections, attacking_side,
                           kBishopKindIndex)) {
    return true;
  }
  if (ray_is_attacked_from(board, target_file, target_rank,
                           kOrthogonalDirections, attacking_side,
                           kRookKindIndex)) {
    return true;
  }

  return false;
}

// True if king_side's king is attacked. False if that side has
// no king on the board, which the certificate format forbids.
bool is_king_attacked(const Board& board, Side king_side) {
  Piece king_piece = colored_piece(king_side, kKingKindIndex);
  Side opposing_side =
      king_side == Side::White ? Side::Black : Side::White;
  for (int square_index = 0; square_index < 25; square_index++) {
    if (board.squares[square_index] == king_piece) {
      return is_square_attacked_by(board, square_index,
                                   opposing_side);
    }
  }
  return false;
}

// Generates pseudo-legal moves for the side to move, then keeps
// only those that do not leave that side's own king attacked.
std::vector<BoardMove> legal_moves(const Board& board) {
  Side mover_side = board.side_to_move;
  std::vector<BoardMove> generated_moves;

  for (int origin_square = 0; origin_square < 25; origin_square++) {
    Piece origin_piece = board.squares[origin_square];
    if (origin_piece == Piece::Empty) {
      continue;
    }
    if (side_of_piece(origin_piece) != mover_side) {
      continue;
    }

    int origin_file = file_of_square(origin_square);
    int origin_rank = rank_of_square(origin_square);
    int kind_index = piece_kind_index(origin_piece);

    if (kind_index == kPawnKindIndex) {
      generate_pawn_moves(board, origin_square, origin_file,
                          origin_rank, mover_side, generated_moves);
    } else if (kind_index == kKnightKindIndex) {
      generate_knight_moves(board, origin_square, origin_file,
                            origin_rank, mover_side, generated_moves);
    } else if (kind_index == kKingKindIndex) {
      generate_king_moves(board, origin_square, origin_file,
                          origin_rank, mover_side, generated_moves);
    } else {
      generate_sliding_moves(board, origin_square, origin_file,
                             origin_rank, mover_side, kind_index,
                             generated_moves);
    }
  }

  std::vector<BoardMove> legal_result;
  for (const BoardMove& candidate_move : generated_moves) {
    Board resulting_board = apply_move(board, candidate_move);
    if (!is_king_attacked(resulting_board, mover_side)) {
      legal_result.push_back(candidate_move);
    }
  }
  return legal_result;
}

// Counts leaf positions depth plies below board, by full legal
// move generation at every ply (a perft count).
std::uint64_t count_leaf_positions(const Board& board, int depth) {
  if (depth == 0) {
    return 1;
  }
  std::uint64_t total_leaf_positions = 0;
  for (const BoardMove& move : legal_moves(board)) {
    total_leaf_positions +=
        count_leaf_positions(apply_move(board, move), depth - 1);
  }
  return total_leaf_positions;
}

// Formats a move as coordinate text, such as "b2b4" or "a4a5q"
// for a queen promotion.
std::string move_text(const BoardMove& move) {
  std::string text;
  text += static_cast<char>('a' + file_of_square(move.from_square));
  text += static_cast<char>('1' + rank_of_square(move.from_square));
  text += static_cast<char>('a' + file_of_square(move.to_square));
  text += static_cast<char>('1' + rank_of_square(move.to_square));
  if (move.promotion_piece != Piece::Empty) {
    switch (piece_kind_index(move.promotion_piece)) {
      case kQueenKindIndex:
        text += 'q';
        break;
      case kRookKindIndex:
        text += 'r';
        break;
      case kBishopKindIndex:
        text += 'b';
        break;
      case kKnightKindIndex:
        text += 'n';
        break;
      default:
        break;
    }
  }
  return text;
}

}
