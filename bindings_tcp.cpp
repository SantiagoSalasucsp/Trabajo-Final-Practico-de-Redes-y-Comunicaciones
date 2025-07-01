#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "tcp.hpp"

namespace py = pybind11;
using namespace tcp;

PYBIND11_MODULE(tcp_cpp, m) {
    m.def("receive_exact",
          [](int fd, std::size_t n) {
              auto v = receive_exact(fd, n);
              return py::bytes(v.data(), v.size());
          },
          py::arg("fd"), py::arg("n"));

    m.def("send_message",
          [](int fd, py::bytes pydata) {
              std::string tmp = pydata;                 // obtiene los bytes
              std::vector<char> v(tmp.begin(), tmp.end());
              send_all(fd, v);
          },
          py::arg("fd"), py::arg("data"));
}