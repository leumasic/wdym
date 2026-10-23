#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <exception>
#include <filesystem>
#include <generator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

import wdym;

namespace nb = nanobind;
using namespace nb::literals;
namespace fs = std::filesystem;

using wdym::ByteLevelBPETokenizer;
using wdym::ByteLevelBPETrainOptions;

template <typename Options, typename Value>
struct TrainOption {
  const char* name;
  Value Options::*field;
};

template <typename Options, typename Value>
TrainOption<Options, Value> option(const char* name, Value Options::*field) {
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

wdym::TextStream stream_python_texts(nb::handle iterator) {
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
           return T::train(std::span(&input, 1), trainOptions);
         },
         nb::kw_only(), "input"_a, (nb::arg(options.name) = defaults.*(options.field))...
  )
      .def_static(
          "train",
          [make_options](const std::vector<fs::path>& input, Values... values) {
            const Options trainOptions = make_options(values...);
            nb::gil_scoped_release release;
            return T::train(input, trainOptions);
          },
          nb::kw_only(), "input"_a, (nb::arg(options.name) = defaults.*(options.field))...
      )
      .def_static(
          "train",
          [make_options](nb::typed<nb::iterable, nb::str> sentence_iterator, Values... values) {
            const Options trainOptions = make_options(values...);
            const nb::object iterator = nb::iter(sentence_iterator);
            nb::gil_scoped_release release;
            return T::train(stream_python_texts(iterator), trainOptions);
          },
          nb::kw_only(), "sentence_iterator"_a,
          (nb::arg(options.name) = defaults.*(options.field))...
      );
}

template <wdym::Tokenizer T, typename... Values>
void bind_tokenizer(nb::module_& m, TrainOption<typename T::TrainOptions, Values>... options) {
  static_assert(
      !wdym::tokenizer_name<T>.empty(), "specialize wdym::tokenizer_name for this tokenizer"
  );
  nb::class_<T> cls(m, wdym::tokenizer_name<T>.data());
  cls.def_static("from_file", &T::from_file, "path"_a).def("save", &T::save, "path"_a);
  bind_train(cls, options...);
}

NB_MODULE(wdym_ext, m) {
  nb::register_exception_translator([](const std::exception_ptr& exception, void*) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::filesystem::filesystem_error& error) {
      const nb::object os_error = nb::handle(PyExc_OSError)(
          error.code().value(), error.code().message(), error.path1().string()
      );
      PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(os_error.ptr())), os_error.ptr());
    }
  });

  bind_tokenizer<ByteLevelBPETokenizer>(
      m, option("vocab_size", &ByteLevelBPETrainOptions::vocab_size)
  );
}
