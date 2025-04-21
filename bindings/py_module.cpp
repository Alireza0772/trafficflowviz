#include <pybind11/pybind11.h>

#include "core/Engine.hpp"

namespace py = pybind11;

PYBIND11_MODULE(trafficflowviz, m)
{
    m.doc() = "TrafficFlowViz Python bindings";

    py::class_<tfv::Engine>(m, "Engine")
        .def(py::init<const std::string&, int, int>())
        .def("init", &tfv::Engine::init)
        .def("run", &tfv::Engine::run)
        .def("set_csv", &tfv::Engine::setCSV)
        .def("set_road_csv", &tfv::Engine::setRoadCSV);
}
