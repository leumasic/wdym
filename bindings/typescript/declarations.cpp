import std;
import wdym;
import binding_reflection;

template <typename T>
constexpr bool IS_INTERFACE = std::is_class_v<T> &&
                              !std::same_as<T, std::monostate> &&
                              !std::same_as<T, std::string> &&
                              !isTemplateSpecializationOf(^^T, ^^std::vector) &&
                              !isTemplateSpecializationOf(^^T, ^^std::variant);

constexpr std::string_view TRAIN_INPUT_DECLARATION = R"(export type TrainInput =
  | { input: string | string[]; sentenceIterator?: never }
  | { sentenceIterator: Iterable<string>; input?: never };

)";

class TypeScriptDeclarations {
 public:
  template <typename T>
  std::string getType() {
    if constexpr (std::same_as<T, std::monostate>)
      return "null";
    else if constexpr (std::same_as<T, bool>)
      return "boolean";
    else if constexpr (std::integral<T>)
      return "number";
    else if constexpr (std::same_as<T, std::string>)
      return "string";
    else if constexpr (std::is_enum_v<T>)
      return getEnumType<T>();
    else if constexpr (isTemplateSpecializationOf(^^T, ^^std::vector))
      return isTemplateSpecializationOf(^^typename T::value_type, ^^std::variant)
                 ? std::format("({})[]", getType<typename T::value_type>())
                 : std::format("{}[]", getType<typename T::value_type>());
    else if constexpr (isTemplateSpecializationOf(^^T, ^^std::variant))
      return getVariantType<T>();
    else
      return getInterfaceName<T>();
  }

  template <typename T>
  std::string getFieldDeclarations() {
    std::string fieldDeclarations;

    template for (constexpr auto field : getFields(^^T)) {
      fieldDeclarations += std::format(
          "  {}{}: {};\n",
          getIdentifier(field),
          std::meta::has_default_member_initializer(field) ? "?" : "",
          getType<typename[:std::meta::type_of(field):]>()
      );
    }

    return fieldDeclarations;
  }

  template <wdym::Tokenizer T>
  std::string getTokenizerDeclaration() {
    const std::string name = getIdentifier(^^T);
    const std::string trainOptionFieldDeclarations =
        getFieldDeclarations<typename T::TrainOptions>();
    const std::string settingsType = getType<typename T::Settings>();
    const std::string normalizationType = getType<wdym::NormalizationSettings>();
    const std::string decodeOptionsType = getType<typename T::DecodeOptions>();
    const std::string sentencePieceLoaderDeclarations =
        requires(const std::filesystem::path& path) { T::loadSentencePiece(path); }
            ? std::format(
                  "  static loadSentencePiece(path: string): {0};\n"
                  "  static fromSentencePiece(bytes: Uint8Array): {0};\n",
                  name
              )
            : "";

    return std::format(
        R"(export type {0}TrainArguments = TrainInput & {{
{1}  settings?: {2};
  normalization?: {3};
}};

export class {0} {{
  private constructor();

  static train(options: {0}TrainArguments): Promise<{0}>;
  static load(path: string): {0};
  static fromBytes(bytes: Uint8Array): {0};
{5}
  save(path: string): void;
  toBytes(): Uint8Array;
  normalize(text: string): string;
  encode(text: string): Uint32Array;
  decode(tokenIds: Uint32Array | number[], options?: {4}): string;
  tokenToId(token: string | Uint8Array): number | undefined;
  markupTokenToId(token: string): number | undefined;
}}
)",
        name,
        trainOptionFieldDeclarations,
        settingsType,
        normalizationType,
        decodeOptionsType,
        sentencePieceLoaderDeclarations
    );
  }

  std::string getInterfaceDeclarations() const {
    std::string interfaceDeclarations;

    for (const std::string& name : interfaceNames_)
      interfaceDeclarations += std::format(
          "export interface {} {{\n{}{}}}\n\n",
          name,
          taggedInterfaceNames_.contains(name) ? std::format("  type: \"{}\";\n", name) : "",
          interfaceNameToFieldDeclarations_.at(name)
      );

    return interfaceDeclarations;
  }

 private:
  template <typename Variant>
  std::string getVariantType() {
    std::vector<std::string> alternativeTypes;

    template for (
        constexpr auto alternative :
        std::define_static_array(std::meta::template_arguments_of(^^Variant))
    ) {
      using Alternative = typename[:alternative:];

      if constexpr (IS_INTERFACE<Alternative>)
        taggedInterfaceNames_.insert(getIdentifier(alternative));
      alternativeTypes.push_back(getType<Alternative>());
    }

    return alternativeTypes |
           std::views::join_with(std::string_view(" | ")) |
           std::ranges::to<std::string>();
  }

  template <typename Enum>
  std::string getEnumType() {
    std::vector<std::string> enumeratorTypes;

    template for (constexpr auto enumerator : getEnumerators(^^Enum))
      enumeratorTypes.push_back(std::format("\"{}\"", getIdentifier(enumerator)));

    return enumeratorTypes |
           std::views::join_with(std::string_view(" | ")) |
           std::ranges::to<std::string>();
  }

  template <typename T>
  std::string getInterfaceName() {
    const std::string name = getIdentifier(^^T);

    if (!interfaceNameToFieldDeclarations_.contains(name)) {
      interfaceNames_.push_back(name);
      interfaceNameToFieldDeclarations_[name] = getFieldDeclarations<T>();
    }

    return name;
  }

  std::vector<std::string> interfaceNames_;
  std::map<std::string, std::string> interfaceNameToFieldDeclarations_;
  std::set<std::string> taggedInterfaceNames_;
};

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(std::cerr, "usage: {} <declarations file>", argv[0]);
    return 1;
  }

  TypeScriptDeclarations declarations;
  const std::string bytePairEncodingDeclaration =
      declarations.getTokenizerDeclaration<wdym::BytePairEncoding>();

  std::ofstream file(argv[1]);
  file << TRAIN_INPUT_DECLARATION << declarations.getInterfaceDeclarations()
       << bytePairEncodingDeclaration;
}
