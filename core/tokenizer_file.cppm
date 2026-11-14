module;

#include "tokenizer_generated.h"

module wdym:tokenizer_file;

import std.compat;
import :normalization;

namespace wdym {
static_assert(
    std::endian::native == std::endian::little,
    "FlatBuffers vectors are read in place, which requires a little-endian machine"
);

std::string_view toStringView(const flatbuffers::String* text) {
  return text == nullptr ? std::string_view() : text->string_view();
}

std::string_view toStringView(const flatbuffers::Vector<uint8_t>* bytes) {
  return bytes == nullptr
             ? std::string_view()
             : std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

std::span<const uint32_t> toSpan(const flatbuffers::Vector<uint32_t>* values) {
  return values == nullptr ? std::span<const uint32_t>()
                           : std::span<const uint32_t>(values->data(), values->size());
}

std::vector<std::byte> finishTokenizerFile(
    flatbuffers::FlatBufferBuilder& builder,
    const Normalizer& normalizer,
    schema::Encoding encodingType,
    flatbuffers::Offset<void> encoding
) {
  std::vector<flatbuffers::Offset<schema::NormalizationStep>> normalizationSteps;
  for (const CompiledNormalizationStep& step : normalizer.steps)
    normalizationSteps.push_back(
        schema::CreateNormalizationStep(
            builder,
            builder.CreateString(step.name),
            builder.CreateVector(step.trieUnits),
            builder.CreateString(step.outputs),
            builder.CreateVector(step.outputOffsets)
        )
    );

  flatbuffers::Offset<flatbuffers::String> invalidUtf8Replacement;
  if (const auto* replacement = std::get_if<std::string>(&normalizer.invalidUtf8Fallback))
    invalidUtf8Replacement = builder.CreateString(*replacement);

  const auto normalization = schema::CreateNormalization(
      builder, invalidUtf8Replacement, builder.CreateVector(normalizationSteps)
  );
  schema::FinishTokenizerBuffer(
      builder, schema::CreateTokenizer(builder, normalization, encodingType, encoding)
  );

  const auto* buffer = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
  return std::vector<std::byte>(buffer, buffer + builder.GetSize());
}

const schema::Tokenizer& getVerifiedTokenizerFile(std::span<const std::byte> alignedBytes) {
  const auto* buffer = reinterpret_cast<const uint8_t*>(alignedBytes.data());
  if (alignedBytes.size() < 2 * sizeof(flatbuffers::uoffset_t) ||
      !schema::TokenizerBufferHasIdentifier(buffer))
    throw std::invalid_argument("the bytes are not a wdym tokenizer file");

  flatbuffers::Verifier verifier(buffer, alignedBytes.size());
  if (!schema::VerifyTokenizerBuffer(verifier))
    throw std::invalid_argument("the tokenizer file is corrupted");

  return *schema::GetTokenizer(buffer);
}

Normalizer readNormalizer(const schema::Tokenizer& tokenizer) {
  std::vector<CompiledNormalizationStep> normalizationSteps;
  InvalidUtf8Fallback invalidUtf8Fallback = KeepInvalidUtf8{};

  if (const schema::Normalization* normalizationTable = tokenizer.normalization()) {
    if (const flatbuffers::String* replacement = normalizationTable->invalid_utf8_replacement())
      invalidUtf8Fallback = std::string(replacement->string_view());

    if (normalizationTable->steps() != nullptr)
      for (const schema::NormalizationStep* step : *normalizationTable->steps())
        normalizationSteps.push_back({
            .name = std::string(toStringView(step->name())),
            .trieUnits = std::ranges::to<std::vector>(toSpan(step->trie_units())),
            .outputs = std::string(toStringView(step->outputs())),
            .outputOffsets = std::ranges::to<std::vector>(toSpan(step->output_offsets())),
        });
  }

  return Normalizer(std::move(normalizationSteps), std::move(invalidUtf8Fallback));
}
}  // namespace wdym
