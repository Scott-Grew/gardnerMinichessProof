#ifndef GARDNER_PROOF_CHECKER_CERTIFICATE_CHECK_H_
#define GARDNER_PROOF_CHECKER_CERTIFICATE_CHECK_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mailbox_board.h"

namespace checker {

// A position packed as 4-bit-per-square nibbles across two
// 64-bit words, plus the side to move as bit 36 of high.
struct BoardKey {
  std::uint64_t low;
  std::uint64_t high;
};

bool operator<(const BoardKey& left, const BoardKey& right);
bool operator==(const BoardKey& left, const BoardKey& right);

BoardKey key_from_board(const Board& board);
std::optional<Board> board_from_key(const BoardKey& key);
Board mirror_board(const Board& board);
Board swap_colours(const Board& board);
BoardKey canonical_key(const Board& board);

// One certificate record: a defender-to-move position's key and
// its stored reply move; promotion_code is 0 for a non-promotion.
struct CertificateEntry {
  BoardKey key;
  int from_square;
  int to_square;
  int promotion_code;
};

// Result of checking a certificate: whether it was accepted, why
// not if rejected, and how many positions it contains.
struct CheckOutcome {
  bool accepted;
  std::string reason;
  std::size_t position_count;
};

std::optional<std::vector<CertificateEntry>> read_certificate(
    const std::string& path, std::string& failure_reason);

CheckOutcome check_certificate(
    const std::vector<CertificateEntry>& entries,
    const Board& real_root, Side defender);

}

#endif
