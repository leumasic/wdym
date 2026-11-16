import std;
import wdym;

int main(int argc, char** argv) {
  if (argc != 4) {
    std::println(std::cerr, "usage: train_wdym <train file> <vocabulary size> <tokenizer>");
    return 1;
  }

  std::ifstream file(argv[1], std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);)
    lines.push_back(line);

  wdym::BytePairEncoding::train(lines, {.maxVocabularySize = std::stoi(argv[2])}).save(argv[3]);
}
