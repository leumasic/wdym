#include <napi.h>

import std;
import wdym;
import binding_reflection;

namespace fs = std::filesystem;

template <typename T>
T fromJs(const Napi::Value& value, const std::string& name);

template <typename T>
T structFromJs(const Napi::Value& value, const std::string& name) {
  if (!value.IsObject())
    throw Napi::TypeError::New(value.Env(), name + " must be an object");

  const auto object = value.As<Napi::Object>();
  T result{};

  template for (constexpr auto field : getFields(^^T)) {
    const std::string fieldName = name + "." + getIdentifier(field);
    const Napi::Value fieldValue = object.Get(getIdentifier(field));

    if (!fieldValue.IsUndefined())
      result.[:field:] = fromJs<typename[:std::meta::type_of(field):]>(fieldValue, fieldName);
    else if (!std::meta::has_default_member_initializer(field))
      throw Napi::TypeError::New(value.Env(), fieldName + " is required");
  }

  return result;
}

template <typename Variant>
Variant variantFromJs(const Napi::Value& value, const std::string& name) {
  static constexpr auto ALTERNATIVES =
      std::define_static_array(std::meta::template_arguments_of(^^Variant));

  template for (constexpr auto alternative : ALTERNATIVES) {
    using Alternative = typename[:alternative:];

    if constexpr (std::same_as<Alternative, std::monostate>) {
      if (value.IsNull() || value.IsUndefined())
        return Alternative{};
    } else if constexpr (std::same_as<Alternative, std::string>) {
      if (value.IsString())
        return fromJs<Alternative>(value, name);
    } else {
      if (value.IsObject() &&
          value.As<Napi::Object>().Get("type").StrictEquals(
              Napi::String::New(value.Env(), getIdentifier(alternative))
          ))
        return structFromJs<Alternative>(value, name);
    }
  }

  throw Napi::TypeError::New(value.Env(), name + " does not match any of its allowed types");
}

template <typename T>
T fromJs(const Napi::Value& value, const std::string& name) {
  const Napi::Env env = value.Env();

  if constexpr (std::same_as<T, std::monostate>) {
    if (!value.IsNull() && !value.IsUndefined())
      throw Napi::TypeError::New(env, name + " must be null or undefined");
    return T{};
  } else if constexpr (std::same_as<T, bool>) {
    if (!value.IsBoolean())
      throw Napi::TypeError::New(env, name + " must be a boolean");
    return value.As<Napi::Boolean>().Value();
  } else if constexpr (std::integral<T>) {
    if (!value.IsNumber())
      throw Napi::TypeError::New(env, name + " must be a number");

    const double number = value.As<Napi::Number>().DoubleValue();
    if (std::trunc(number) != number || number < std::numeric_limits<T>::min() ||
        number > std::numeric_limits<T>::max())
      throw Napi::RangeError::New(
          env,
          std::format(
              "{} must be an integer from {} to {}",
              name,
              std::numeric_limits<T>::min(),
              std::numeric_limits<T>::max()
          )
      );

    return static_cast<T>(number);
  } else if constexpr (std::same_as<T, std::string>) {
    if (!value.IsString())
      throw Napi::TypeError::New(env, name + " must be a string");
    return value.As<Napi::String>().Utf8Value();
  } else if constexpr (std::is_enum_v<T>) {
    std::vector<std::string_view> enumeratorNames;

    template for (constexpr auto enumerator : getEnumerators(^^T)) {
      if (value.IsString() && value.As<Napi::String>().Utf8Value() == getIdentifier(enumerator))
        return [:enumerator:];

      enumeratorNames.push_back(getIdentifier(enumerator));
    }

    throw Napi::TypeError::New(env, std::format("{} must be one of {}", name, enumeratorNames));
  } else if constexpr (isTemplateSpecializationOf(^^T, ^^std::vector)) {
    if (!value.IsArray())
      throw Napi::TypeError::New(env, name + " must be an array");

    const auto array = value.As<Napi::Array>();
    T values;
    for (std::uint32_t index = 0; index < array.Length(); ++index)
      values.push_back(
          fromJs<typename T::value_type>(array.Get(index), std::format("{}[{}]", name, index))
      );
    return values;
  } else if constexpr (isTemplateSpecializationOf(^^T, ^^std::variant)) {
    return variantFromJs<T>(value, name);
  } else {
    return structFromJs<T>(value, name);
  }
}

template <typename T>
T fromJsOrDefault(const Napi::Value& value, const std::string& name) {
  return value.IsUndefined() ? T{} : fromJs<T>(value, name);
}

std::vector<std::string> collectStrings(const Napi::Value& iterable, const std::string& name) {
  const Napi::Env env = iterable.Env();
  const Napi::Value getIterator =
      iterable.IsObject()
          ? iterable.As<Napi::Object>().Get(Napi::Symbol::WellKnown(env, "iterator"))
          : env.Undefined();
  if (!getIterator.IsFunction())
    throw Napi::TypeError::New(env, name + " must be iterable");

  const auto iterator = getIterator.As<Napi::Function>().Call(iterable, {}).As<Napi::Object>();
  const auto next = iterator.Get("next").As<Napi::Function>();
  const auto getNextResult = [&] { return next.Call(iterator, {}).As<Napi::Object>(); };

  std::vector<std::string> strings;
  for (
      Napi::Object result = getNextResult(); !result.Get("done").ToBoolean();
      result = getNextResult()
  )
    strings.push_back(fromJs<std::string>(result.Get("value"), name + " item"));

  return strings;
}

std::span<const std::byte> bytesFromJs(const Napi::Value& value, const std::string& name) {
  if (!value.IsTypedArray() || value.As<Napi::TypedArray>().TypedArrayType() != napi_uint8_array)
    throw Napi::TypeError::New(value.Env(), name + " must be a Uint8Array");

  const auto bytes = value.As<Napi::Uint8Array>();
  return std::as_bytes(std::span(bytes.Data(), bytes.ByteLength()));
}

std::vector<std::uint32_t> tokenIdsFromJs(const Napi::Value& value, const std::string& name) {
  if (value.IsTypedArray() && value.As<Napi::TypedArray>().TypedArrayType() == napi_uint32_array) {
    const auto tokenIds = value.As<Napi::Uint32Array>();
    return std::vector<std::uint32_t>(tokenIds.Data(), tokenIds.Data() + tokenIds.ElementLength());
  }

  return fromJs<std::vector<std::uint32_t>>(value, name);
}

Napi::Value toJs(Napi::Env env, const std::optional<std::uint32_t>& tokenId) {
  return tokenId ? Napi::Number::New(env, *tokenId) : env.Undefined();
}

using TokenizerConstructors = std::unordered_map<std::string_view, Napi::FunctionReference>;

template <wdym::Tokenizer T>
class JsTokenizer : public Napi::ObjectWrap<JsTokenizer<T>> {
 public:
  using TrainOptions = T::TrainOptions;
  using Settings = T::Settings;
  using DecodeOptions = T::DecodeOptions;

  static Napi::Function defineClass(Napi::Env env) {
    std::vector<typename JsTokenizer::PropertyDescriptor> properties = {
        JsTokenizer::template StaticMethod<&JsTokenizer::train>("train"),
        JsTokenizer::template StaticMethod<&JsTokenizer::load>("load"),
        JsTokenizer::template StaticMethod<&JsTokenizer::fromBytes>("fromBytes"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::save>("save"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::toBytes>("toBytes"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::normalize>("normalize"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::encode>("encode"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::decode>("decode"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::tokenToId>("tokenToId"),
        JsTokenizer::template InstanceMethod<&JsTokenizer::markupTokenToId>("markupTokenToId"),
    };

    if constexpr (HAS_SENTENCEPIECE_LOADERS) {
      properties.push_back(
          JsTokenizer::template StaticMethod<&JsTokenizer::loadSentencePiece>("loadSentencePiece")
      );
      properties.push_back(
          JsTokenizer::template StaticMethod<&JsTokenizer::fromSentencePiece>("fromSentencePiece")
      );
    }

    return JsTokenizer::DefineClass(env, getIdentifier(^^T), properties);
  }

  explicit JsTokenizer(const Napi::CallbackInfo& info)
      : Napi::ObjectWrap<JsTokenizer>(info), tokenizer_(std::move(getTokenizerArgument(info))) {}

 private:
  static constexpr bool HAS_SENTENCEPIECE_LOADERS =
      requires(const fs::path& path) { T::loadSentencePiece(path); };

  class TrainWorker : public Napi::AsyncWorker {
   public:
    TrainWorker(Napi::Env env, std::function<T()> trainTokenizer)
        : Napi::AsyncWorker(env),
          trainTokenizer_(std::move(trainTokenizer)),
          deferred_(Napi::Promise::Deferred::New(env)) {}

    Napi::Promise getPromise() const { return deferred_.Promise(); }

   protected:
    void Execute() override { tokenizer_.emplace(trainTokenizer_()); }

    void OnOK() override { deferred_.Resolve(create(Env(), std::move(*tokenizer_))); }

    void OnError(const Napi::Error& error) override { deferred_.Reject(error.Value()); }

   private:
    std::function<T()> trainTokenizer_;
    std::optional<T> tokenizer_;
    Napi::Promise::Deferred deferred_;
  };

  static T& getTokenizerArgument(const Napi::CallbackInfo& info) {
    if (info.Length() != 1 || !info[0].IsExternal())
      throw Napi::TypeError::New(
          info.Env(),
          std::format("use {0}.train, {0}.load or {0}.fromBytes instead", getIdentifier(^^T))
      );

    return *info[0].As<Napi::External<T>>().Data();
  }

  static Napi::Object create(Napi::Env env, T tokenizer) {
    const Napi::FunctionReference& constructor =
        env.GetInstanceData<TokenizerConstructors>()->at(getIdentifier(^^T));
    return constructor.New({Napi::External<T>::New(env, &tokenizer)});
  }

  static Napi::Value train(const Napi::CallbackInfo& info) {
    const Napi::Env env = info.Env();
    if (!info[0].IsObject())
      throw Napi::TypeError::New(env, "options must be an object");

    const auto options = info[0].As<Napi::Object>();
    const auto trainOptions = structFromJs<TrainOptions>(options, "options");
    const auto settings = fromJsOrDefault<Settings>(options.Get("settings"), "options.settings");
    const auto normalization = fromJsOrDefault<wdym::NormalizationSettings>(
        options.Get("normalization"), "options.normalization"
    );

    const Napi::Value input = options.Get("input");
    const Napi::Value sentenceIterator = options.Get("sentenceIterator");
    if (input.IsUndefined() == sentenceIterator.IsUndefined())
      throw Napi::TypeError::New(env, "options must have either input or sentenceIterator");

    std::function<T()> trainTokenizer;
    if (input.IsString())
      trainTokenizer = [=, path = fs::path(fromJs<std::string>(input, "options.input"))] {
        return T::trainFromFiles(path, trainOptions, settings, normalization);
      };
    else if (input.IsArray())
      trainTokenizer = [=, paths = fromJs<std::vector<std::string>>(input, "options.input")] {
        return T::trainFromFiles(paths, trainOptions, settings, normalization);
      };
    else if (!input.IsUndefined())
      throw Napi::TypeError::New(env, "options.input must be a string or an array of strings");
    else
      trainTokenizer = [=, texts = collectStrings(sentenceIterator, "options.sentenceIterator")] {
        return T::train(texts, trainOptions, settings, normalization);
      };

    auto* worker = new TrainWorker(env, std::move(trainTokenizer));
    worker->Queue();
    return worker->getPromise();
  }

  static Napi::Value load(const Napi::CallbackInfo& info) {
    return create(info.Env(), T::load(fromJs<std::string>(info[0], "path")));
  }

  static Napi::Value fromBytes(const Napi::CallbackInfo& info) {
    return create(info.Env(), T::fromBytes(bytesFromJs(info[0], "bytes")));
  }

  static Napi::Value loadSentencePiece(const Napi::CallbackInfo& info)
    requires HAS_SENTENCEPIECE_LOADERS
  {
    return create(info.Env(), T::loadSentencePiece(fromJs<std::string>(info[0], "path")));
  }

  static Napi::Value fromSentencePiece(const Napi::CallbackInfo& info)
    requires HAS_SENTENCEPIECE_LOADERS
  {
    return create(info.Env(), T::fromSentencePiece(bytesFromJs(info[0], "bytes")));
  }

  void save(const Napi::CallbackInfo& info) {
    tokenizer_.save(fromJs<std::string>(info[0], "path"));
  }

  Napi::Value toBytes(const Napi::CallbackInfo& info) {
    const std::vector<std::byte> bytes = tokenizer_.toBytes();
    auto array = Napi::Uint8Array::New(info.Env(), bytes.size());
    std::ranges::copy(bytes, reinterpret_cast<std::byte*>(array.Data()));
    return array;
  }

  Napi::Value normalize(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), tokenizer_.normalize(fromJs<std::string>(info[0], "text")));
  }

  Napi::Value encode(const Napi::CallbackInfo& info) {
    const std::vector<std::uint32_t> tokenIds =
        tokenizer_.encode(fromJs<std::string>(info[0], "text"));
    auto array = Napi::Uint32Array::New(info.Env(), tokenIds.size());
    std::ranges::copy(tokenIds, array.Data());
    return array;
  }

  Napi::Value decode(const Napi::CallbackInfo& info) {
    const std::vector<std::uint32_t> tokenIds = tokenIdsFromJs(info[0], "tokenIds");
    const auto options = fromJsOrDefault<DecodeOptions>(info[1], "options");
    return Napi::String::New(info.Env(), tokenizer_.decode(tokenIds, options));
  }

  Napi::Value tokenToId(const Napi::CallbackInfo& info) {
    if (info[0].IsString())
      return toJs(info.Env(), tokenizer_.tokenToId(fromJs<std::string>(info[0], "token")));

    const std::span<const std::byte> token = bytesFromJs(info[0], "token");
    return toJs(
        info.Env(),
        tokenizer_.tokenToId(
            std::string_view(reinterpret_cast<const char*>(token.data()), token.size())
        )
    );
  }

  Napi::Value markupTokenToId(const Napi::CallbackInfo& info) {
    return toJs(info.Env(), tokenizer_.markupTokenToId(fromJs<std::string>(info[0], "token")));
  }

  const T tokenizer_;
};

template <wdym::Tokenizer T>
void bindTokenizer(Napi::Env env, Napi::Object exports) {
  const Napi::Function tokenizerClass = JsTokenizer<T>::defineClass(env);
  env.GetInstanceData<TokenizerConstructors>()->emplace(
      getIdentifier(^^T), Napi::Persistent(tokenizerClass)
  );
  exports.Set(getIdentifier(^^T), tokenizerClass);
}

Napi::Object initialize(Napi::Env env, Napi::Object exports) {
  env.SetInstanceData(new TokenizerConstructors());

  bindTokenizer<wdym::BytePairEncoding>(env, exports);

  return exports;
}

NODE_API_MODULE(wdym, initialize)
