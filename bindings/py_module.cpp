#include <pybind11/pybind11.h>

#include <memory>

#include "Engine.hpp"
namespace py = pybind11;
PYBIND11_MODULE(trafficviz, m)
{
    py::class_<tfv::Engine, std::shared_ptr<tfv::Engine>>(m, "Engine")
        .def(py::init<const std::string&, int, int>())
        .def("run", &tfv::Engine::run);
}
