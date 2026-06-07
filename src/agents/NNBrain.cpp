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
        // Per-bank-member seed. Member 0 == the original seed (so a 1-member bank
        // reproduces today's single weight set bit-for-bit); members >0 are distinct.
        inline uint64_t memberSeed(uint64_t seed, int m)
        {
            if(m == 0)
                return seed;
            uint64_t s = seed + static_cast<uint64_t>(m) * 0x9E3779B97F4A7C15ULL;
            return splitmix64(s);
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
                     const IdmParams& idm, float laneThreshold, int population)
        : m_layers(std::move(layers)), m_activation(activationCode(activation)),
          m_population(population < 1 ? 1 : (population > 256 ? 256 : population)), m_idm(idm),
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
        m_W.assign(static_cast<std::size_t>(m_population), {});
        m_b.assign(static_cast<std::size_t>(m_population), {});
        for(int member = 0; member < m_population; ++member)
        {
            uint64_t s = memberSeed(seed, member) ^ 0xA5A5A5A5DEADBEEFULL; // per-kind salt
            auto& Wm = m_W[static_cast<std::size_t>(member)];
            auto& bm = m_b[static_cast<std::size_t>(member)];
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
                Wm.push_back(std::move(W));
                bm.push_back(std::move(b));
            }
        }
    }

    void NNBrain::reset(uint64_t seed)
    {
        m_seed = seed;
        if(m_valid)
            initWeights(seed);
    }

    void NNBrain::forwardOne(int member, const Observation& obs, Action& out) const
    {
        Action a{};
        const auto& W_ = m_W[static_cast<std::size_t>(member)];
        const auto& b_ = m_b[static_cast<std::size_t>(member)];
        std::vector<float> x(obs.begin(), obs.end()); // OBS_LEN inputs
        for(std::size_t L = 0; L + 1 < m_layers.size(); ++L)
        {
            const int in = m_layers[L], out_n = m_layers[L + 1];
            const std::vector<float>& W = W_[L];
            const std::vector<float>& b = b_[L];
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
        out = a;
    }

    void NNBrain::decideBatch(const Observation* obs, int n, Action* out)
    {
        // No ids -> bank member 0 (the shared/default net).
        for(int k = 0; k < n; ++k)
        {
            if(!m_valid)
            {
                out[k] = Action{}; // degenerate (registry should have fallen back to rule)
                continue;
            }
            forwardOne(0, obs[k], out[k]);
        }
    }

    void NNBrain::decideBatchIds(const Observation* obs, const uint64_t* ids, int n, Action* out)
    {
        for(int k = 0; k < n; ++k)
        {
            if(!m_valid)
            {
                out[k] = Action{};
                continue;
            }
            uint64_t s = ids[k]; // deterministic id -> bank member (modulo bias acceptable)
            const int member =
                static_cast<int>(splitmix64(s) % static_cast<uint64_t>(m_population));
            forwardOne(member, obs[k], out[k]);
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
        if(m_population > 1)
            mix(static_cast<uint64_t>(m_population)); // K folded in only when >1 (K=1 string stable)
        auto mixF = [&](float f) {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(bits);
        };
        for(int member = 0; member < m_population; ++member)
        {
            for(const auto& W : m_W[static_cast<std::size_t>(member)])
                for(float w : W)
                    mixF(w);
            for(const auto& b : m_b[static_cast<std::size_t>(member)])
                for(float w : b)
                    mixF(w);
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "nn:%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

} // namespace tfv
