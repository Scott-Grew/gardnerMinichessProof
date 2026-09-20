#include "board.h"

#include <cassert>
#include <cctype>
#include <utility>

namespace gardner {

namespace {

// FEN letter for a piece: lowercase for Black, uppercase for
// White; '?' if piece_type is None.
char fen_letter(Color color, PieceType piece_type) {
  char letter = '?';
  switch (piece_type) {
    case PieceType::Pawn:
      letter = 'p';
      break;
    case PieceType::Knight:
      letter = 'n';
      break;
    case PieceType::Bishop:
      letter = 'b';
      break;
    case PieceType::Rook:
      letter = 'r';
      break;
    case PieceType::Queen:
      letter = 'q';
      break;
    case PieceType::King:
      letter = 'k';
      break;
    case PieceType::None:
      return '?';
  }
  if (color == Color::White) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(letter)));
  }
  return letter;
}

// Parses one FEN board-field letter into its color and piece
// type; case gives the color.
std::optional<std::pair<Color, PieceType>> piece_from_fen_letter(
    char letter) {
  const Color color = std::isupper(static_cast<unsigned char>(letter))
                          ? Color::White
                          : Color::Black;
  switch (std::tolower(static_cast<unsigned char>(letter))) {
    case 'p':
      return std::pair{color, PieceType::Pawn};
    case 'n':
      return std::pair{color, PieceType::Knight};
    case 'b':
      return std::pair{color, PieceType::Bishop};
    case 'r':
      return std::pair{color, PieceType::Rook};
    case 'q':
      return std::pair{color, PieceType::Queen};
    case 'k':
      return std::pair{color, PieceType::King};
    default:
      return std::nullopt;
  }
}

// UCI promotion letter for a promotion piece type; asserts on
// non-promotable types.
char promotion_letter(PieceType piece_type) {
  switch (piece_type) {
    case PieceType::Queen:
      return 'q';
    case PieceType::Rook:
      return 'r';
    case PieceType::Bishop:
      return 'b';
    case PieceType::Knight:
      return 'n';
    default:
      assert(false);
      return '?';
  }
}

// File letter ('a'-'e') of a square index.
char square_file_letter(int square_index) {
  return static_cast<char>('a' + file_of(square_index));
}

// Rank letter ('1'-'5') of a square index.
char square_rank_letter(int square_index) {
  return static_cast<char>('1' + rank_of(square_index));
}

}

// The color on the other side.
Color opposite_color(Color color) {
  if (color == Color::White) return Color::Black;
  return Color::White;
}

// All occupied squares, White or Black pieces combined.
std::uint32_t occupancy(const Position& position) {
  return position.pieces_by_color[static_cast<int>(Color::White)] |
         position.pieces_by_color[static_cast<int>(Color::Black)];
}

// The piece type on a square, or None if it is empty.
PieceType piece_type_on(const Position& position, int square_index) {
  const std::uint32_t square_bit = std::uint32_t{1} << square_index;
  for (int piece_type_index = 0; piece_type_index < 6;
       ++piece_type_index) {
    if (position.pieces_by_type[piece_type_index] & square_bit) {
      return static_cast<PieceType>(piece_type_index);
    }
  }
  return PieceType::None;
}

// Applies a move to a position, removing any captured piece,
// moving or promoting the mover, and flipping side to move.
Position make_move(const Position& position, Move move) {
  Position result = position;
  const std::uint32_t from_bit = std::uint32_t{1} << move.from_square;
  const std::uint32_t to_bit = std::uint32_t{1} << move.to_square;
  const Color mover_color = position.side_to_move;
  const Color defender_color = opposite_color(mover_color);
  const PieceType moved_piece_type =
      piece_type_on(position, move.from_square);
  const PieceType captured_piece_type =
      piece_type_on(position, move.to_square);

  if (captured_piece_type != PieceType::None) {
    result.pieces_by_type[static_cast<int>(captured_piece_type)] &=
        ~to_bit;
    result.pieces_by_color[static_cast<int>(defender_color)] &=
        ~to_bit;
  }

  result.pieces_by_type[static_cast<int>(moved_piece_type)] &=
      ~from_bit;
  result.pieces_by_color[static_cast<int>(mover_color)] &= ~from_bit;

  const PieceType placed_piece_type =
      move.promotion == PieceType::None ? moved_piece_type
                                        : move.promotion;
  result.pieces_by_type[static_cast<int>(placed_piece_type)] |=
      to_bit;
  result.pieces_by_color[static_cast<int>(mover_color)] |= to_bit;

  result.side_to_move = defender_color;
  return result;
}

// Parses a FEN board and side-to-move field into a Position;
// returns nullopt on any malformed input.
std::optional<Position> position_from_fen(std::string_view fen) {
  const std::size_t board_field_end = fen.find(' ');
  if (board_field_end == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view board_field = fen.substr(0, board_field_end);

  std::size_t side_field_start = board_field_end + 1;
  while (side_field_start < fen.size() &&
         fen[side_field_start] == ' ') {
    ++side_field_start;
  }
  if (side_field_start >= fen.size()) return std::nullopt;

  Color side_to_move;
  const char side_character = fen[side_field_start];
  if (side_character == 'w') {
    side_to_move = Color::White;
  } else if (side_character == 'b') {
    side_to_move = Color::Black;
  } else {
    return std::nullopt;
  }

  Position position{};
  position.side_to_move = side_to_move;

  int rank_index = kBoardRanks - 1;
  std::size_t rank_start = 0;
  int ranks_parsed = 0;
  // Walk each '/'-separated rank from rank 5 down to rank 1.
  while (rank_start <= board_field.size()) {
    const std::size_t slash_position =
        board_field.find('/', rank_start);
    const std::string_view rank_field = board_field.substr(
        rank_start, slash_position == std::string_view::npos
                        ? std::string_view::npos
                        : slash_position - rank_start);
    if (rank_index < 0) return std::nullopt;

    int file_index = 0;
    for (const char rank_character : rank_field) {
      if (rank_character >= '1' && rank_character <= '9') {
        file_index += rank_character - '0';
        continue;
      }
      const std::optional<std::pair<Color, PieceType>> piece =
          piece_from_fen_letter(rank_character);
      if (!piece.has_value() || !is_valid_file(file_index)) {
        return std::nullopt;
      }
      const int square_index = square_of(file_index, rank_index);
      const std::uint32_t square_bit = std::uint32_t{1}
                                       << square_index;
      position.pieces_by_type[static_cast<int>(piece->second)] |=
          square_bit;
      position.pieces_by_color[static_cast<int>(piece->first)] |=
          square_bit;
      ++file_index;
    }
    if (file_index != kBoardFiles) return std::nullopt;

    ++ranks_parsed;
    --rank_index;
    if (slash_position == std::string_view::npos) break;
    rank_start = slash_position + 1;
  }
  if (ranks_parsed != kBoardRanks) return std::nullopt;

  return position;
}

// Serializes a position to a FEN board field plus side to
// move, run-length-encoding empty squares.
std::string fen_from_position(const Position& position) {
  std::string board_field;
  for (int rank_index = kBoardRanks - 1; rank_index >= 0;
       --rank_index) {
    int empty_run = 0;
    for (int file_index = 0; file_index < kBoardFiles; ++file_index) {
      const int square_index = square_of(file_index, rank_index);
      const PieceType piece_type =
          piece_type_on(position, square_index);
      if (piece_type == PieceType::None) {
        ++empty_run;
        continue;
      }
      if (empty_run > 0) {
        board_field += std::to_string(empty_run);
        empty_run = 0;
      }
      const Color piece_color =
          (position.pieces_by_color[static_cast<int>(Color::White)] &
           (std::uint32_t{1} << square_index)) != 0
              ? Color::White
              : Color::Black;
      board_field += fen_letter(piece_color, piece_type);
    }
    if (empty_run > 0) {
      board_field += std::to_string(empty_run);
    }
    if (rank_index > 0) board_field += '/';
  }
  board_field += ' ';
  board_field += position.side_to_move == Color::White ? 'w' : 'b';
  board_field += " - - 0 1";
  return board_field;
}

// UCI text for a move: from-square, to-square, and an
// optional promotion letter.
std::string uci_from_move(Move move) {
  std::string result;
  result += square_file_letter(move.from_square);
  result += square_rank_letter(move.from_square);
  result += square_file_letter(move.to_square);
  result += square_rank_letter(move.to_square);
  if (move.promotion != PieceType::None) {
    result += promotion_letter(move.promotion);
  }
  return result;
}

// Builds the PositionKey: one 4-bit code per square (0 empty,
// else piece type + 1, +6 for Black) plus a side bit.
PositionKey exact_key(const Position& position) {
  std::uint64_t low = 0;
  std::uint64_t high = 0;
  for (int square_index = 0; square_index < kSquareCount;
       ++square_index) {
    const PieceType piece_type =
        piece_type_on(position, square_index);
    std::uint64_t nibble_value = 0;
    if (piece_type != PieceType::None) {
      const std::uint32_t square_bit = std::uint32_t{1}
                                       << square_index;
      const Color piece_color =
          (position.pieces_by_color[static_cast<int>(Color::White)] &
           square_bit) != 0
              ? Color::White
              : Color::Black;
      const std::uint64_t base_value =
          static_cast<std::uint64_t>(piece_type) + 1;
      nibble_value =
          piece_color == Color::White ? base_value : base_value + 6;
    }
    // Squares 0-15 pack into low, 16-24 into high.
    if (square_index < 16) {
      low |= nibble_value << (square_index * 4);
    } else {
      high |= nibble_value << ((square_index - 16) * 4);
    }
  }
  if (position.side_to_move == Color::Black) {
    high |= std::uint64_t{1} << 36;
  }
  return PositionKey{low, high};
}

}
