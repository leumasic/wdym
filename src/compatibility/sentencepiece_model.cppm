module wdym:sentencepiece_model;

import std.compat;

namespace wdym {
enum class SentencePieceType : uint8_t {
  Normal = 1,
  Unknown = 2,
  Control = 3,
  UserDefined = 4,
  Unused = 5,
  Byte = 6,
};

enum class SentencePieceAlgorithm : uint8_t {
  Unigram = 1,
  BytePairEncoding = 2,
  Word = 3,
  Character = 4,
};

struct SentencePiece {
  std::string text;
  float score = 0;
  SentencePieceType type = SentencePieceType::Normal;
};

struct SentencePieceModel {
  std::vector<SentencePiece> pieces;

  SentencePieceAlgorithm algorithm = SentencePieceAlgorithm::Unigram;
  bool byteFallback = false;
  bool treatWhitespaceAsSuffix = false;
  std::string unknownSurface = " ⁇ ";

  std::string normalizerName;
  std::string precompiledCharacterMap;
  std::string normalizationRuleTsv;
  bool addDummyPrefix = true;
  bool removeExtraWhitespaces = true;
  bool escapeWhitespaces = true;
};

namespace sentencepiece_model_detail {
enum class WireType : uint8_t {
  Varint = 0,
  Fixed64 = 1,
  LengthDelimited = 2,
  Fixed32 = 5,
};

struct FieldKey {
  uint64_t fieldNumber;
  WireType wireType;
};

[[noreturn]] inline void throwCorrupted() {
  throw std::invalid_argument("the SentencePiece model is corrupted");
}

class ProtobufReader {
 public:
  explicit ProtobufReader(std::string_view bytes) : unreadBytes_(bytes) {}

  bool isAtEnd() const { return unreadBytes_.empty(); }

  FieldKey readFieldKey() {
    const uint64_t key = readVarint();
    const uint64_t wireType = key & 0b111;
    if (wireType != std::to_underlying(WireType::Varint) &&
        wireType != std::to_underlying(WireType::Fixed64) &&
        wireType != std::to_underlying(WireType::LengthDelimited) &&
        wireType != std::to_underlying(WireType::Fixed32))
      throwCorrupted();

    return {.fieldNumber = key >> 3, .wireType = static_cast<WireType>(wireType)};
  }

  uint64_t readVarint() {
    static constexpr size_t MAX_VARINT_SIZE = 10;

    uint64_t value = 0;
    for (size_t byteIndex = 0; byteIndex < MAX_VARINT_SIZE; ++byteIndex) {
      const auto byte = static_cast<uint8_t>(readBytes(1).front());
      value |= static_cast<uint64_t>(byte & 0x7F) << (7 * byteIndex);
      if ((byte & 0x80) == 0)
        return value;
    }

    throwCorrupted();
  }

  std::string_view readLengthDelimited() { return readBytes(readVarint()); }

  float readFloat() {
    const std::string_view bytes = readBytes(sizeof(float));

    uint32_t littleEndianBits = 0;
    for (size_t byteIndex = 0; byteIndex < sizeof(float); ++byteIndex)
      littleEndianBits |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[byteIndex]))
                          << (8 * byteIndex);

    return std::bit_cast<float>(littleEndianBits);
  }

  void skip(WireType wireType) {
    switch (wireType) {
      case WireType::Varint:
        readVarint();
        return;
      case WireType::Fixed64:
        readBytes(8);
        return;
      case WireType::LengthDelimited:
        readLengthDelimited();
        return;
      case WireType::Fixed32:
        readBytes(4);
        return;
    }
  }

 private:
  std::string_view readBytes(size_t size) {
    if (size > unreadBytes_.size())
      throwCorrupted();

    const std::string_view bytes = unreadBytes_.substr(0, size);
    unreadBytes_.remove_prefix(size);
    return bytes;
  }

  std::string_view unreadBytes_;
};

inline void expectWireType(FieldKey fieldKey, WireType expectedWireType) {
  if (fieldKey.wireType != expectedWireType)
    throwCorrupted();
}

inline std::string readString(ProtobufReader& reader, FieldKey fieldKey) {
  expectWireType(fieldKey, WireType::LengthDelimited);
  return std::string(reader.readLengthDelimited());
}

inline bool readBool(ProtobufReader& reader, FieldKey fieldKey) {
  expectWireType(fieldKey, WireType::Varint);
  return reader.readVarint() != 0;
}

inline SentencePiece readPiece(std::string_view bytes) {
  SentencePiece piece;
  ProtobufReader reader(bytes);

  while (!reader.isAtEnd()) {
    const FieldKey fieldKey = reader.readFieldKey();
    switch (fieldKey.fieldNumber) {
      case 1:
        piece.text = readString(reader, fieldKey);
        break;
      case 2:
        expectWireType(fieldKey, WireType::Fixed32);
        piece.score = reader.readFloat();
        break;
      case 3: {
        expectWireType(fieldKey, WireType::Varint);
        const uint64_t type = reader.readVarint();
        if (type < std::to_underlying(SentencePieceType::Normal) ||
            type > std::to_underlying(SentencePieceType::Byte))
          throwCorrupted();

        piece.type = static_cast<SentencePieceType>(type);
        break;
      }
      default:
        reader.skip(fieldKey.wireType);
    }
  }

  if (piece.text.empty())
    throwCorrupted();

  return piece;
}

inline void readTrainerSpec(std::string_view bytes, SentencePieceModel& model) {
  ProtobufReader reader(bytes);

  while (!reader.isAtEnd()) {
    const FieldKey fieldKey = reader.readFieldKey();
    switch (fieldKey.fieldNumber) {
      case 3: {
        expectWireType(fieldKey, WireType::Varint);
        const uint64_t algorithm = reader.readVarint();
        if (algorithm < std::to_underlying(SentencePieceAlgorithm::Unigram) ||
            algorithm > std::to_underlying(SentencePieceAlgorithm::Character))
          throwCorrupted();

        model.algorithm = static_cast<SentencePieceAlgorithm>(algorithm);
        break;
      }
      case 24:
        model.treatWhitespaceAsSuffix = readBool(reader, fieldKey);
        break;
      case 35:
        model.byteFallback = readBool(reader, fieldKey);
        break;
      case 44:
        model.unknownSurface = readString(reader, fieldKey);
        break;
      default:
        reader.skip(fieldKey.wireType);
    }
  }
}

inline void readNormalizerSpec(std::string_view bytes, SentencePieceModel& model) {
  ProtobufReader reader(bytes);

  while (!reader.isAtEnd()) {
    const FieldKey fieldKey = reader.readFieldKey();
    switch (fieldKey.fieldNumber) {
      case 1:
        model.normalizerName = readString(reader, fieldKey);
        break;
      case 2:
        model.precompiledCharacterMap = readString(reader, fieldKey);
        break;
      case 3:
        model.addDummyPrefix = readBool(reader, fieldKey);
        break;
      case 4:
        model.removeExtraWhitespaces = readBool(reader, fieldKey);
        break;
      case 5:
        model.escapeWhitespaces = readBool(reader, fieldKey);
        break;
      case 6:
        model.normalizationRuleTsv = readString(reader, fieldKey);
        break;
      default:
        reader.skip(fieldKey.wireType);
    }
  }
}
}  // namespace sentencepiece_model_detail

inline SentencePieceModel readSentencePieceModel(std::string_view bytes) {
  using namespace sentencepiece_model_detail;

  SentencePieceModel model;
  ProtobufReader reader(bytes);

  while (!reader.isAtEnd()) {
    const FieldKey fieldKey = reader.readFieldKey();
    switch (fieldKey.fieldNumber) {
      case 1:
        expectWireType(fieldKey, WireType::LengthDelimited);
        model.pieces.push_back(readPiece(reader.readLengthDelimited()));
        break;
      case 2:
        expectWireType(fieldKey, WireType::LengthDelimited);
        readTrainerSpec(reader.readLengthDelimited(), model);
        break;
      case 3:
        expectWireType(fieldKey, WireType::LengthDelimited);
        readNormalizerSpec(reader.readLengthDelimited(), model);
        break;
      default:
        reader.skip(fieldKey.wireType);
    }
  }

  if (model.pieces.empty())
    throwCorrupted();

  return model;
}
}  // namespace wdym
