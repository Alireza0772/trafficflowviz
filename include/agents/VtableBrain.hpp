#ifndef TFV_VTABLE_BRAIN_HPP
#define TFV_VTABLE_BRAIN_HPP

#include "agents/Brain.hpp"
#include "agents/tfv_brain.h"

#include <cstddef>
#include <cstdint>

namespace tfv
{
    /**
     * Adapts a loaded C-ABI tfv_brain_vtable (from a DLL / Python / ONNX shim) to the
     * in-process IBrain, so external brains funnel through one place. Built-in brains
     * (rule / nn) are native IBrains and do NOT use this — zero overhead on that path.
     *
     * The caller must have already passed tfv_brain_abi_ok(desc). When `owning`, the
     * vtable's destroy() is invoked on teardown.
     */
    class VtableBrain final : public IBrain
    {
      public:
        VtableBrain(const tfv_brain_desc& desc, const tfv_brain_vtable* vt, void* self, bool owning);
        ~VtableBrain() override;

        VtableBrain(const VtableBrain&) = delete;
        VtableBrain& operator=(const VtableBrain&) = delete;

        void decideBatch(const Observation* obs, int n, Action* out) override;
        void reset(uint64_t seed) override;
        std::string weightsHash() const override;
        const char* kindName() const override { return m_kind.c_str(); }
        bool drivesLaneChange() const override;

      private:
        // True if the brain's vtable is large enough to include the member at byteOffset
        // (optional trailing members are only safe to call when structSize covers them).
        bool covers(std::size_t byteOffset) const;

        const tfv_brain_vtable* m_vt{nullptr};
        void* m_self{nullptr};
        bool m_owning{false};
        std::uint32_t m_structSize{0};
        std::string m_kind;
    };

} // namespace tfv

#endif // TFV_VTABLE_BRAIN_HPP
