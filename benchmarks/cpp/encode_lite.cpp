#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cached_sentencepiece_lite.h"
#include "sentencepiece_lite.h"

using Clock = std::chrono::steady_clock;
using sentencepiece::lite::CachedSentencePieceLite;
using sentencepiece::lite::SentencePieceLiteProcessor;
using sentencepiece::lite::StatusCode;
using sentencepiece::lite::TokenCache;

struct TokenIdBatch {
  std::vector<int> tokenIds;
  std::vector<std::size_t> tokenIdOffsets;
};

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  for (std::string line; std::getline(file, line);)
    lines.push_back(line);
  return lines;
}

TokenIdBatch encodeBatch(
    const SentencePieceLiteProcessor& processor,
    const std::vector<std::string>& texts,
    std::size_t threadCount,
    bool useCache
) {
  TokenIdBatch batch;
  batch.tokenIdOffsets.resize(texts.size() + 1);
  std::vector<std::vector<int>> chunkTokenIds(threadCount);

  const auto encodeChunk = [&](std::size_t chunkIndex) {
    std::vector<int> tokenIds;
    std::vector<int> textTokenIds;
    std::optional<TokenCache> cache;
    if (useCache)
      cache.emplace();

    const std::size_t firstTextIndex = texts.size() * chunkIndex / threadCount;
    const std::size_t endTextIndex = texts.size() * (chunkIndex + 1) / threadCount;

    for (std::size_t textIndex = firstTextIndex; textIndex < endTextIndex; ++textIndex) {
      if (useCache)
        CachedSentencePieceLite::Encode(processor, *cache, texts[textIndex], &textTokenIds);
      else
        processor.Encode(texts[textIndex], &textTokenIds);

      tokenIds.insert(tokenIds.end(), textTokenIds.begin(), textTokenIds.end());
      batch.tokenIdOffsets[textIndex + 1] = textTokenIds.size();
    }

    chunkTokenIds[chunkIndex] = std::move(tokenIds);
  };

  std::vector<std::thread> threads;
  for (std::size_t chunkIndex = 1; chunkIndex < threadCount; ++chunkIndex)
    threads.emplace_back(encodeChunk, chunkIndex);

  encodeChunk(0);
  for (std::thread& thread : threads)
    thread.join();

  std::partial_sum(
      batch.tokenIdOffsets.begin(), batch.tokenIdOffsets.end(), batch.tokenIdOffsets.begin()
  );

  batch.tokenIds.reserve(batch.tokenIdOffsets.back());
  for (const std::vector<int>& tokenIds : chunkTokenIds)
    batch.tokenIds.insert(batch.tokenIds.end(), tokenIds.begin(), tokenIds.end());

  return batch;
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "usage: encode_lite <model.spm.fb> <test file> <vocabulary size> <repeats> <threads>\n";
    return 1;
  }

  std::ifstream modelFile(argv[1], std::ios::binary);
  const std::string model((std::istreambuf_iterator<char>(modelFile)), std::istreambuf_iterator<char>());
  const SentencePieceLiteProcessor processor(model);
  if (processor.status() != StatusCode::kOk) {
    std::cerr << "could not load " << argv[1] << "\n";
    return 1;
  }

  const std::vector<std::string> lines = readLines(argv[2]);
  const int vocabularySize = std::stoi(argv[3]);
  const int repeats = std::stoi(argv[4]);
  const std::size_t threadCount = std::stoul(argv[5]);

  std::size_t byteCount = 0;
  for (const std::string& line : lines)
    byteCount += line.size();

  const auto benchmarkEncoding = [&](bool useCache) {
    double bestSeconds = std::numeric_limits<double>::infinity();
    std::size_t tokenCount = 0;
    std::vector<int> tokenIds;

    for (int repeat = 0; repeat < repeats; ++repeat) {
      if (threadCount > 1) {
        const auto start = Clock::now();
        const TokenIdBatch batch = encodeBatch(processor, lines, threadCount, useCache);
        bestSeconds = std::min(bestSeconds, std::chrono::duration<double>(Clock::now() - start).count());
        tokenCount = batch.tokenIds.size();
      } else {
        TokenCache cache;
        tokenCount = 0;

        const auto start = Clock::now();
        for (const std::string& line : lines) {
          tokenIds.clear();
          if (useCache)
            CachedSentencePieceLite::Encode(processor, cache, line, &tokenIds);
          else
            processor.Encode(line, &tokenIds);
          tokenCount += tokenIds.size();
        }
        bestSeconds = std::min(bestSeconds, std::chrono::duration<double>(Clock::now() - start).count());
      }
    }

    std::cout << R"({"benchmark": "encode", "api": "c++", "library": "sentencepiece-lite", "vocabulary_size": )"
              << vocabularySize << R"(, "threads": )" << threadCount << R"(, "cache": )"
              << (useCache ? "true" : "false") << R"(, "tokens": )" << tokenCount
              << R"(, "megabytes_per_second": )" << byteCount / bestSeconds / 1e6 << "}\n";
  };

  benchmarkEncoding(false);
  benchmarkEncoding(true);
}
