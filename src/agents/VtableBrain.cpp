#include "agents/VtableBrain.hpp"

#include "agents/Action.hpp"

#include <cstddef>
#include <type_traits>

namespace tfv
{
    // The reinterpret_cast in decideBatch + flat-buffer indexing are sound only if the C
    // POD is layout-identical AND trivially-copyable. Enforce at compile time; bump
    // TFV_BRAIN_ACT_LAYOUT if this changes.
    static_assert(sizeof(tfv_action) == sizeof(Action), "tfv_action must match tfv::Action size");
    static_assert(alignof(tfv_action) == alignof(Action), "tfv_action must match alignment");
    static_assert(std::is_standard_layout<Action>::value, "Action must be standard-layout");
    static_assert(std::is_trivially_copyable<Action>::value, "Action must be trivially copyable");
    static_assert(offsetof(tfv_action, accel) == offsetof(Action, accel), "accel offset");
    static_assert(offsetof(tfv_action, laneChange) == offsetof(Action, laneChange), "lane offset");
    static_assert(offsetof(tfv_action, turn) == offsetof(Action, turn), "turn offset");
    static_assert(offsetof(tfv_action, lightCmd) == offsetof(Action, lightCmd), "light offset");
    static_assert(offsetof(tfv_action, reserved0) == offsetof(Action, reserved0), "r0 offset");
    static_assert(offsetof(tfv_action, reserved1) == offsetof(Action, reserved1), "r1 offset");

    VtableBrain::VtableBrain(const tfv_brain_desc& desc, const tfv_brain_vtable* vt, void* self,
                             bool owning)
        : m_vt(vt), m_self(self), m_owning(owning), m_structSize(vt ? vt->structSize : 0),
          m_kind(desc.kindName ? desc.kindName : "vtable")
    {
    }

    bool VtableBrain::covers(std::size_t byteOffset) const
    {
        // A trailing member is present only if the brain-provided vtable is large enough
        // to include its slot (guards against an older/shorter externally-built vtable).
        return m_structSize >= byteOffset + sizeof(void (*)());
    }

    VtableBrain::~VtableBrain()
    {
        if(m_owning && m_vt && m_vt->destroy)
            m_vt->destroy(m_self);
    }

    void VtableBrain::decideBatch(const Observation* obs, int n, Action* out)
    {
        if(!m_vt || !m_vt->decide_batch || n <= 0)
            return;
        // Observations and Actions are contiguous PODs; pass the flat buffers across.
        m_vt->decide_batch(m_self, obs->data(), n, static_cast<int>(OBS_LEN),
                           reinterpret_cast<tfv_action*>(out));
    }

    void VtableBrain::reset(uint64_t seed)
    {
        if(m_vt && covers(offsetof(tfv_brain_vtable, reset)) && m_vt->reset)
            m_vt->reset(m_self, seed);
    }

    std::string VtableBrain::weightsHash() const
    {
        if(m_vt && covers(offsetof(tfv_brain_vtable, weights_hash)) && m_vt->weights_hash)
            if(const char* h = m_vt->weights_hash(m_self))
                return h;
        return "vtable:unknown";
    }

    bool VtableBrain::drivesLaneChange() const
    {
        return m_vt && covers(offsetof(tfv_brain_vtable, drives_lane_change)) &&
               m_vt->drives_lane_change && m_vt->drives_lane_change(m_self) != 0;
    }

} // namespace tfv
