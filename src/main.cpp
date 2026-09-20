#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "attack_tables.h"
#include "board.h"
#include "canonical.h"
#include "certificate.h"
#include "dfpn.h"
#include "movegen.h"
#include "shared_table.h"

namespace {

// CLI usage text and the Gardner 5x5 chess starting position.
constexpr char kUsageMessage[] =
    "usage: gardner-proof perft <depth> [fen]\n"
    "       gardner-proof tables\n"
    "       gardner-proof prove-win --defender <white|black> "
    "--table-megabytes <N> --output <path> [--fen <fen>] "
    "[--moves \"<uci> <uci> ...\"] [--node-limit <N>]\n";

constexpr char kStartFen[] = "rnbqk/ppppp/5/PPPPP/RNBQK w - - 0 1";

// Runs perft from a position (default the Gardner start),
// printing each root move's subtree size and nodes/second.
int run_perft_command(int argument_count, char** arguments) {
  if (argument_count < 3) {
    std::cerr << kUsageMessage;
    return 2;
  }

  const std::string depth_argument = arguments[2];
  int depth = 0;
  const auto parse_result = std::from_chars(
      depth_argument.data(),
      depth_argument.data() + depth_argument.size(), depth);
  const bool depth_is_valid =
      parse_result.ec == std::errc{} &&
      parse_result.ptr ==
          depth_argument.data() + depth_argument.size() &&
      depth >= 0;
  if (!depth_is_valid) {
    std::cerr << kUsageMessage;
    return 2;
  }

  const std::string fen_argument =
      argument_count >= 4 ? arguments[3] : kStartFen;
  const std::optional<gardner::Position> root_position =
      gardner::position_from_fen(fen_argument);
  if (!root_position.has_value()) {
    std::cerr << "unparseable fen: " << fen_argument << "\n";
    return 2;
  }

  const auto start_time = std::chrono::steady_clock::now();
  const gardner::MoveList root_moves =
      gardner::generate_legal_moves(*root_position);
  std::uint64_t total_nodes = 0;
  for (const gardner::Move move : root_moves) {
    const gardner::Position child_position =
        gardner::make_move(*root_position, move);
    const std::uint64_t child_node_count =
        gardner::perft(child_position, depth - 1);
    std::cout << gardner::uci_from_move(move) << ": "
              << child_node_count << "\n";
    total_nodes += child_node_count;
  }
  const auto end_time = std::chrono::steady_clock::now();

  std::cout << "\n";
  std::cout << "Nodes searched: " << total_nodes << "\n";

  const double elapsed_seconds =
      std::chrono::duration<double>(end_time - start_time).count();
  const long long nodes_per_second =
      elapsed_seconds > 0.0
          ? static_cast<long long>(static_cast<double>(total_nodes) /
                                   elapsed_seconds)
          : 0;
  std::cerr << "nodes_per_second: " << nodes_per_second << "\n";
  return 0;
}

// Prints the rook and bishop slider table sizes.
int run_tables_command() {
  std::cout << "rook_table_entries: "
            << gardner::rook_table_entry_count() << "\n";
  std::cout << "bishop_table_entries: "
            << gardner::bishop_table_entry_count() << "\n";
  return 0;
}

// Value following flag_name among the CLI arguments, or
// nullopt if the flag is absent or has no following token.
std::optional<std::string> argument_value(
    int argument_count, char** arguments,
    std::string_view flag_name) {
  for (int index = 2; index < argument_count - 1; ++index) {
    if (flag_name == arguments[index]) {
      return std::string(arguments[index + 1]);
    }
  }
  return std::nullopt;
}

// Parses text as a base-10 std::uint64_t, requiring the
// entire string to be consumed.
std::optional<std::uint64_t> parse_uint64(const std::string& text) {
  std::uint64_t value = 0;
  const auto parse_result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_result.ec != std::errc{} ||
      parse_result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Splits text on runs of whitespace into tokens.
std::vector<std::string> split_on_whitespace(
    const std::string& text) {
  std::vector<std::string> tokens;
  std::size_t position = 0;
  while (position < text.size()) {
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position]))) {
      ++position;
    }
    const std::size_t token_start = position;
    while (
        position < text.size() &&
        !std::isspace(static_cast<unsigned char>(text[position]))) {
      ++position;
    }
    if (position > token_start) {
      tokens.push_back(
          text.substr(token_start, position - token_start));
    }
  }
  return tokens;
}

// Parses prove-win's flags, replays --moves, runs the df-pn
// search, and writes a certificate on Proved.
int run_prove_win_command(int argument_count, char** arguments) {
  const std::optional<std::string> defender_argument =
      argument_value(argument_count, arguments, "--defender");
  const std::optional<std::string> table_megabytes_argument =
      argument_value(argument_count, arguments, "--table-megabytes");
  const std::optional<std::string> output_argument =
      argument_value(argument_count, arguments, "--output");
  if (!defender_argument.has_value() ||
      !table_megabytes_argument.has_value() ||
      !output_argument.has_value()) {
    std::cerr << kUsageMessage;
    return 2;
  }

  gardner::Color defender_color;
  if (*defender_argument == "white") {
    defender_color = gardner::Color::White;
  } else if (*defender_argument == "black") {
    defender_color = gardner::Color::Black;
  } else {
    std::cerr << "unknown defender: " << *defender_argument << "\n";
    return 2;
  }

  const std::optional<std::uint64_t> table_megabytes_value =
      parse_uint64(*table_megabytes_argument);
  if (!table_megabytes_value.has_value()) {
    std::cerr << "invalid table-megabytes: "
              << *table_megabytes_argument << "\n";
    return 2;
  }

  const std::string fen_argument =
      argument_value(argument_count, arguments, "--fen")
          .value_or(kStartFen);
  std::optional<gardner::Position> position =
      gardner::position_from_fen(fen_argument);
  if (!position.has_value()) {
    std::cerr << "unparseable fen: " << fen_argument << "\n";
    return 2;
  }

  const std::optional<std::string> moves_argument =
      argument_value(argument_count, arguments, "--moves");
  if (moves_argument.has_value()) {
    for (const std::string& move_token :
         split_on_whitespace(*moves_argument)) {
      const gardner::MoveList legal_moves =
          gardner::generate_legal_moves(*position);
      std::optional<gardner::Move> matching_move;
      for (const gardner::Move move : legal_moves) {
        if (gardner::uci_from_move(move) == move_token) {
          matching_move = move;
          break;
        }
      }
      if (!matching_move.has_value()) {
        std::cerr << "unknown move: " << move_token << "\n";
        return 2;
      }
      position = gardner::make_move(*position, *matching_move);
    }
  }

  std::uint64_t node_limit_value = 0;
  const std::optional<std::string> node_limit_argument =
      argument_value(argument_count, arguments, "--node-limit");
  if (node_limit_argument.has_value()) {
    const std::optional<std::uint64_t> parsed_node_limit =
        parse_uint64(*node_limit_argument);
    if (!parsed_node_limit.has_value()) {
      std::cerr << "invalid node-limit: " << *node_limit_argument
                << "\n";
      return 2;
    }
    node_limit_value = *parsed_node_limit;
  }

  const gardner::Position normalised_root =
      gardner::defender_as_white(*position, defender_color);

  gardner::SharedTable table(
      static_cast<std::size_t>(*table_megabytes_value));
  gardner::ProofSearch search(table, node_limit_value);

  const auto start_time = std::chrono::steady_clock::now();
  const gardner::ProofResult proof_result =
      search.prove(normalised_root);
  const auto end_time = std::chrono::steady_clock::now();
  const double elapsed_seconds =
      std::chrono::duration<double>(end_time - start_time).count();

  std::string result_name;
  bool wrote_certificate = false;
  std::optional<std::vector<gardner::CertificateEntry>> certificate;

  if (proof_result == gardner::ProofResult::Proved) {
    certificate =
        gardner::extract_certificate(search, normalised_root);
    if (certificate.has_value() &&
        gardner::write_certificate(*output_argument, *certificate)) {
      result_name = "proved";
      wrote_certificate = true;
    } else {
      result_name = "extraction_failed";
    }
  } else if (proof_result == gardner::ProofResult::Disproved) {
    result_name = "disproved";
  } else {
    result_name = "unknown";
  }

  std::cout << "result: " << result_name << "\n";
  std::cout << "nodes: " << search.statistics().nodes_visited << "\n";
  std::cout << "seconds: " << std::fixed << std::setprecision(3)
            << elapsed_seconds << std::defaultfloat << "\n";
  std::cout << "repetition_hits: "
            << search.statistics().repetition_hits << "\n";
  std::cout << "table_entries_used: " << table.occupied_count()
            << "\n";
  if (wrote_certificate) {
    std::cout << "certificate_positions: " << certificate->size()
              << "\n";
    const std::array<std::uint64_t, 26> histogram =
        gardner::men_histogram(*certificate);
    std::cout << "men_histogram: ";
    bool first_entry = true;
    for (std::size_t men_count = 0; men_count < histogram.size();
         ++men_count) {
      if (histogram[men_count] == 0) continue;
      if (!first_entry) std::cout << " ";
      std::cout << men_count << ":" << histogram[men_count];
      first_entry = false;
    }
    std::cout << "\n";
  }

  return wrote_certificate ? 0 : 1;
}

}

// Dispatches to the perft, tables, or prove-win subcommand
// named in argv[1].
int main(int argument_count, char** arguments) {
  if (argument_count < 2) {
    std::cerr << kUsageMessage;
    return 2;
  }

  const std::string command = arguments[1];
  if (command == "perft") {
    return run_perft_command(argument_count, arguments);
  }
  if (command == "tables") {
    return run_tables_command();
  }
  if (command == "prove-win") {
    return run_prove_win_command(argument_count, arguments);
  }

  std::cerr << kUsageMessage;
  return 2;
}
