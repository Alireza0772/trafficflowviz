#include "io/RunManifest.hpp"

#include <cstdio>
#include <string>

namespace tfv
{
    namespace
    {
        std::string esc(const std::string& s) // minimal JSON string escaping
        {
            std::string o;
            o.reserve(s.size() + 2);
            for(char c : s)
            {
                switch(c)
                {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\t': o += "\\t"; break;
                case '\r': o += "\\r"; break;
                default:
                    if(static_cast<unsigned char>(c) < 0x20) // other control chars -> \u00XX
                    {
                        char u[8];
                        std::snprintf(u, sizeof(u), "\\u%04x", static_cast<unsigned char>(c));
                        o += u;
                    }
                    else
                        o += c;
                    break;
                }
            }
            return o;
        }
        std::string fnum(double v) // locale-independent number
        {
            char b[64];
            std::snprintf(b, sizeof(b), "%.6g", v);
            return b;
        }
    } // namespace

    void RunManifest::write(std::ostream& os) const
    {
        auto q = [&](const std::string& s) { return '"' + esc(s) + '"'; };
        os << "{\n";
        os << "  \"schema_version\": " << schemaVersion << ",\n";
        os << "  \"master_seed\": " << masterSeed << ",\n";
        os << "  \"brain_kind\": " << q(brainKind) << ",\n";
        os << "  \"brain_weights_hash\": " << q(brainWeightsHash) << ",\n";
        os << "  \"city_file\": " << q(cityFile) << ",\n";
        os << "  \"vehicles_file\": " << q(vehiclesFile) << ",\n";
        os << "  \"signs_file\": " << q(signsFile) << ",\n";
        os << "  \"vehicle_count\": " << vehicleCount << ",\n";
        os << "  \"decision_hz\": " << fnum(decisionHz) << ",\n";
        os << "  \"fixed_dt\": " << fnum(fixedDt) << ",\n";
        os << "  \"total_ticks\": " << totalTicks << ",\n";
        os << "  \"export_every_n\": " << exportEveryN << ",\n";
        os << "  \"git_sha\": " << q(gitSha) << ",\n";
        os << "  \"wall_clock_utc\": " << q(wallClockUtc) << ",\n";
        os << "  \"final_state_digest\": \"0x";
        char d[24];
        std::snprintf(d, sizeof(d), "%016llx", static_cast<unsigned long long>(finalStateDigest));
        os << d << "\",\n";
        os << "  \"config\": {";
        bool first = true;
        for(const auto& [k, v] : configSnapshot)
        {
            os << (first ? "\n" : ",\n") << "    " << q(k) << ": " << q(v);
            first = false;
        }
        os << (first ? "" : "\n  ") << "}\n";
        os << "}\n";
    }

} // namespace tfv
