/*
 * example_brain.c — a reference external brain implementing tfv_brain.h.
 *
 * Build it as a shared module and load it with:  sim.default_brain = dll:<path-to-.so>
 * (CMake builds it as the `tfv_example_brain` MODULE target.)
 *
 * This "cruise" brain steers each vehicle's speed toward the observed speed limit with
 * a simple proportional law and never changes lanes — just enough to demonstrate the
 * Observation -> Action contract a scientist's trained model would also satisfy.
 */

#include "agents/tfv_brain.h"

#define OBS_SELF_SPEED 0  /* o[0] = speed / OBS_SPEED_SCALE */
#define OBS_SPEED_LIMIT 4 /* o[4] = effective speed limit / OBS_SPEED_SCALE */
#define OBS_SPEED_SCALE 40.0f

#if defined(_WIN32)
#define TFV_EXPORT __declspec(dllexport)
#else
#define TFV_EXPORT __attribute__((visibility("default")))
#endif

static void cb_destroy(void* self) { (void)self; }

static void cb_decide(void* self, const float* obs, int n, int obsLen, tfv_action* out)
{
    (void)self;
    for(int k = 0; k < n; ++k)
    {
        const float* o = obs + (size_t)k * obsLen;
        const float speed = o[OBS_SELF_SPEED] * OBS_SPEED_SCALE;
        const float v0 = o[OBS_SPEED_LIMIT] * OBS_SPEED_SCALE;
        float a = (v0 - speed) * 0.4f; /* proportional cruise control */
        if(a > 1.5f)
            a = 1.5f;
        if(a < -6.0f)
            a = -6.0f;

        tfv_action act;
        act.accel = a;
        act.laneChange = 0;
        act.turn = 0;
        act.lightCmd = (a < -0.5f) ? 1u : 0u; /* light::BRAKE */
        act.reserved0 = 0.0f;
        act.reserved1 = 0;
        out[k] = act;
    }
}

static void cb_reset(void* self, uint64_t seed)
{
    (void)self;
    (void)seed;
}
static const char* cb_weights_hash(void* self)
{
    (void)self;
    return "example-cruise:v1";
}
static int cb_drives_lane_change(void* self)
{
    (void)self;
    return 0;
}

static const tfv_brain_vtable kVtable = {
    sizeof(tfv_brain_vtable), cb_destroy, cb_decide, cb_reset, cb_weights_hash,
    cb_drives_lane_change};

TFV_EXPORT int tfv_brain_create(const char* config, tfv_brain_desc* outDesc,
                                const tfv_brain_vtable** outVtable, void** outSelf)
{
    (void)config;
    if(!outDesc || !outVtable || !outSelf)
        return 1;
    outDesc->abiVersion = TFV_BRAIN_ABI_VERSION;
    outDesc->obsLen = TFV_BRAIN_OBS_LEN;
    outDesc->actLayout = TFV_BRAIN_ACT_LAYOUT;
    outDesc->structSize = (uint32_t)sizeof(tfv_brain_desc);
    outDesc->kindName = "example-cruise";
    *outVtable = &kVtable;
    *outSelf = (void*)0; /* stateless brain */
    return 0;
}
