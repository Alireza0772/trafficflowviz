#include "agents/NNBrain.hpp"

#include "agents/Action.hpp"
#include "agents/Observation.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tfv
{
    namespace
    {
        // Deterministic, portable PRNG (no std distributions, whose impl can vary).
        inline uint64_t splitmix64(uint64_t& s)
        {
            uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }
        inline float uniform01(uint64_t& s)
        {
            return static_cast<float>((splitmix64(s) >> 40) * (1.0 / 16777216.0)); // 24-bit -> [0,1)
        }
        inline int activationCode(const std::string& a)
        {
            if(a == "relu")
                return 1;
            if(a == "identity")
                return 2;
            return 0; // tanh (default)
        }
        inline float applyAct(int code, float v)
        {
            if(code == 1)
                return v > 0.0f ? v : 0.0f; // relu
            if(code == 2)
                return v;                   // identity
            return std::tanh(v);            // tanh
        }
    } // namespace

    NNBrain::NNBrain(std::vector<int> layers, const std::string& activation, uint64_t seed,
                     const IdmParams& idm, float laneThreshold)
        : m_layers(std::move(layers)), m_activation(activationCode(activation)), m_idm(idm),
          m_laneThreshold(laneThreshold), m_seed(seed)
    {
        m_valid = m_layers.size() >= 2 &&
                  m_layers.front() == static_cast<int>(OBS_LEN) && m_layers.back() == 4;
        for(int n : m_layers)
            if(n <= 0)
                m_valid = false;
        if(m_valid)
            initWeights(seed);
    }

    void NNBrain::initWeights(uint64_t seed)
    {
        m_W.clear();
        m_b.clear();
        uint64_t s = seed ^ 0xA5A5A5A5DEADBEEFULL; // per-kind salt
        for(std::size_t L = 0; L + 1 < m_layers.size(); ++L)
        {
            const int in = m_layers[L], out = m_layers[L + 1];
            const float r = 1.0f / std::sqrt(static_cast<float>(in > 0 ? in : 1)); // Xavier-ish
            std::vector<float> W(static_cast<std::size_t>(out) * in);
            std::vector<float> b(static_cast<std::size_t>(out));
            for(float& w : W)
                w = (uniform01(s) * 2.0f - 1.0f) * r;
            for(float& bb : b)
                bb = (uniform01(s) * 2.0f - 1.0f) * r;
            m_W.push_back(std::move(W));
            m_b.push_back(std::move(b));
        }
    }

    void NNBrain::reset(uint64_t seed)
    {
        m_seed = seed;
        if(m_valid)
            initWeights(seed);
    }

    void NNBrain::decideBatch(const Observation* obs, int n, Action* out)
    {
        for(int k = 0; k < n; ++k)
        {
            Action a{};
            if(!m_valid)
            {
                out[k] = a; // degenerate: do nothing (registry should have fallen back to rule)
                continue;
            }
            std::vector<float> x(obs[k].begin(), obs[k].end()); // OBS_LEN inputs
            for(std::size_t L = 0; L + 1 < m_layers.size(); ++L)
            {
                const int in = m_layers[L], out_n = m_layers[L + 1];
                const std::vector<float>& W = m_W[L];
                const std::vector<float>& b = m_b[L];
                std::vector<float> y(static_cast<std::size_t>(out_n));
                for(int o = 0; o < out_n; ++o) // fixed loop order: output-major, input-minor
                {
                    float acc = b[static_cast<std::size_t>(o)];
                    for(int i = 0; i < in; ++i)
                        acc += W[static_cast<std::size_t>(o) * in + i] * x[static_cast<std::size_t>(i)];
                    y[static_cast<std::size_t>(o)] = acc;
                }
                const bool lastLayer = (L + 2 == m_layers.size());
                if(!lastLayer)
                    for(float& v : y)
                        v = applyAct(m_activation, v);
                x.swap(y);
            }
            // Decode the 4-output head -> Action.
            const float t = std::tanh(x[0]);
            a.accel = (t >= 0.0f) ? t * m_idm.a_max : t * m_idm.b_max; // bounded [-b_max, a_max]
            if(!std::isfinite(a.accel))
                a.accel = 0.0f;
            const float lane = std::tanh(x[1]);
            a.laneChange = (lane > m_laneThreshold) ? 1 : (lane < -m_laneThreshold ? -1 : 0);
            a.turn = 0;
            a.lightCmd = (a.accel < -0.5f) ? light::BRAKE : 0; // only BRAKE currently renders
            out[k] = a;
        }
    }

    std::string NNBrain::weightsHash() const
    {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint64_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        for(int l : m_layers)
            mix(static_cast<uint64_t>(l));
        mix(static_cast<uint64_t>(m_activation));
        auto mixF = [&](float f) {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(bits);
        };
        for(const auto& W : m_W)
            for(float w : W)
                mixF(w);
        for(const auto& b : m_b)
            for(float w : b)
                mixF(w);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "nn:%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

} // namespace tfv
