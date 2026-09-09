#include "fobj.h"

#include <string.h>

#include "debug.h"
#include "spline.h"

HSD_ObjAllocData fobj_alloc_data;

HSD_ObjAllocData* HSD_FObjGetAllocData(void)
{
    return &fobj_alloc_data;
}

void HSD_FObjInitAllocData(void)
{
    HSD_ObjAllocInit(HSD_FObjGetAllocData(), sizeof(HSD_FObj), 4);
}

void HSD_FObjRemove(HSD_FObj* fobj)
{
    if (!fobj) {
        return;
    }

    HSD_FObjFree(fobj);
}

void HSD_FObjRemoveAll(HSD_FObj* fobj)
{
    if (fobj == NULL) {
        return;
    }
    HSD_FObjRemoveAll(fobj->next);
    HSD_FObjRemove(fobj);
}

u32 HSD_FObjSetState(HSD_FObj* fobj, u32 state)
{
    if (fobj) {
        fobj->flags = (state & 0xF) | (fobj->flags & 0xF0);
    }
    return state;
}

u32 HSD_FObjGetState(HSD_FObj* fobj)
{
    if (!fobj) {
        return 0;
    }
    return fobj->flags & 0xF;
}

static inline void HSD_FObjReqAnim(HSD_FObj* fobj, f32 startframe)
{
    if (fobj == NULL) {
        return;
    }

    fobj->ad = fobj->ad_head;
    fobj->time = (f32) fobj->startframe + startframe;
    fobj->op = HSD_A_OP_NONE;
    fobj->op_intrp = HSD_A_OP_NONE;
    fobj->flags &= ~0x40;
    fobj->nb_pack = 0;
    fobj->fterm = 0;
    fobj->p0 = 0.f;
    fobj->p1 = 0.f;
    fobj->d0 = 0.f;
    fobj->d1 = 0.f;
    HSD_FObjSetState(fobj, FOBJ_LOAD_DATA0);
}

void HSD_FObjReqAnimAll(HSD_FObj* fobj, f32 startframe)
{
    HSD_FObj* track;

    if (fobj == NULL) {
        return;
    }

    for (track = fobj; track != NULL; track = track->next) {
        HSD_FObjReqAnim(track, startframe);
    }
}

static inline void FObj_FlushKeyData(HSD_FObj* fobj, void* obj,
                                     HSD_ObjUpdateFunc obj_update, f32 rate)
{
    if (fobj->op_intrp == HSD_A_OP_KEY) {
        HSD_FObjInterpretAnim(fobj, obj, obj_update, rate);
    }
}

void HSD_FObjStopAnim(HSD_FObj* fobj, void* obj, HSD_ObjUpdateFunc obj_update,
                      f32 rate)
{
    if (fobj == NULL) {
        return;
    }

    FObj_FlushKeyData(fobj, obj, obj_update, rate);
    HSD_FObjSetState(fobj, 0);
}

void HSD_FObjStopAnimAll(HSD_FObj* fobj, void* obj,
                         HSD_ObjUpdateFunc obj_update, f32 rate)
{
    for (; fobj != NULL; fobj = fobj->next) {
        HSD_FObjStopAnim(fobj, obj, obj_update, rate);
    }
}

/* Stream values store the least significant byte first. */
static f32 parseFloat(u8** cursor, u8 format)
{
    union {
        f32 value;
        u32 bits;
    } decoded;
    f32 raw_value;
    s32 scale_divisor;

    if (format == HSD_A_FRAC_FLOAT) {
        decoded.bits = (u32) ((*cursor)++)[0];
        decoded.bits |= (u32) ((*cursor)++)[0] << 8;
        decoded.bits |= (u32) ((*cursor)++)[0] << 16;
        decoded.bits |= (u32) ((*cursor)++)[0] << 24;
        return decoded.value;
    }

    scale_divisor = (1 << (format & 0x1F));
    switch (format & 0xE0) {
    case HSD_A_FRAC_S8:
        raw_value = (s8) (*cursor)[0];
        *cursor += 1;
        break;
    case HSD_A_FRAC_U8:
        raw_value = (*cursor)[0];
        *cursor += 1;
        break;
    case HSD_A_FRAC_S16:
        raw_value = (s16) (((u16) (*cursor)[1] << 8) | (*cursor)[0]);
        *cursor += 2;
        break;
    case HSD_A_FRAC_U16:
        raw_value = ((*cursor)[1] << 8) | (*cursor)[0];
        *cursor += 2;
        break;
    default:
        return 0.0f;
    }
    return raw_value / scale_divisor;
}

/* The opcode shares a byte with the repeat count. Leave it for that parser. */
static u8 parseOpCode(u8** cursor)
{
    return (**cursor) & 0xF;
}

/* Decode count minus one: three header bits, then seven bits per byte. */
static u32 parsePackInfo(u8** cursor)
{
    u8 encoded_byte;
    u32 repeat_count;
    s32 shift;

    encoded_byte = *(*cursor)++;
    repeat_count = ((encoded_byte >> 4) & 7) + 1;
    shift = 3;
    if (!(encoded_byte & 0x80)) {
        return repeat_count;
    }
    do {
        encoded_byte = *(*cursor)++;
        repeat_count += (encoded_byte & 0x7F) << shift;
        shift += 7;
    } while (encoded_byte & 0x80);
    return repeat_count;
}

static void FObjLaunchKeyData(HSD_FObj* fobj)
{
    if ((fobj->flags & 0x40) != 0) {
        fobj->op_intrp = fobj->op;
        fobj->flags &= ~0x40;
        fobj->flags |= 0x80;
        fobj->p0 = fobj->p1;
    }
}

/* Read seven-bit value groups, least significant first, until bit 7 clears. */
static s32 parseWait(u8** cursor)
{
    u8 encoded_byte;
    s32 wait_frames = 0;
    s32 shift = 0;

    do {
        encoded_byte = *(*cursor)++;
        wait_frames |= (encoded_byte & 0x7f) << shift;
        shift += 7;
    } while (encoded_byte & 0x80);

    return wait_frames;
}

static u32 FObjLoadWait(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x16C, st == FOBJ_LOAD_WAIT);

    if ((unsigned) (fobj->ad - fobj->ad_head) >= fobj->length) {
        return 6;
    } else {
        fobj->fterm = parseWait(&fobj->ad);
        fobj->flags |= 0x20;
        return HSD_FObjSetState(fobj, FOBJ_LOAD_DATA);
    }
}

static u32 FObjAnimCON(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x17F, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    fobj->p0 = fobj->p1;
    fobj->p1 = parseFloat(&fobj->ad, fobj->frac_value);
    if (fobj->op_intrp != HSD_A_OP_SLP) {
        fobj->d0 = fobj->d1;
        fobj->d1 = 0.0F;
    }

    return HSD_FObjSetState(fobj, st == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static u32 FObjAnimLinear(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x193, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    fobj->p0 = fobj->p1;
    fobj->p1 = parseFloat(&fobj->ad, fobj->frac_value);
    if (fobj->op_intrp != HSD_A_OP_SLP) {
        fobj->d0 = fobj->d1;
        fobj->d1 = 0.0F;
    }

    return HSD_FObjSetState(fobj, st == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static u32 FObjAnimSPL0(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x1A7, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    fobj->p0 = fobj->p1;
    fobj->d0 = fobj->d1;
    fobj->p1 = parseFloat(&fobj->ad, fobj->frac_value);
    fobj->d1 = 0.0F;

    return HSD_FObjSetState(fobj, st == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static u32 FObjAnimSPL(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x1B9, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    fobj->p0 = fobj->p1;
    fobj->p1 = parseFloat(&fobj->ad, fobj->frac_value);
    fobj->d0 = fobj->d1;
    fobj->d1 = parseFloat(&fobj->ad, fobj->frac_slope);

    return HSD_FObjSetState(fobj, st == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static u32 FObjAnimSLP(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x1CC, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    fobj->d0 = fobj->d1;
    fobj->d1 = parseFloat(&fobj->ad, fobj->frac_slope);

    return HSD_FObjGetState(fobj);
}

static u32 FObjAnimKey(HSD_FObj* fobj)
{
    u32 st = HSD_FObjGetState(fobj);
    HSD_ASSERT(0x1E9, st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA);

    FObjLaunchKeyData(fobj);
    fobj->p1 = parseFloat(&fobj->ad, fobj->frac_value);
    fobj->flags |= 0x40;

    return HSD_FObjSetState(fobj, st == FOBJ_LOAD_DATA0 ? FOBJ_LOAD_WAIT : 4);
}

static inline u32 FObjLoadData(HSD_FObj* fobj)
{
    if ((unsigned) (fobj->ad - fobj->ad_head) >= fobj->length) {
        return 6;
    } else {
        fobj->op_intrp = fobj->op;
        if (fobj->nb_pack == 0) {
            fobj->op = parseOpCode(&fobj->ad);
            fobj->nb_pack = parsePackInfo(&fobj->ad);
        }

        fobj->nb_pack -= 1;

        switch (fobj->op) {
        case HSD_A_OP_CON:
            return FObjAnimCON(fobj);

        case HSD_A_OP_LIN:
            return FObjAnimLinear(fobj);

        case HSD_A_OP_SPL0:
            return FObjAnimSPL0(fobj);

        case HSD_A_OP_SPL:
            return FObjAnimSPL(fobj);

        case HSD_A_OP_SLP:
            return FObjAnimSLP(fobj);

        case HSD_A_OP_KEY:
            return FObjAnimKey(fobj);

        default:
            return 0;
        }
    }
}

void FObjUpdateAnim(HSD_FObj* fobj, void* obj, HSD_ObjUpdateFunc obj_update)
{
    f32 step_value;
    HSD_ObjData update_data;

    if (obj_update == NULL) {
        return;
    }
    switch (fobj->op_intrp) {
    case HSD_A_OP_KEY:
        if (fobj->flags & 0x80) {
            update_data.fv = fobj->p0;
            fobj->flags &= 0xFFFFFF7F;
        } else {
            return;
        }
        break;
    case HSD_A_OP_CON:
        if (fobj->time >= fobj->fterm) {
            step_value = fobj->p1;
        } else {
            step_value = fobj->p0;
        }
        update_data.fv = step_value;
        break;
    case HSD_A_OP_LIN:
        if (fobj->flags & 0x20) {
            fobj->flags = fobj->flags & 0xFFFFFFDF;
            if (fobj->fterm != 0) {
                fobj->d0 = (fobj->p1 - fobj->p0) / fobj->fterm;
            } else {
                fobj->d0 = 0;
                fobj->p0 = fobj->p1;
            }
        }
        update_data.fv = fobj->d0 * fobj->time + fobj->p0;
        break;
    case HSD_A_OP_SPL0:
    case HSD_A_OP_SPL:
    case HSD_A_OP_SLP:
        if (fobj->fterm != 0) {
            update_data.fv =
                splGetHelmite(1.0 / fobj->fterm, fobj->time, fobj->p0,
                              fobj->p1, fobj->d0, fobj->d1);
        } else {
            update_data.fv = fobj->p1;
        }
        break;
    default:
        break;
    }
    obj_update(obj, fobj->obj_type, &update_data);
}

void HSD_FObjInterpretAnim(HSD_FObj* fobj, void* obj,
                           HSD_ObjUpdateFunc obj_update, f32 rate)
{
    f32 fterm;
    u32 state;

    fterm = 0.0F;
    state = fobj != NULL ? HSD_FObjGetState(fobj) : 0;
    if (state != 0 && !(fobj->time += rate, (fobj->time < 0.0))) {
        for (;;) {
            switch (state) {
            case 6: {
                fobj->time += fterm;
                FObjLaunchKeyData(fobj);
                FObjUpdateAnim(fobj, obj, obj_update);
                return;
            }
            case FOBJ_LOAD_DATA0:
            case FOBJ_LOAD_DATA: {
                state = FObjLoadData(fobj);
                break;
            }
            case FOBJ_LOAD_WAIT: {
                if ((fobj->flags & 0x80) != 0) {
                    FObjUpdateAnim(fobj, obj, obj_update);
                }
                state = FObjLoadWait(fobj);
                break;
            }
            case 4: {
                if (fobj->fterm <= fobj->time) {
                    u8 _[8] = { 0 };
                    state =
#ifdef MUST_MATCH
                        state =
#endif
                            FOBJ_LOAD_WAIT;

                    fterm = fobj->fterm;
                    fobj->time -= fobj->fterm;
                    HSD_FObjSetState(fobj, state);
                    break;
                }
                FObjUpdateAnim(fobj, obj, obj_update);
                state =
#ifdef MUST_MATCH
                    state =
#endif
                        5;
                HSD_FObjSetState(fobj, state);
                return;
            }
            case 5: {
                state =
#ifdef MUST_MATCH
                    state =
#endif
                        4;
                HSD_FObjSetState(fobj, state);
                break;
            }
            case 0:
                return;
            }
        }
    }
}

void HSD_FObjInterpretAnimAll(void* fobj, void* obj,
                              HSD_ObjUpdateFunc obj_update, f32 rate)
{
    HSD_FObj* track = fobj;
    while (track != NULL) {
        HSD_FObjInterpretAnim(track, obj, obj_update, rate);
        track = track->next;
    }
}

HSD_FObj* HSD_FObjLoadDesc(HSD_FObjDesc* desc)
{
    if (desc != NULL) {
        HSD_FObj* fobj = HSD_FObjAlloc();
        fobj->next = HSD_FObjLoadDesc(desc->next);
        fobj->startframe = desc->startframe;
        fobj->obj_type = desc->type;
        fobj->frac_value = desc->frac_value;
        fobj->frac_slope = desc->frac_slope;
        fobj->ad_head = desc->ad;
        fobj->length = desc->length;
        fobj->flags = 0;
        return fobj;
    }
    return NULL;
}

HSD_FObj* HSD_FObjAlloc(void)
{
    HSD_FObj* new = HSD_ObjAlloc(HSD_FObjGetAllocData());
    HSD_ASSERT(0x2F3, new);
    memset(new, 0, sizeof(HSD_FObj));
    return new;
}

void HSD_FObjFree(HSD_FObj* fobj)
{
    HSD_ObjFree(HSD_FObjGetAllocData(), fobj);
}
