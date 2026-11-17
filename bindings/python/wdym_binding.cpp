#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>

import std;
import wdym;
import binding_reflection;

namespace nb = nanobind;
using namespace nb::literals;
namespace fs = std::filesystem;

// The lengths we go to to make our python friends happy...
consteval std::string toSnakeCase(std::string_view name) {
  std::string snakeCaseName;

  for (const char character : name) {
    if (character >= 'A' && character <= 'Z') {
      snakeCaseName.push_back('_');
      snakeCaseName.push_back(static_cast<char>(character - 'A' + 'a'));
    } else {
      snakeCaseName.push_back(character);
    }
  }

  return snakeCaseName;
}

consteval const char* getPythonFieldName(std::meta::info field) {
  return std::define_static_string(toSnakeCase(std::meta::identifier_of(field)));
}

consteval bool canRepresentNone(std::meta::info type) {
  type = std::meta::dealias(type);
  if (type == ^^std::monostate)
    return true;

  if (!isTemplateSpecializationOf(type, ^^std::variant))
    return false;

  return std::ranges::contains(
      std::meta::template_arguments_of(type) | std::views::transform(std::meta::dealias),
      ^^std::monostate
  );
}

template <typename T, std::size_t FieldIndex>
using FieldType = [:std::meta::type_of(getFields(^^T)[FieldIndex]):];

template <std::meta::info Type>
auto makeArgument(const char* name) {
  const nb::arg argument(name);

  if constexpr (canRepresentNone(Type))
    return argument.none();
  else
    return argument;
}

template <typename T>
auto makeArgument(const char* name, const T& defaultValue) {
  return makeArgument<^^T>(name) = defaultValue;
}

template <std::meta::info Field, typename T>
auto makeFieldArgument(const T& defaults) {
  constexpr auto PYTHON_FIELD_NAME = getPythonFieldName(Field);

  if constexpr (std::meta::has_default_member_initializer(Field))
    return makeArgument(PYTHON_FIELD_NAME, defaults.[:Field:]);
  else
    return makeArgument<std::meta::type_of(Field)>(PYTHON_FIELD_NAME);
}

template <typename T>
void bindStruct(nb::module_& m) {
  static constexpr auto FIELDS = getFields(^^T);
  const T defaults{};

  nb::class_<T> cls(m, getIdentifier(^^T));
  if constexpr (FIELDS.empty())
    cls.def(nb::init<>());
  else
    [&]<std::size_t... FieldIndices>(std::index_sequence<FieldIndices...>) {
      cls.def(
          nb::init<FieldType<T, FieldIndices>...>(),
          nb::kw_only(),
          makeFieldArgument<FIELDS[FieldIndices]>(defaults)...
      );
    }(std::make_index_sequence<FIELDS.size()>());

  template for (constexpr auto field : FIELDS) {
    const auto setterArgument = makeArgument<std::meta::type_of(field)>("value");
    cls.def_rw(getPythonFieldName(field), &[:field:], nb::for_setter(setterArgument));
  }

  cls.def(nb::self == nb::self);
}

class GilSafeObject {
 public:
  ~GilSafeObject() {
    nb::gil_scoped_acquire acquire;
    object.reset();
  }

  nb::object object;
};

wdym::TextStream streamPythonTexts(nb::handle iterator) {
  GilSafeObject current;

  while (true) {
    std::string_view text;

    {
      nb::gil_scoped_acquire acquire;
      current.object = nb::steal(PyIter_Next(iterator.ptr()));

      if (!current.object.is_valid()) {
        if (PyErr_Occurred())
          throw nb::python_error();

        co_return;
      }

      // TODO: parametrize hardcoded "sentence_iterator"
      if (!nb::isinstance<nb::str>(current.object))
        throw nb::type_error("sentence_iterator must yield str");

      text = nb::cast<std::string_view>(current.object);
    }

    co_yield text;
  }
}

template <wdym::Tokenizer T>
void bindTrain(nb::class_<T>& cls) {
  using TrainOptions = T::TrainOptions;
  using Settings = T::Settings;
  static constexpr auto TRAIN_OPTION_FIELDS = getFields(^^TrainOptions);
  const TrainOptions defaultTrainOptions{};

  const auto settingsArgument = makeArgument("settings", Settings{});
  const auto normalizationArgument = makeArgument("normalization", wdym::NormalizationSettings{});

  [&]<std::size_t... FieldIndices>(std::index_sequence<FieldIndices...>) {
    cls.def_static(
           "train",
           [](const fs::path& input,
              FieldType<TrainOptions, FieldIndices>... trainOptionValues,
              Settings settings,
              wdym::NormalizationSettings normalization) {
             const TrainOptions trainOptions{trainOptionValues...};

             nb::gil_scoped_release release;

             return T::trainFromFiles(input, trainOptions, settings, normalization);
           },
           nb::kw_only(),
           "input"_a,
           makeFieldArgument<TRAIN_OPTION_FIELDS[FieldIndices]>(defaultTrainOptions)...,
           settingsArgument,
           normalizationArgument
    )
        .def_static(
            "train",
            [](const std::vector<fs::path>& input,
               FieldType<TrainOptions, FieldIndices>... trainOptionValues,
               Settings settings,
               wdym::NormalizationSettings normalization) {
              const TrainOptions trainOptions{trainOptionValues...};

              nb::gil_scoped_release release;

              return T::trainFromFiles(input, trainOptions, settings, normalization);
            },
            nb::kw_only(),
            "input"_a,
            makeFieldArgument<TRAIN_OPTION_FIELDS[FieldIndices]>(defaultTrainOptions)...,
            settingsArgument,
            normalizationArgument
        )
        .def_static(
            "train",
            [](nb::typed<nb::iterable, nb::str> sentenceIterator,
               FieldType<TrainOptions, FieldIndices>... trainOptionValues,
               Settings settings,
               wdym::NormalizationSettings normalization) {
              const TrainOptions trainOptions{trainOptionValues...};
              const nb::object iterator = nb::iter(sentenceIterator);

              nb::gil_scoped_release release;

              return T::train(streamPythonTexts(iterator), trainOptions, settings, normalization);
            },
            nb::kw_only(),
            "sentence_iterator"_a,
            makeFieldArgument<TRAIN_OPTION_FIELDS[FieldIndices]>(defaultTrainOptions)...,
            settingsArgument,
            normalizationArgument
        );
  }(std::make_index_sequence<TRAIN_OPTION_FIELDS.size()>());
}

template <wdym::Tokenizer T>
void bindTokenizer(nb::module_& m) {
  using DecodeOptions = T::DecodeOptions;

  nb::class_<T> cls(m, getIdentifier(^^T));
  bindTrain(cls);

  cls.def_static("load", &T::load, "path"_a)
      .def("save", &T::save, "path"_a)
      .def_static(
          "from_bytes",
          [](const nb::bytes& data) {
            return T::fromBytes(std::span(static_cast<const std::byte*>(data.data()), data.size()));
          },
          "data"_a
      )
      .def(
          "to_bytes",
          [](const T& tokenizer) {
            const std::vector<std::byte> bytes = tokenizer.toBytes();
            return nb::bytes(bytes.data(), bytes.size());
          }
      )
      .def("normalize", &T::normalize, "text"_a)
      .def(
          "encode",
          [](const T& tokenizer, std::string_view text) {
            nb::gil_scoped_release release;
            return tokenizer.encode(text);
          },
          "text"_a
      )
      .def(
          "decode",
          [](const T& tokenizer,
             const std::vector<std::uint32_t>& tokenIds,
             DecodeOptions options) {
            nb::gil_scoped_release release;
            return tokenizer.decode(tokenIds, options);
          },
          "token_ids"_a,
          makeArgument("options", DecodeOptions{})
      )
      .def(
          "token_to_id",
          [](const T& tokenizer, std::string_view token) { return tokenizer.tokenToId(token); },
          "token"_a
      )
      .def(
          "token_to_id",
          [](const T& tokenizer, const nb::bytes& token) {
            return tokenizer.tokenToId(std::string_view(token.c_str(), token.size()));
          },
          "token"_a
      )
      .def(
          "markup_token_to_id",
          [](const T& tokenizer, std::string_view token) {
            return tokenizer.markupTokenToId(token);
          },
          "token"_a
      );
}

NB_MODULE(wdym_ext, m) {
  // See https://docs.python.org/3.15/c-api/exceptions.html for Python C exception APIs
  nb::register_exception_translator([](const std::exception_ptr& exception, void*) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::filesystem::filesystem_error& error) {
      nb::object osError = nb::handle(PyExc_OSError)(
          error.code().value(), error.code().message(), error.path1().string()
      );
      PyErr_SetRaisedException(osError.release().ptr());
    }
  });

  bindStruct<wdym::ReplacementRule>(m);
  bindStruct<wdym::NormalizationStep>(m);
  bindStruct<wdym::KeepInvalidUtf8>(m);
  bindStruct<wdym::NormalizationSettings>(m);
  bindStruct<wdym::MarkupToken>(m);
  bindStruct<wdym::ByteFallback>(m);
  bindStruct<wdym::BytePairEncodingSettings>(m);
  bindStruct<wdym::BytePairEncodingDecodeOptions>(m);

  bindTokenizer<wdym::BytePairEncoding>(m);
}
