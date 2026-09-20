#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "certificate_check.h"
#include "mailbox_board.h"

namespace {

// Prints the command-line usage summary to standard error.
void print_usage() {
  std::cerr << "usage: checker <certificate_path> --defender "
               "<white|black> [--fen <fen>] [--moves \"<move> "
               "<move> ...\"]"
            << std::endl;
}

// Parses "white" or "black"; nullopt for anything else.
std::optional<checker::Side> side_from_text(const std::string& text) {
  if (text == "white") {
    return checker::Side::White;
  }
  if (text == "black") {
    return checker::Side::Black;
  }
  return std::nullopt;
}

// Splits a whitespace-separated move list into individual
// coordinate-move tokens, such as "b2b4".
std::vector<std::string> split_move_tokens(const std::string& text) {
  std::vector<std::string> tokens;
  std::istringstream token_stream(text);
  std::string token;
  while (token_stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

}

// Replays --moves from --fen (default the Gardner start) to get
// the real root, then prints ACCEPT or REJECT for the certificate.
int main(int argument_count, char** argument_values) {
  if (argument_count < 2) {
    print_usage();
    return 2;
  }

  std::string certificate_path = argument_values[1];
  std::optional<checker::Side> defender_side;
  std::string root_fen = "rnbqk/ppppp/5/PPPPP/RNBQK w - - 0 1";
  std::string moves_argument;
  bool moves_argument_given = false;

  int argument_index = 2;
  while (argument_index < argument_count) {
    std::string flag_name = argument_values[argument_index];
    if (flag_name == "--defender" &&
        argument_index + 1 < argument_count) {
      defender_side =
          side_from_text(argument_values[argument_index + 1]);
      if (!defender_side.has_value()) {
        print_usage();
        return 2;
      }
      argument_index += 2;
      continue;
    }
    if (flag_name == "--fen" && argument_index + 1 < argument_count) {
      root_fen = argument_values[argument_index + 1];
      argument_index += 2;
      continue;
    }
    if (flag_name == "--moves" &&
        argument_index + 1 < argument_count) {
      moves_argument = argument_values[argument_index + 1];
      moves_argument_given = true;
      argument_index += 2;
      continue;
    }
    print_usage();
    return 2;
  }

  if (!defender_side.has_value()) {
    print_usage();
    return 2;
  }

  std::optional<checker::Board> parsed_root_board =
      checker::board_from_fen(root_fen);
  if (!parsed_root_board.has_value()) {
    std::cerr << "unparseable fen: " << root_fen << std::endl;
    return 2;
  }

  checker::Board current_board = *parsed_root_board;
  for (const std::string& move_token :
       split_move_tokens(moves_argument)) {
    bool move_was_applied = false;
    for (const checker::BoardMove& candidate_move :
         checker::legal_moves(current_board)) {
      if (checker::move_text(candidate_move) == move_token) {
        current_board =
            checker::apply_move(current_board, candidate_move);
        move_was_applied = true;
        break;
      }
    }
    if (!move_was_applied) {
      std::cerr << "unknown move: " << move_token << std::endl;
      return 2;
    }
  }

  std::string failure_reason;
  std::optional<std::vector<checker::CertificateEntry>>
      certificate_entries =
          checker::read_certificate(certificate_path, failure_reason);
  if (!certificate_entries.has_value()) {
    if (failure_reason == "cannot open file") {
      std::cerr << "cannot open file: " << certificate_path
                << std::endl;
      return 2;
    }
    std::cout << "REJECT " << failure_reason << std::endl;
    return 1;
  }

  checker::CheckOutcome outcome = checker::check_certificate(
      *certificate_entries, current_board, *defender_side);

  if (!outcome.accepted) {
    std::cout << "REJECT " << outcome.reason << std::endl;
    return 1;
  }

  std::string defender_text =
      *defender_side == checker::Side::White ? "white" : "black";
  std::cout << "ACCEPT positions=" << outcome.position_count
            << " defender=" << defender_text << " root=" << root_fen
            << " moves="
            << (moves_argument_given ? moves_argument : "-")
            << std::endl;
  return 0;
}
