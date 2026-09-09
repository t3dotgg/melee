#ifndef MELEE_NATIVE_GX_TEV_H
#define MELEE_NATIVE_GX_TEV_H

/* Private to gx.c. Texture sampling and decoded vertex coordinates stay in
 * the native renderer. TEV registers are signed 11-bit values, not GXColor. */
typedef struct GXSWTevCombiner {
    int input[4];
    GXTevOp op;
    GXTevBias bias;
    GXTevScale scale;
    GXBool clamp;
    GXTevRegID output;
} GXSWTevCombiner;

typedef struct GXSWTevStage {
    GXSWTevCombiner color;
    GXSWTevCombiner alpha;
    GXTexCoordID coord;
    GXTexMapID map;
    GXChannelID raster;
    GXTevKColorSel kcolor;
    GXTevKAlphaSel kalpha;
    GXTevSwapSel raster_swap;
    GXTevSwapSel texture_swap;
} GXSWTevStage;

static GXSWTevStage gx_tev_stages[GX_MAX_TEVSTAGE];
static int gx_tev_registers[4][4];
static GXColor gx_tev_kcolors[GX_MAX_KCOLOR];
static u8 gx_tev_swaps[4][4];
static u8 gx_num_tev_stages;

void GXSetNumTevStages(u8 count)
{
    gx_num_tev_stages = count > GX_MAX_TEVSTAGE ? GX_MAX_TEVSTAGE : count;
}

void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                     GXTevColorArg c, GXTevColorArg d)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    int* input = gx_tev_stages[stage].color.input;
    input[0] = a;
    input[1] = b;
    input[2] = c;
    input[3] = d;
}

void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                     GXTevAlphaArg c, GXTevAlphaArg d)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    int* input = gx_tev_stages[stage].alpha.input;
    input[0] = a;
    input[1] = b;
    input[2] = c;
    input[3] = d;
}

static void gx_tev_set_op(GXSWTevCombiner* combiner, GXTevOp op,
                          GXTevBias bias, GXTevScale scale, GXBool clamp,
                          GXTevRegID output)
{
    combiner->op = (GXTevOp) ((unsigned) op & 15);
    combiner->bias = bias;
    combiner->scale = (GXTevScale) ((unsigned) scale & 3);
    combiner->clamp = clamp;
    combiner->output = (GXTevRegID) ((unsigned) output & 3);
}

void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID output)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    gx_tev_set_op(&gx_tev_stages[stage].color, op, bias, scale, clamp, output);
}

void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID output)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    gx_tev_set_op(&gx_tev_stages[stage].alpha, op, bias, scale, clamp, output);
}

void GXSetTevColor(GXTevRegID id, GXColor color)
{
    if ((unsigned) id >= 4) {
        return;
    }
    gx_tev_registers[id][0] = color.r;
    gx_tev_registers[id][1] = color.g;
    gx_tev_registers[id][2] = color.b;
    gx_tev_registers[id][3] = color.a;
}

static int gx_tev_signed11(int value)
{
    return ((value & 2047) ^ 1024) - 1024;
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color)
{
    if ((unsigned) id >= 4) {
        return;
    }
    gx_tev_registers[id][0] = gx_tev_signed11(color.r);
    gx_tev_registers[id][1] = gx_tev_signed11(color.g);
    gx_tev_registers[id][2] = gx_tev_signed11(color.b);
    gx_tev_registers[id][3] = gx_tev_signed11(color.a);
}

void GXSetTevKColor(GXTevKColorID id, GXColor color)
{
    if ((unsigned) id < GX_MAX_KCOLOR) {
        gx_tev_kcolors[id] = color;
    }
}

void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel selector)
{
    if ((unsigned) stage < GX_MAX_TEVSTAGE) {
        gx_tev_stages[stage].kcolor = selector;
    }
}

void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel selector)
{
    if ((unsigned) stage < GX_MAX_TEVSTAGE) {
        gx_tev_stages[stage].kalpha = selector;
    }
}

void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map,
                   GXChannelID raster)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    gx_tev_stages[stage].coord = coord;
    gx_tev_stages[stage].map = map;
    gx_tev_stages[stage].raster = raster;
}

void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel raster,
                      GXTevSwapSel texture)
{
    if ((unsigned) stage >= GX_MAX_TEVSTAGE) {
        return;
    }
    gx_tev_stages[stage].raster_swap = (GXTevSwapSel) ((unsigned) raster & 3);
    gx_tev_stages[stage].texture_swap =
        (GXTevSwapSel) ((unsigned) texture & 3);
}

void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red,
                           GXTevColorChan green, GXTevColorChan blue,
                           GXTevColorChan alpha)
{
    if ((unsigned) table >= 4) {
        return;
    }
    gx_tev_swaps[table][0] = (u8) ((unsigned) red & 3);
    gx_tev_swaps[table][1] = (u8) ((unsigned) green & 3);
    gx_tev_swaps[table][2] = (u8) ((unsigned) blue & 3);
    gx_tev_swaps[table][3] = (u8) ((unsigned) alpha & 3);
}

void GXSetTevOp(GXTevStageID stage, GXTevMode mode)
{
    GXTevColorArg color = stage == GX_TEVSTAGE0 ? GX_CC_RASC : GX_CC_CPREV;
    GXTevAlphaArg alpha = stage == GX_TEVSTAGE0 ? GX_CA_RASA : GX_CA_APREV;
    switch (mode) {
    case GX_MODULATE:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, color, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, alpha, GX_CA_ZERO);
        break;
    case GX_DECAL:
        GXSetTevColorIn(stage, color, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, alpha);
        break;
    case GX_BLEND:
        GXSetTevColorIn(stage, color, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, alpha, GX_CA_ZERO);
        break;
    case GX_REPLACE:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    case GX_PASSCLR:
        GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, color);
        GXSetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, alpha);
        break;
    default:
        return;
    }
    GXSetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
}

static void gx_tev_reset(void)
{
    memset(gx_tev_stages, 0, sizeof gx_tev_stages);
    memset(gx_tev_registers, 0, sizeof gx_tev_registers);
    memset(gx_tev_kcolors, 0, sizeof gx_tev_kcolors);
    for (unsigned table = 0; table < 4; table++) {
        for (unsigned channel = 0; channel < 4; channel++) {
            gx_tev_swaps[table][channel] =
                (u8) (table == 0 || channel == 3 ? channel : table - 1);
        }
    }
    for (GXTevStageID stage = GX_TEVSTAGE0; stage < GX_MAX_TEVSTAGE; stage++) {
        GXSetTevOrder(stage, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevKColorSel(stage, GX_TEV_KCSEL_1_4);
        GXSetTevKAlphaSel(stage, GX_TEV_KASEL_1);
        GXSetTevOp(stage, GX_PASSCLR);
    }
    /* Keep the native renderer's untextured startup draws visible. The game
     * configures its own stage inputs before it draws an HSD material. */
    gx_num_tev_stages = 1;
}

static int gx_tev_component(GXColor color, unsigned channel)
{
    switch (channel) {
    case 0:
        return color.r;
    case 1:
        return color.g;
    case 2:
        return color.b;
    default:
        return color.a;
    }
}

static void gx_tev_swap(int result[4], GXColor color, GXTevSwapSel table)
{
    for (unsigned channel = 0; channel < 4; channel++) {
        result[channel] =
            gx_tev_component(color, gx_tev_swaps[table][channel]);
    }
}

static int gx_tev_konst(unsigned selector, unsigned channel, bool alpha)
{
    static const u8 fractions[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
    selector &= 31;
    if (selector < 8) {
        return fractions[selector];
    }
    if (!alpha && selector >= 12 && selector < 16) {
        return gx_tev_component(gx_tev_kcolors[selector - 12], channel);
    }
    if (selector >= 16) {
        return gx_tev_component(gx_tev_kcolors[selector & 3],
                                (selector - 16) / 4);
    }
    return 0;
}

static int gx_tev_color_input(int selector, unsigned channel,
                              const int registers[4][4], const int texture[4],
                              const int raster[4], int konst)
{
    if ((unsigned) selector <= GX_CC_A2) {
        return registers[selector / 2][(selector & 1) ? 3 : channel];
    }
    switch (selector) {
    case GX_CC_TEXC:
        return texture[channel];
    case GX_CC_TEXA:
        return texture[3];
    case GX_CC_RASC:
        return raster[channel];
    case GX_CC_RASA:
        return raster[3];
    case GX_CC_ONE:
        return 255;
    case GX_CC_HALF:
        return 128;
    case GX_CC_KONST:
        return konst;
    default:
        return 0;
    }
}

static int gx_tev_alpha_input(int selector, const int registers[4][4],
                              const int texture[4], const int raster[4],
                              int konst)
{
    if ((unsigned) selector <= GX_CA_A2) {
        return registers[selector][3];
    }
    switch (selector) {
    case GX_CA_TEXA:
        return texture[3];
    case GX_CA_RASA:
        return raster[3];
    case GX_CA_KONST:
        return konst;
    default:
        return 0;
    }
}

static int gx_tev_clamp(int value, GXBool clamp)
{
    int low = clamp ? 0 : -1024;
    int high = clamp ? 255 : 1023;
    return value < low ? low : value > high ? high : value;
}

/* Integer interpolation follows Flipper's 8-bit input and 9-bit factor.
 * Scale is applied before rounding. Negative D inputs use multiplication
 * instead of a signed left shift. Divide-by-two rounds down. */
static int gx_tev_arithmetic(const GXSWTevCombiner* combiner, int a, int b,
                             int c, int d)
{
    int factor = c + (c >> 7);
    int interpolation = a * 256 + (b - a) * factor;
    int scale = combiner->scale == GX_CS_DIVIDE_2 ? 1 : 1 << combiner->scale;
    if (combiner->bias == GX_TB_ADDHALF) {
        d += 128;
    }
    if (combiner->bias == GX_TB_SUBHALF) {
        d -= 128;
    }
    interpolation *= scale;
    if (combiner->scale != GX_CS_DIVIDE_2) {
        interpolation += combiner->op == GX_TEV_SUB ? 127 : 128;
    }
    int value = d * scale + (combiner->op == GX_TEV_SUB ? -(interpolation >> 8)
                                                        : interpolation >> 8);
    if (combiner->scale == GX_CS_DIVIDE_2) {
        value >>= 1;
    }
    return gx_tev_clamp(value, combiner->clamp);
}

static bool gx_tev_compare(GXTevOp op, const int a[4], const int b[4],
                           unsigned channel)
{
    unsigned mode = ((unsigned) op >> 1) & 3;
    int left = a[channel];
    int right = b[channel];
    if (mode < 3) {
        left = a[0];
        right = b[0];
        if (mode >= 1) {
            left += a[1] * 256;
            right += b[1] * 256;
        }
        if (mode == 2) {
            left += a[2] * 65536;
            right += b[2] * 65536;
        }
    }
    return (op & 1) ? left == right : left > right;
}

static GXColor gx_shade_color(GXColor raster0, GXColor raster1,
                              const f32 coords[8][2], f32 depth)
{
    int registers[4][4];
    int result[4] = { raster0.r, raster0.g, raster0.b, raster0.a };
    (void) depth;
    memcpy(registers, gx_tev_registers, sizeof registers);
    for (unsigned index = 0; index < gx_num_tev_stages; index++) {
        const GXSWTevStage* stage = &gx_tev_stages[index];
        GXColor sampled = { 255, 255, 255, 255 };
        GXColor raster_color = { 0, 0, 0, 0 };
        int texture[4], raster[4], inputs[4][4];
        unsigned map = (unsigned) stage->map & ~GX_TEX_DISABLE;
        unsigned coord = (unsigned) stage->coord;
        if (coord >= GX_MAX_TEXCOORD) {
            coord = 0;
        }
        if (map < GX_MAX_TEXMAP && !(stage->map & GX_TEX_DISABLE)) {
            sampled = gx_texture_sample(&gx_textures[map], coords[coord][0],
                                        coords[coord][1]);
        }
        switch (stage->raster) {
        case GX_COLOR0:
        case GX_ALPHA0:
        case GX_COLOR0A0:
            raster_color = raster0;
            break;
        case GX_COLOR1:
        case GX_ALPHA1:
        case GX_COLOR1A1:
            raster_color = raster1;
            break;
        default:
            /* Bump alpha requires the indirect texture pipeline. */
            break;
        }
        gx_tev_swap(texture, sampled, stage->texture_swap);
        gx_tev_swap(raster, raster_color, stage->raster_swap);
        /* Both combiners read the registers before either one writes. */
        for (unsigned input = 0; input < 4; input++) {
            for (unsigned channel = 0; channel < 3; channel++) {
                int konst = gx_tev_konst(stage->kcolor, channel, false);
                inputs[input][channel] =
                    gx_tev_color_input(stage->color.input[input], channel,
                                       registers, texture, raster, konst);
            }
            inputs[input][3] = gx_tev_alpha_input(
                stage->alpha.input[input], registers, texture, raster,
                gx_tev_konst(stage->kalpha, 3, true));
            if (input < 3) {
                for (unsigned channel = 0; channel < 4; channel++) {
                    inputs[input][channel] &= 255;
                }
            }
        }
        for (unsigned channel = 0; channel < 4; channel++) {
            const GXSWTevCombiner* combiner =
                channel == 3 ? &stage->alpha : &stage->color;
            if (combiner->op >= GX_TEV_COMP_R8_GT) {
                bool test = gx_tev_compare(combiner->op, inputs[0], inputs[1],
                                           channel);
                result[channel] = gx_tev_clamp(
                    inputs[3][channel] + (test ? inputs[2][channel] : 0),
                    combiner->clamp);
            } else {
                result[channel] = gx_tev_arithmetic(
                    combiner, inputs[0][channel], inputs[1][channel],
                    inputs[2][channel], inputs[3][channel]);
            }
            registers[combiner->output][channel] = result[channel];
        }
    }
    /* The final stage supplies RGB and alpha even when it writes REG0..2.
     * The pixel pipeline uses the low byte of an unclamped stage result. */
    GXColor color = { (u8) result[0], (u8) result[1], (u8) result[2],
                      (u8) result[3] };
    return color;
}

#endif
