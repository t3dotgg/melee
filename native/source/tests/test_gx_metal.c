#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/gx_metal.h"

/* An odd width exercises Metal's padded transfer rows. No game data is used.
 */
enum {
    WIDTH = 67,
    HEIGHT = 53,
    PIXELS = WIDTH * HEIGHT
};
static uint8_t pixels[PIXELS * 4];
static float depths[PIXELS];
static NativeGXMetalTexture textures[8];

static NativeGXMetalState default_state(void)
{
    NativeGXMetalState state = { 0 };
    state.stage_count = 1;
    NativeGXMetalStage* stage = &state.stage[0];
    stage->map = 255;
    stage->raster = 4;
    stage->color.input[0] = stage->color.input[1] = stage->color.input[2] = 15;
    stage->color.input[3] = 10;
    stage->alpha.input[0] = stage->alpha.input[1] = stage->alpha.input[2] = 7;
    stage->alpha.input[3] = 5;
    stage->color.clamp = stage->alpha.clamp = 1;
    for (unsigned i = 0; i < 4; ++i) {
        state.swap[i][0] = 0;
        state.swap[i][1] = 1;
        state.swap[i][2] = 2;
        state.swap[i][3] = 3;
    }
    state.z_compare = state.z_update = 1;
    state.z_func = 3;
    state.alpha_func[0] = state.alpha_func[1] = 7;
    state.blend_src = 1;
    state.color_update = state.alpha_update = 1;
    state.scissor[2] = WIDTH;
    state.scissor[3] = HEIGHT;
    state.viewport_far = 1;
    return state;
}

static void clear_efb(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    for (unsigned i = 0; i < PIXELS; ++i) {
        pixels[i * 4] = red;
        pixels[i * 4 + 1] = green;
        pixels[i * 4 + 2] = blue;
        pixels[i * 4 + 3] = alpha;
        depths[i] = 1;
    }
    assert(NativeGXMetalUpload(pixels, depths));
}

static void draw_triangle(NativeGXMetalState* state, float depth, float red,
                          float green, float blue, float alpha)
{
    NativeGXMetalVertex vertices[3] = { 0 };
    /* Screen winding: top left, top right, bottom left. */
    const float positions[3][2] = { { -1, 1 }, { 1, 1 }, { -1, -1 } };
    for (unsigned i = 0; i < 3; ++i) {
        vertices[i].position[0] = positions[i][0];
        vertices[i].position[1] = positions[i][1];
        vertices[i].position[2] = depth;
        vertices[i].position[3] = 1;
        vertices[i].color[0][0] = red;
        vertices[i].color[0][1] = green;
        vertices[i].color[0][2] = blue;
        vertices[i].color[0][3] = alpha;
        for (unsigned coord = 0; coord < 8; ++coord) {
            vertices[i].texcoord[coord][0] = 0.5F;
            vertices[i].texcoord[coord][1] = 0.5F;
            vertices[i].texcoord[coord][2] = 1;
        }
    }
    assert(NativeGXMetalDraw(vertices, 3, state, textures));
}

static void read_efb(void)
{
    assert(NativeGXMetalReadback(pixels, depths));
}

static void expect_pixel(unsigned x, unsigned y, int red, int green, int blue,
                         int alpha)
{
    unsigned index = (y * WIDTH + x) * 4;
    int expected[] = { red, green, blue, alpha };
    for (unsigned i = 0; i < 4; ++i) {
        if (abs((int) pixels[index + i] - expected[i]) > 1) {
            fprintf(stderr, "pixel (%u,%u) channel %u: got %u expected %d\n",
                    x, y, i, pixels[index + i], expected[i]);
            assert(0);
        }
    }
}

static void test_transfer(void)
{
    for (unsigned i = 0; i < sizeof pixels; ++i) {
        pixels[i] = (uint8_t) (i * 37);
    }
    for (unsigned i = 0; i < PIXELS; ++i) {
        depths[i] = (float) i / PIXELS;
    }
    assert(NativeGXMetalUpload(pixels, depths));
    memset(pixels, 0, sizeof pixels);
    memset(depths, 0, sizeof depths);
    read_efb();
    for (unsigned i = 0; i < sizeof pixels; ++i) {
        assert(pixels[i] == (uint8_t) (i * 37));
    }
    for (unsigned i = 0; i < PIXELS; ++i) {
        assert(depths[i] == (float) i / PIXELS);
    }
}

static void test_depth_and_culling(void)
{
    NativeGXMetalState state = default_state();
    clear_efb(1, 2, 3, 4);
    draw_triangle(&state, 0.25F, 255, 0, 0, 255);
    draw_triangle(&state, 0.75F, 0, 255, 0, 255);
    read_efb();
    expect_pixel(5, 5, 255, 0, 0, 255);
    expect_pixel(WIDTH - 1, HEIGHT - 1, 1, 2, 3, 4);
    assert(fabsf(depths[5 * WIDTH + 5] - 0.25F) < 0.00001F);

    clear_efb(1, 2, 3, 4);
    state.cull_mode = 2;
    draw_triangle(&state, 0.5F, 255, 0, 0, 255);
    read_efb();
    expect_pixel(5, 5, 255, 0, 0, 255);
    state.cull_mode = 1;
    draw_triangle(&state, 0.2F, 0, 255, 0, 255);
    read_efb();
    expect_pixel(5, 5, 255, 0, 0, 255);

    state = default_state();
    state.z_compare = 0;
    draw_triangle(&state, 0.9F, 0, 255, 0, 255);
    read_efb();
    expect_pixel(5, 5, 0, 255, 0, 255);
    assert(fabsf(depths[5 * WIDTH + 5] - 0.5F) < 0.00001F);
}

static void test_alpha_depth_order(void)
{
    NativeGXMetalState state = default_state();
    state.alpha_func[0] = 4;
    state.alpha_ref[0] = 128;
    clear_efb(1, 2, 3, 4);
    draw_triangle(&state, 0.25F, 255, 0, 0, 100);
    read_efb();
    expect_pixel(5, 5, 1, 2, 3, 4);
    assert(depths[5 * WIDTH + 5] == 1);

    state.z_before_texture = 1;
    assert(NativeGXMetalSupports(&state));
    draw_triangle(&state, 0.25F, 255, 0, 0, 100);
    read_efb();
    expect_pixel(5, 5, 1, 2, 3, 4);
    assert(fabsf(depths[5 * WIDTH + 5] - 0.25F) < 0.00001F);
}

static void test_blend_and_masks(void)
{
    NativeGXMetalState state = default_state();
    clear_efb(0, 0, 255, 255);
    state.blend_mode = 1;
    state.blend_src = 4;
    state.blend_dst = 5;
    state.dst_alpha_enabled = 1;
    state.dst_alpha = 33;
    draw_triangle(&state, 0.5F, 255, 0, 0, 128);
    read_efb();
    expect_pixel(5, 5, 128, 0, 127, 33);

    state.color_update = 0;
    state.dst_alpha = 88;
    draw_triangle(&state, 0.4F, 0, 255, 0, 128);
    read_efb();
    expect_pixel(5, 5, 128, 0, 127, 88);

    state.color_update = 1;
    state.alpha_update = 0;
    state.blend_mode = 0;
    draw_triangle(&state, 0.3F, 0, 255, 0, 255);
    read_efb();
    expect_pixel(5, 5, 0, 255, 0, 88);

    state = default_state();
    clear_efb(150, 100, 80, 200);
    state.blend_mode = 3;
    draw_triangle(&state, 0.5F, 20, 130, 10, 50);
    read_efb();
    expect_pixel(5, 5, 130, 0, 70, 150);
}

static void test_scissor_and_texture(void)
{
    NativeGXMetalState state = default_state();
    clear_efb(1, 2, 3, 4);
    state.scissor[0] = state.scissor[1] = 8;
    state.scissor[2] = state.scissor[3] = 4;
    draw_triangle(&state, 0.5F, 255, 0, 0, 255);
    read_efb();
    expect_pixel(9, 9, 255, 0, 0, 255);
    expect_pixel(5, 5, 1, 2, 3, 4);
    expect_pixel(13, 9, 1, 2, 3, 4);

    static uint8_t texel[4] = { 23, 45, 67, 89 };
    state = default_state();
    state.stage[0].map = 0;
    state.stage[0].color.input[3] = 8;
    state.stage[0].alpha.input[3] = 4;
    textures[0].rgba = texel;
    textures[0].width = textures[0].height = 1;
    textures[0].serial = 1;
    draw_triangle(&state, 0.2F, 255, 255, 255, 255);
    read_efb();
    expect_pixel(5, 5, 23, 45, 67, 89);
    texel[0] = 123;
    textures[0].serial = 2;
    draw_triangle(&state, 0.1F, 255, 255, 255, 255);
    read_efb();
    expect_pixel(5, 5, 123, 45, 67, 89);
    memset(textures, 0, sizeof textures);
}

static void test_interpolation(void)
{
    NativeGXMetalState state = default_state();
    NativeGXMetalVertex vertices[3] = { 0 };
    const float clip_w[] = { 1, 2, 4 };
    const float positions[3][2] = { { -1, 1 }, { 1, 1 }, { -1, -1 } };
    const float s_coord[] = { 0, 1, 0 };
    const float q_coord[] = { 1, 2, 1 };
    for (unsigned i = 0; i < 3; ++i) {
        vertices[i].position[0] = positions[i][0] * clip_w[i];
        vertices[i].position[1] = positions[i][1] * clip_w[i];
        vertices[i].position[2] = 0.5F * clip_w[i];
        vertices[i].position[3] = clip_w[i];
        vertices[i].color[0][i] = 255;
        vertices[i].color[0][3] = 255;
        vertices[i].texcoord[0][0] = s_coord[i];
        vertices[i].texcoord[0][1] = 0;
        vertices[i].texcoord[0][2] = q_coord[i];
    }
    clear_efb(0, 0, 0, 0);
    assert(NativeGXMetalDraw(vertices, 3, &state, textures));
    read_efb();
    /* Raster colors use screen weights even when the clip W values differ. */
    const unsigned x = 20, y = 10;
    float weights[] = { 1 - (x + 0.5F) / WIDTH - (y + 0.5F) / HEIGHT,
                        (x + 0.5F) / WIDTH, (y + 0.5F) / HEIGHT };
    expect_pixel(x, y, (int) roundf(weights[0] * 255),
                 (int) roundf(weights[1] * 255),
                 (int) roundf(weights[2] * 255), 255);

    /* Use a ramp to expose the final S / Q value. Both S and Q interpolate
     * with perspective before that division. */
    uint8_t ramp[256 * 4];
    for (unsigned i = 0; i < 256; ++i) {
        ramp[i * 4] = (uint8_t) i;
        ramp[i * 4 + 1] = ramp[i * 4 + 2] = 0;
        ramp[i * 4 + 3] = 255;
    }
    textures[0].rgba = ramp;
    textures[0].width = 256;
    textures[0].height = 1;
    textures[0].serial = 3;
    state.stage[0].map = 0;
    state.stage[0].color.input[3] = 8;
    state.stage[0].alpha.input[3] = 4;
    assert(NativeGXMetalDraw(vertices, 3, &state, textures));
    read_efb();
    float s = 0, q = 0;
    for (unsigned i = 0; i < 3; ++i) {
        s += weights[i] * s_coord[i] / clip_w[i];
        q += weights[i] * q_coord[i] / clip_w[i];
    }
    expect_pixel(x, y, (int) floorf(s / q * 256), 0, 0, 255);
    memset(textures, 0, sizeof textures);
}

int main(void)
{
    assert(NativeGXMetalInit(WIDTH, HEIGHT));
    test_transfer();
    test_depth_and_culling();
    test_alpha_depth_order();
    test_blend_and_masks();
    test_scissor_and_texture();
    test_interpolation();
    NativeGXMetalState state = default_state();
    state.blend_mode = 2;
    state.logic_op = 6;
    assert(!NativeGXMetalSupports(&state));
    NativeGXMetalShutdown();
    assert(!NativeGXMetalSupports(&state));
    assert(NativeGXMetalInit(WIDTH, HEIGHT));
    read_efb();
    expect_pixel(5, 5, 0, 0, 0, 0);
    assert(depths[5 * WIDTH + 5] == 1);
    NativeGXMetalShutdown();
    puts("Native Metal EFB tests passed");
    return 0;
}
