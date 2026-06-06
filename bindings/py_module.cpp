#include <pybind11/pybind11.h>

#include "core/Engine.hpp"

namespace py = pybind11;

PYBIND11_MODULE(trafficflowviz, m)
{
    m.doc() = "TrafficFlowViz Python bindings";

    // NOTE: minimal Phase 0 binding so the default build (TFV_BUILD_PYTHON=ON)
    // compiles against the real API. Engine has only a default constructor; title
    // and window size come from Configuration. A full Brain/Observation/Action
    // binding is Phase 6 (see docs/agent-simulation-design.md).
    py::class_<tfv::Engine>(m, "Engine")
        .def(py::init<>())
        .def("init", &tfv::Engine::init)
        .def("run", &tfv::Engine::run)
        .def("set_city_csv", &tfv::Engine::setCityInfo)
        .def("set_vehicle_csv", &tfv::Engine::setVehicleInfo);
}
