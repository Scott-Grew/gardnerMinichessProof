#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "board.h"
#include "canonical.h"
#include "movegen.h"

namespace {

struct TestCase {
  int depth;
  std::string_view fen;
};

constexpr std::array<TestCase, 5> kTestCases{{
    {5, "rnbqk/ppppp/5/PPPPP/RNBQK w - - 0 1"},
    {4, "1n2k/P4/5/5/4K w - - 0 1"},
    {4, "4k/5/5/p4/1N2K b - - 0 1"},
    {4, "r3k/1pq2/2P2/1B1Q1/R3K w - - 0 1"},
    {4, "4k/5/5/3q1/4K w - - 0 1"},
}};

bool report_failure(std::string_view check_name,
                    std::string_view fen) {
  std::cerr << "FAILED " << check_name << " fen=" << fen << '\n';
  return false;
}

std::vector<gardner::PositionKey> sorted_successor_keys(
    const gardner::Position& base_position,
    const gardner::MoveList& moves) {
  std::vector<gardner::PositionKey> keys;
  keys.reserve(static_cast<std::size_t>(moves.size()));
  for (const gardner::Move move : moves) {
    keys.push_back(
        gardner::exact_key(gardner::make_move(base_position, move)));
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

bool check_move_mapping_equivalence(
    const gardner::Position& position, std::string_view fen,
    std::string_view check_name,
    gardner::Position (*transform_position)(const gardner::Position&),
    gardner::Move (*transform_move)(gardner::Move)) {
  const gardner::Position transformed_position =
      transform_position(position);
  const gardner::MoveList original_moves =
      gardner::generate_legal_moves(position);

  std::vector<gardner::PositionKey> keys_mapped_through_transform;
  keys_mapped_through_transform.reserve(
      static_cast<std::size_t>(original_moves.size()));
  for (const gardner::Move move : original_moves) {
    const gardner::Move transformed_move = transform_move(move);
    keys_mapped_through_transform.push_back(gardner::exact_key(
        gardner::make_move(transformed_position, transformed_move)));
  }
  std::sort(keys_mapped_through_transform.begin(),
            keys_mapped_through_transform.end());

  const gardner::MoveList transformed_position_moves =
      gardner::generate_legal_moves(transformed_position);
  const std::vector<gardner::PositionKey>
      keys_from_transformed_position_moves = sorted_successor_keys(
          transformed_position, transformed_position_moves);

  if (keys_mapped_through_transform !=
      keys_from_transformed_position_moves) {
    return report_failure(check_name, fen);
  }
  return true;
}

bool run_checks_for_test_case(const TestCase& test_case) {
  const std::optional<gardner::Position> parsed_position =
      gardner::position_from_fen(test_case.fen);
  if (!parsed_position.has_value()) {
    return report_failure("position_from_fen", test_case.fen);
  }
  const gardner::Position position = *parsed_position;

  if (gardner::mirror_files(gardner::mirror_files(position)) !=
      position) {
    return report_failure("mirror_files_involution", test_case.fen);
  }
  if (gardner::swap_colors(gardner::swap_colors(position)) !=
      position) {
    return report_failure("swap_colors_involution", test_case.fen);
  }

  const std::uint64_t base_node_count =
      gardner::perft(position, test_case.depth);
  if (base_node_count !=
      gardner::perft(gardner::mirror_files(position),
                     test_case.depth)) {
    return report_failure("perft_mirror_files", test_case.fen);
  }
  if (base_node_count !=
      gardner::perft(gardner::swap_colors(position),
                     test_case.depth)) {
    return report_failure("perft_swap_colors", test_case.fen);
  }

  if (!check_move_mapping_equivalence(
          position, test_case.fen, "move_mapping_mirror_files",
          gardner::mirror_files, gardner::mirror_files_move)) {
    return false;
  }
  if (!check_move_mapping_equivalence(
          position, test_case.fen, "move_mapping_swap_colors",
          gardner::swap_colors, gardner::swap_colors_move)) {
    return false;
  }

  const gardner::Position canonical_from_position =
      gardner::canonical_under_mirror(position).position;
  const gardner::Position canonical_from_mirrored =
      gardner::canonical_under_mirror(gardner::mirror_files(position))
          .position;
  if (canonical_from_position != canonical_from_mirrored) {
    return report_failure("canonical_under_mirror_agreement",
                          test_case.fen);
  }
  return true;
}

bool check_start_position_swap_colors_structure() {
  constexpr std::string_view kStartFen =
      "rnbqk/ppppp/5/PPPPP/RNBQK w - - 0 1";
  const std::optional<gardner::Position> start_position =
      gardner::position_from_fen(kStartFen);
  if (!start_position.has_value()) {
    return report_failure("position_from_fen", kStartFen);
  }
  const std::string swapped_fen = gardner::fen_from_position(
      gardner::swap_colors(*start_position));
  constexpr std::string_view kExpectedPrefix =
      "rnbqk/ppppp/5/PPPPP/RNBQK b";
  if (swapped_fen.compare(0, kExpectedPrefix.size(),
                          kExpectedPrefix) != 0) {
    return report_failure("start_swap_colors_structure", kStartFen);
  }
  return true;
}

}

int main() {
  for (const TestCase& test_case : kTestCases) {
    if (!run_checks_for_test_case(test_case)) {
      return 1;
    }
  }
  if (!check_start_position_swap_colors_structure()) {
    return 1;
  }
  return 0;
}
