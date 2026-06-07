#include "agents/PythonBrain.hpp"

#include "utils/LoggingManager.hpp"

#ifdef TFV_WITH_PYTHON

#include "agents/Action.hpp"
#include "agents/Observation.hpp"
#include "core/Configuration.hpp"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>

namespace py = pybind11;

namespace tfv
{
    namespace
    {
        // One CPython interpreter per process: created lazily on the first Python brain,
        // deliberately LEAKED (never Py_Finalize'd — finalization is fragile with numpy/
        // extension state). If an interpreter already exists (the engine was imported FROM
        // Python via trafficflowviz_py), we do NOT create a second one (that aborts).
        struct InterpreterState
        {
            bool available{false};
        };
        InterpreterState& interpreter()
        {
            static InterpreterState s;
            static std::once_flag once;
            std::call_once(once, [] {
                try
                {
                    if(Py_IsInitialized())
                    {
                        s.available = true; // externally owned; we own nothing, touch no GIL
                    }
                    else
                    {
                        new py::scoped_interpreter();    // leaked on purpose (never finalize)
                        new py::gil_scoped_release();    // leaked: release the GIL the interp holds
                        s.available = true;
                    }
                }
                catch(const std::exception& e)
                {
                    LOG_ERROR("[brain python] interpreter init failed: {err}",
                              PARAM(err, std::string(e.what())));
                    s.available = false;
                }
            });
            return s;
        }

        // Extract a `key=value` token from the "?<config>" string (split on '&').
        std::string configValue(const std::string& config, const std::string& key)
        {
            std::size_t pos = 0;
            while(pos < config.size())
            {
                std::size_t amp = config.find('&', pos);
                std::string tok = config.substr(pos, amp == std::string::npos ? amp : amp - pos);
                const std::size_t eq = tok.find('=');
                if(eq != std::string::npos && tok.substr(0, eq) == key)
                    return tok.substr(eq + 1);
                if(amp == std::string::npos)
                    break;
                pos = amp + 1;
            }
            return "";
        }

        class PythonBrain final : public IBrain
        {
          public:
            PythonBrain(py::module_ mod, py::object fn, bool drivesLane, std::string hash)
                : m_mod(std::move(mod)), m_fn(std::move(fn)), m_drivesLane(drivesLane),
                  m_hash(std::move(hash))
            {
            }
            ~PythonBrain() override
            {
#if PY_VERSION_HEX >= 0x030D0000
                const bool finalizing = Py_IsFinalizing();
#else
                const bool finalizing = _Py_IsFinalizing();
#endif
                if(finalizing)
                {
                    // Interpreter is tearing down (engine imported FROM Python): touching the
                    // GIL/thread state now is unsafe — leak the handles (decref is moot).
                    m_fn.release();
                    m_mod.release();
                    return;
                }
                py::gil_scoped_acquire gil; // drop py handles under the GIL
                m_fn = py::object();
                m_mod = py::module_();
            }

            void decideBatch(const Observation* obs, int n, Action* out) override
            {
                for(int i = 0; i < n; ++i)
                    out[i] = Action{}; // pre-zero -> a thrown error leaves safe holds
                if(n <= 0)
                    return;
                py::gil_scoped_acquire gil;
                try
                {
                    // Zero-copy view over the contiguous [n,24] obs buffer (non-owning base).
                    // The contract forbids mutating it / retaining it past the call.
                    py::array_t<float> view(
                        {static_cast<py::ssize_t>(n), static_cast<py::ssize_t>(OBS_LEN)},
                        {static_cast<py::ssize_t>(OBS_LEN * sizeof(float)),
                         static_cast<py::ssize_t>(sizeof(float))},
                        obs->data(), py::none());
                    // Enforce the read-only contract: a contract-violating in-place write
                    // now raises instead of silently corrupting the engine's obs buffer.
                    py::detail::array_proxy(view.ptr())->flags &=
                        ~py::detail::npy_api::NPY_ARRAY_WRITEABLE_;

                    py::object res = m_fn(view);
                    py::array_t<float, py::array::c_style | py::array::forcecast> a(res);
                    if(a.ndim() == 1)
                    {
                        if(a.shape(0) != n)
                            throw std::runtime_error("decide_batch returned wrong length");
                        auto r = a.unchecked<1>();
                        for(int i = 0; i < n; ++i)
                            out[i].accel = r(i);
                    }
                    else if(a.ndim() == 2)
                    {
                        if(a.shape(0) != n || a.shape(1) < 1)
                            throw std::runtime_error("decide_batch returned wrong shape");
                        const py::ssize_t K = a.shape(1);
                        auto r = a.unchecked<2>();
                        for(int i = 0; i < n; ++i)
                        {
                            out[i].accel = r(i, 0);
                            if(K > 1)
                                out[i].laneChange = static_cast<int8_t>(std::lround(r(i, 1)));
                            if(K > 2)
                                out[i].turn = static_cast<int8_t>(std::lround(r(i, 2)));
                            if(K > 3)
                                out[i].lightCmd = static_cast<uint8_t>(std::lround(r(i, 3)));
                            if(K > 4)
                                out[i].reserved0 = r(i, 4);
                            if(K > 5)
                                out[i].reserved1 = static_cast<int32_t>(std::lround(r(i, 5)));
                        }
                    }
                    else
                        throw std::runtime_error("decide_batch must return a 1D or 2D array");
                }
                catch(const std::exception& e)
                {
                    if(!m_logged.test_and_set())
                        LOG_ERROR("[brain python] decide_batch error (holding): {err}",
                                  PARAM(err, std::string(e.what())));
                    PyErr_Clear();
                    for(int i = 0; i < n; ++i)
                        out[i] = Action{}; // safe holds; the loop survives
                }
            }

            void reset(uint64_t seed) override
            {
                py::gil_scoped_acquire gil;
                try
                {
                    if(py::hasattr(m_mod, "reset"))
                    {
                        py::object fn = m_mod.attr("reset");
                        if(PyCallable_Check(fn.ptr()))
                            fn(static_cast<unsigned long long>(seed));
                    }
                }
                catch(const std::exception&)
                {
                    PyErr_Clear(); // reset is best-effort
                }
            }

            std::string weightsHash() const override { return m_hash; }
            const char* kindName() const override { return "python"; }
            bool drivesLaneChange() const override { return m_drivesLane; }

          private:
            py::module_ m_mod;
            py::object m_fn;
            bool m_drivesLane{false};
            std::string m_hash{"python:unknown"};
            std::atomic_flag m_logged = ATOMIC_FLAG_INIT;
        };

    } // namespace

    std::unique_ptr<IBrain> loadPythonBrain(const std::string& module, const std::string& func,
                                            const std::string& config, const IdmParams& idm)
    {
        (void)idm;
        if(!interpreter().available)
        {
            LOG_ERROR("[brain python] no usable interpreter");
            return nullptr;
        }
        py::gil_scoped_acquire gil;
        try
        {
            // Extra sys.path dir: ?path=<dir> in the kind, else the sim.python.path config.
            std::string extra = configValue(config, "path");
            if(extra.empty())
                extra = TFV_CONFIG().getString("sim.python.path", "");
            if(!extra.empty())
                py::module_::import("sys").attr("path").attr("insert")(0, extra);

            const std::string fname = func.empty() ? "decide_batch" : func;
            py::module_ mod = py::module_::import(module.c_str());
            py::object fn = mod.attr(fname.c_str());
            if(!PyCallable_Check(fn.ptr()))
            {
                LOG_ERROR("[brain python] {m}.{f} is not callable", PARAM(m, module),
                          PARAM(f, fname));
                return nullptr;
            }

            bool drivesLane = TFV_CONFIG().getInt("sim.python.drives_lane_change", 0) != 0;
            if(py::hasattr(mod, "drives_lane_change"))
            {
                try
                {
                    drivesLane = mod.attr("drives_lane_change").cast<bool>();
                }
                catch(...)
                {
                }
            }

            std::string hash = "py:" + module;
            if(py::hasattr(mod, "weights_hash"))
            {
                try
                {
                    hash = "py:" + mod.attr("weights_hash")().cast<std::string>();
                }
                catch(...)
                {
                }
            }

            auto brain = std::make_unique<PythonBrain>(std::move(mod), std::move(fn), drivesLane,
                                                       std::move(hash));
            // Warm-up decode to surface shape/dtype errors at load (failures are logged +
            // safe; the brain still loads and will hold until the model is fixed).
            if(TFV_CONFIG().getInt("sim.python.warmup", 1) != 0)
            {
                Observation zero{};
                Action a{};
                brain->decideBatch(&zero, 1, &a);
            }
            LOG_INFO("[brain python] loaded {m}.{f}", PARAM(m, module), PARAM(f, fname));
            return brain;
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("[brain python] load failed for '{m}': {err}", PARAM(m, module),
                      PARAM(err, std::string(e.what())));
            PyErr_Clear();
            return nullptr;
        }
    }

} // namespace tfv

#else // TFV_WITH_PYTHON not defined: dependency-free stub

namespace tfv
{
    std::unique_ptr<IBrain> loadPythonBrain(const std::string& module, const std::string& /*func*/,
                                            const std::string& /*config*/, const IdmParams& /*idm*/)
    {
        LOG_WARN("[brain python] support not compiled in (TFV_WITH_PYTHON=OFF) for '{m}'; "
                 "falling back to rule",
                 PARAM(m, module));
        return nullptr;
    }
} // namespace tfv

#endif
