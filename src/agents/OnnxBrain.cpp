#include "agents/OnnxBrain.hpp"

#include "utils/LoggingManager.hpp"

#ifdef TFV_WITH_ONNX

#include "agents/Action.hpp"
#include "agents/Observation.hpp"
#include "core/Configuration.hpp"

#include <onnxruntime_cxx_api.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tfv
{
    namespace
    {
        // One process-wide Ort::Env, lazily created and deliberately leaked (Sessions must
        // not outlive the Env; leaking avoids teardown-order fragility), mirroring the
        // PythonBrain interpreter singleton.
        Ort::Env& ortEnv()
        {
            static Ort::Env* env = nullptr;
            static std::once_flag once;
            std::call_once(once, [] { env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "tfv"); });
            return *env;
        }

        // Extract a key=value token from the "?<config>" string (split on '&').
        std::string configValue(const std::string& config, const std::string& key)
        {
            std::size_t pos = 0;
            while(pos < config.size())
            {
                const std::size_t amp = config.find('&', pos);
                const std::string tok =
                    config.substr(pos, amp == std::string::npos ? amp : amp - pos);
                const std::size_t eq = tok.find('=');
                if(eq != std::string::npos && tok.substr(0, eq) == key)
                    return tok.substr(eq + 1);
                if(amp == std::string::npos)
                    break;
                pos = amp + 1;
            }
            return "";
        }

        class OnnxBrain final : public IBrain
        {
          public:
            OnnxBrain(Ort::Session session, std::string inName, std::string outName,
                      bool drivesLane, std::string hash)
                : m_session(std::move(session)), m_inName(std::move(inName)),
                  m_outName(std::move(outName)),
                  m_mem(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
                  m_drivesLane(drivesLane), m_hash(std::move(hash))
            {
            }

            void decideBatch(const Observation* obs, int n, Action* out) override
            {
                for(int i = 0; i < n; ++i)
                    out[i] = Action{}; // pre-zero -> a thrown error leaves safe holds
                if(n <= 0)
                    return;
                try
                {
                    const std::array<int64_t, 2> shape{static_cast<int64_t>(n),
                                                       static_cast<int64_t>(OBS_LEN)};
                    // ORT only READS an input tensor; const_cast avoids a per-tick copy
                    // (the engine retains buffer ownership).
                    Ort::Value input = Ort::Value::CreateTensor<float>(
                        m_mem, const_cast<float*>(obs->data()),
                        static_cast<size_t>(n) * OBS_LEN, shape.data(), shape.size());

                    const char* inNames[] = {m_inName.c_str()};
                    const char* outNames[] = {m_outName.c_str()};
                    auto outputs = m_session.Run(Ort::RunOptions{nullptr}, inNames, &input, 1,
                                                 outNames, 1);

                    auto info = outputs[0].GetTensorTypeAndShapeInfo();
                    const std::vector<int64_t> os = info.GetShape();
                    // Re-validate per tick (a dynamic-axis model reports -1 at load).
                    int64_t rows = 0, K = 0;
                    if(os.size() == 2)
                    {
                        rows = os[0];
                        K = os[1];
                    }
                    else if(os.size() == 1)
                    {
                        rows = os[0];
                        K = 1;
                    }
                    else
                        throw std::runtime_error("onnx output must be 1D or 2D");
                    if(rows != n || K < 1 || K > 6)
                        throw std::runtime_error("onnx output has wrong shape");

                    const float* od = outputs[0].GetTensorData<float>();
                    for(int i = 0; i < n; ++i)
                    {
                        const float* row = od + static_cast<std::size_t>(i) * K;
                        out[i].accel = row[0]; // raw request; validator owns the bounds
                        if(K > 1)
                            out[i].laneChange = static_cast<int8_t>(std::lround(row[1]));
                        if(K > 2)
                            out[i].turn = static_cast<int8_t>(std::lround(row[2]));
                        if(K > 3)
                            out[i].lightCmd = static_cast<uint8_t>(std::lround(row[3]));
                        if(K > 4)
                            out[i].reserved0 = row[4];
                        if(K > 5)
                            out[i].reserved1 = static_cast<int32_t>(std::lround(row[5]));
                    }
                }
                catch(const std::exception& e)
                {
                    if(!m_logged.test_and_set())
                        LOG_ERROR("[brain onnx] inference error (holding): {err}",
                                  PARAM(err, std::string(e.what())));
                    for(int i = 0; i < n; ++i)
                        out[i] = Action{}; // safe holds; the loop survives
                }
            }

            void reset(uint64_t /*seed*/) override {} // stateless inference
            std::string weightsHash() const override { return m_hash; }
            const char* kindName() const override { return "onnx"; }
            bool drivesLaneChange() const override { return m_drivesLane; }

          private:
            Ort::Session m_session;
            std::string m_inName, m_outName;
            Ort::MemoryInfo m_mem;
            bool m_drivesLane{false};
            std::string m_hash{"onnx:unknown"};
            std::atomic_flag m_logged = ATOMIC_FLAG_INIT;
        };

    } // namespace

    std::unique_ptr<IBrain> loadOnnxBrain(const std::string& path, const std::string& config,
                                          const IdmParams& idm)
    {
        (void)idm;
        try
        {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetInterOpNumThreads(1);
            opts.SetExecutionMode(ORT_SEQUENTIAL);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            Ort::Session session(ortEnv(), path.c_str(), opts); // POSIX const char* path

            // Validate the I/O contract: 1 float32 [*,24] input, 1 float32 output.
            if(session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            {
                LOG_ERROR("[brain onnx] {p}: expected exactly 1 input and 1 output",
                          PARAM(p, path));
                return nullptr;
            }
            // Keep the OWNING TypeInfo alive: GetTensorTypeAndShapeInfo() returns a
            // non-owning view into it, so binding to a temporary would dangle (UAF).
            Ort::TypeInfo inType = session.GetInputTypeInfo(0);
            Ort::TypeInfo outType = session.GetOutputTypeInfo(0);
            auto inInfo = inType.GetTensorTypeAndShapeInfo();
            auto outInfo = outType.GetTensorTypeAndShapeInfo();
            if(inInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
               outInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            {
                LOG_ERROR("[brain onnx] {p}: input/output must be float32", PARAM(p, path));
                return nullptr;
            }
            const std::vector<int64_t> inShape = inInfo.GetShape();
            if(inShape.empty() || (inShape.back() != static_cast<int64_t>(OBS_LEN)))
            {
                LOG_ERROR("[brain onnx] {p}: input trailing dim must be 24", PARAM(p, path));
                return nullptr;
            }

            Ort::AllocatorWithDefaultOptions alloc;
            const std::string inName = session.GetInputNameAllocated(0, alloc).get();
            const std::string outName = session.GetOutputNameAllocated(0, alloc).get();

            bool drivesLane = TFV_CONFIG().getInt("sim.onnx.drives_lane_change", 0) != 0;
            const std::string dlc = configValue(config, "drives_lane_change");
            if(!dlc.empty())
                drivesLane = (dlc != "0");

            auto brain = std::make_unique<OnnxBrain>(std::move(session), inName, outName, drivesLane,
                                                     "onnx:" + path);

            if(TFV_CONFIG().getInt("sim.onnx.warmup", 1) != 0) // soft warm-up
            {
                Observation zero{};
                Action a{};
                brain->decideBatch(&zero, 1, &a);
            }
            LOG_INFO("[brain onnx] loaded {p} (in '{in}' out '{out}')", PARAM(p, path),
                     PARAM(in, inName), PARAM(out, outName));
            return brain;
        }
        catch(const std::exception& e)
        {
            LOG_ERROR("[brain onnx] load failed for '{p}': {err}", PARAM(p, path),
                      PARAM(err, std::string(e.what())));
            return nullptr;
        }
    }

} // namespace tfv

#else // TFV_WITH_ONNX not defined: dependency-free stub

namespace tfv
{
    std::unique_ptr<IBrain> loadOnnxBrain(const std::string& path, const std::string& /*config*/,
                                          const IdmParams& /*idm*/)
    {
        LOG_WARN("[brain onnx] support not compiled in (TFV_WITH_ONNX=OFF) for '{p}'; "
                 "falling back to rule",
                 PARAM(p, path));
        return nullptr;
    }
} // namespace tfv

#endif
