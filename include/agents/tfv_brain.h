#ifndef TFV_BRAIN_H
#define TFV_BRAIN_H

/*
 * tfv_brain.h — the stable C-ABI contract every external brain implements.
 *
 * A brain maps a fixed-length Observation to an Action. This pure-C header is the
 * versioned vtable that DLL / embedded-Python / ONNX backends expose and that the
 * engine loads behind the in-process C++ IBrain (see VtableBrain). It deliberately
 * uses only <stdint.h>/<stddef.h> so it is loadable from any language.
 *
 * Compatibility is a HARD gate: a brain whose abiVersion / obsLen / actLayout do
 * not match is REFUSED (never decide_batch'd) and the engine falls back to "rule".
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Bump abiVersion on any vtable/contract change; actLayout on any Action change. */
#define TFV_BRAIN_ABI_VERSION 1u
#define TFV_BRAIN_OBS_LEN 24u  /* MUST equal tfv::OBS_LEN */
#define TFV_BRAIN_ACT_LAYOUT 1u

    /* POD mirror of tfv::Action with EXPLICIT layout (field order/types must match;
     * natural padding is left to the compiler and asserted on the C++ side).
     * NOTE: there are 2 padding bytes at offsets 6-7 (after lightCmd, before
     * reserved0). They are NOT part of the value — compare/serialize field-wise,
     * never memcmp the whole struct. */
    typedef struct tfv_action
    {
        float accel;
        int8_t laneChange; /* -1 left, 0 none, +1 right */
        int8_t turn;       /* 0 straight, -1 left, +1 right, 2 u-turn */
        uint8_t lightCmd;  /* light:: bitfield */
        float reserved0;
        int32_t reserved1;
    } tfv_action;

    /* Self-describing descriptor, filled by the brain and read by the loader
     * BEFORE any decide call. structSize is a forward-compatible size gate. */
    typedef struct tfv_brain_desc
    {
        uint32_t abiVersion;
        uint32_t obsLen;
        uint32_t actLayout;
        uint32_t structSize;
        const char* kindName; /* static string, brain-owned */
    } tfv_brain_desc;

    /* The vtable. `self` is opaque brain state. decide_batch is MANDATORY; the rest
     * may be NULL (weights_hash / drives_lane_change are optional). */
    typedef struct tfv_brain_vtable
    {
        uint32_t structSize; /* sizeof — version/size gate */
        void (*destroy)(void* self);
        void (*decide_batch)(void* self, const float* obs, int n, int obsLen, tfv_action* out);
        void (*reset)(void* self, uint64_t seed);
        const char* (*weights_hash)(void* self);  /* may be NULL */
        int (*drives_lane_change)(void* self);     /* 0/1; NULL => 0 */
    } tfv_brain_vtable;

    /* Factory a backend exports as the C symbol "tfv_brain_create". `config` is an
     * opaque key=val string the engine passes through from the kind suffix.
     * Returns 0 on success; nonzero = creation failed (engine falls back to rule). */
    typedef int (*tfv_brain_create_fn)(const char* config, tfv_brain_desc* outDesc,
                                       const tfv_brain_vtable** outVtable, void** outSelf);

    /* The HARD compatibility gate every loader calls before trusting a brain. */
    static inline int tfv_brain_abi_ok(const tfv_brain_desc* d)
    {
        return d && d->abiVersion == TFV_BRAIN_ABI_VERSION && d->obsLen == TFV_BRAIN_OBS_LEN &&
               d->actLayout == TFV_BRAIN_ACT_LAYOUT && d->structSize >= sizeof(tfv_brain_desc);
    }

    /* Vtable size gate: the brain must provide at least through decide_batch (the
     * mandatory member). Optional trailing members are only safe to call when
     * structSize covers them — see VtableBrain. The contract is append-only: any ABI
     * growth only appends members and bumps abiVersion. */
    static inline int tfv_brain_vtable_ok(const tfv_brain_vtable* vt)
    {
        return vt && vt->structSize >= offsetof(tfv_brain_vtable, reset) && vt->decide_batch != 0;
    }

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TFV_BRAIN_H
