#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

import std;
import wdym;

namespace nb = nanobind;
using namespace nb::literals;
namespace fs = std::filesystem;

using wdym::ByteLevelBPETokenizer;
using wdym::ByteLevelBPETrainOptions;

template <typename Options, typename Value>
struct TrainOption {
  const char* name;
  Value Options::* field;
};

template <typename Options, typename Value>
TrainOption<Options, Value> option(const char* name, Value Options::* field) {
  return {name, field};
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
      if (!nb::isinstance<nb::str>(current.object))
        throw nb::type_error("sentence_iterator must yield str");
      text = nb::cast<std::string_view>(current.object);
    }
    co_yield text;
  }
}

template <wdym::Tokenizer T, typename... Values>
void bind_train(nb::class_<T>& cls, TrainOption<typename T::TrainOptions, Values>... options) {
  using Options = T::TrainOptions;
  const Options defaults{};

  const auto make_options = [options...](const Values&... values) {
    Options result{};
    ((result.*(options.field) = values), ...);
    return result;
  };

  cls.def_static(
         "train",
         [make_options](const fs::path& input, Values... values) {
           const Options trainOptions = make_options(values...);
           nb::gil_scoped_release release;
           return T::train_from_files(input, trainOptions);
         },
         nb::kw_only(),
         "input"_a,
         (nb::arg(options.name) = defaults.*(options.field))...
  )
      .def_static(
          "train",
          [make_options](const std::vector<fs::path>& input, Values... values) {
            const Options trainOptions = make_options(values...);
            nb::gil_scoped_release release;
            return T::train_from_files(input, trainOptions);
          },
          nb::kw_only(),
          "input"_a,
          (nb::arg(options.name) = defaults.*(options.field))...
      )
      .def_static(
          "train",
          [make_options](nb::typed<nb::iterable, nb::str> sentence_iterator, Values... values) {
            const Options trainOptions = make_options(values...);
            const nb::object iterator = nb::iter(sentence_iterator);
            nb::gil_scoped_release release;
            return T::train(streamPythonTexts(iterator), trainOptions);
          },
          nb::kw_only(),
          "sentence_iterator"_a,
          (nb::arg(options.name) = defaults.*(options.field))...
      );
}

template <wdym::Tokenizer T, typename... Values>
void bind_tokenizer(nb::module_& m, TrainOption<typename T::TrainOptions, Values>... options) {
  static_assert(
      !wdym::tokenizer_name<T>.empty(), "specialize wdym::tokenizer_name for this tokenizer"
  );
  nb::class_<T> cls(m, wdym::tokenizer_name<T>.data());
  cls.def_static("load", &T::load, "path"_a).def("save", &T::save, "path"_a);
  bind_train(cls, options...);
}

void bind_normalizer(nb::module_& m) {
  nb::class_<wdym::ReplacementRule>(m, "ReplacementRule")
      .def(nb::init<>())
      .def_rw("input", &wdym::ReplacementRule::input)
      .def_rw("output", &wdym::ReplacementRule::output);

  nb::class_<wdym::NormalizationStep>(m, "NormalizationStep")
      .def(nb::init<>())
      .def_rw("name", &wdym::NormalizationStep::name)
      .def_rw("rules", &wdym::NormalizationStep::rules);

  nb::class_<wdym::KeepInvalidUtf8>(m, "KeepInvalidUtf8").def(nb::init<>());

  nb::class_<wdym::NormalizationSettings>(m, "NormalizationSettings")
      .def(nb::init<>())
      .def_rw("invalid_utf8_fallback", &wdym::NormalizationSettings::invalid_utf8_fallback)
      .def_rw("steps", &wdym::NormalizationSettings::steps);
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

  bind_normalizer(m);

  bind_tokenizer<ByteLevelBPETokenizer>(
      m, option("max_vocabulary_size", &ByteLevelBPETrainOptions::max_vocabulary_size)
  );
}
