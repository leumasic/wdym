#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace wdym {
class ByteLevelBPETokenizer {
public:
  explicit ByteLevelBPETokenizer();

  std::unordered_map<std::string, int> vocabulary;
  void train(std::vector<std::string> files);
  void save();
  void load();

  // word -> id
  void encode();
  // id -> word
  void decode();
};
} // namespace wdym
