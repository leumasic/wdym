#include "tokenizer.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

NB_MODULE(wdym_ext, m) {
  nb::class_<wdym::ByteLevelBPETokenizer>(m, "ByteLevelBPETokenizer")
      .def(nb::init<>())
      .def("train", &wdym::ByteLevelBPETokenizer::train);
}
