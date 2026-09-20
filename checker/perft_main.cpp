#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "mailbox_board.h"

namespace {

// Parses a non-negative integer depth; nullopt if text is empty
// or has any non-digit character.
std::optional<int> parse_depth_argument(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }
  for (char character : text) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
  }
  return std::stoi(text);
}

// Prints the command-line usage summary to standard error.
void print_usage() {
  std::cerr << "usage: checker_perft <depth> <fen>" << std::endl;
}

}

// Prints each root move's leaf count at depth and the total, an
// independent perft check against the solver's move generator.
int main(int argument_count, char** argument_values) {
  if (argument_count != 3) {
    print_usage();
    return 2;
  }

  std::optional<int> parsed_depth =
      parse_depth_argument(argument_values[1]);
  if (!parsed_depth.has_value()) {
    print_usage();
    return 2;
  }

  std::optional<checker::Board> parsed_board =
      checker::board_from_fen(argument_values[2]);
  if (!parsed_board.has_value()) {
    std::cerr << "unparseable fen: " << argument_values[2]
              << std::endl;
    return 2;
  }

  const checker::Board& root_board = *parsed_board;
  int search_depth = *parsed_depth;

  std::uint64_t total_nodes_searched = 0;
  for (const checker::BoardMove& root_move :
       checker::legal_moves(root_board)) {
    checker::Board child_board =
        checker::apply_move(root_board, root_move);
    std::uint64_t child_node_count =
        checker::count_leaf_positions(child_board, search_depth - 1);
    std::cout << checker::move_text(root_move) << ": "
              << child_node_count << std::endl;
    total_nodes_searched += child_node_count;
  }

  std::cout << std::endl;
  std::cout << "Nodes searched: " << total_nodes_searched
            << std::endl;
  return 0;
}
