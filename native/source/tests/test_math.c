#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/mtx.h>

_Static_assert(sizeof(void*) == 8, "Math tests require host 64-bit pointers");
_Static_assert(sizeof(u32) == 4, "SDK u32 must remain 32 bits");
_Static_assert(sizeof(Mtx) == 48, "Native affine matrices contain 12 floats");

static void check(int condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", name);
        exit(1);
    }
}

static void near(float actual, float expected)
{
    if (!isfinite(actual) ||
        fabsf(actual - expected) > 0.00002f * fmaxf(1.0f, fabsf(expected)))
    {
        fprintf(stderr, "FAILED: expected %.9g, got %.9g\n", expected, actual);
        exit(1);
    }
}

static void vector(Vec actual, Vec expected)
{
    near(actual.x, expected.x);
    near(actual.y, expected.y);
    near(actual.z, expected.z);
}

static void matrix(Mtx actual, Mtx expected)
{
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            near(actual[row][column], expected[row][column]);
        }
    }
}

static void test_vectors(void)
{
    Vec a = { 3, 4, 12 };
    Vec b = { -2, 5, 1 };
    Vec result;
    near(PSVECSquareMag(&a), 169);
    near(PSVECMag(&a), 13);
    near(PSVECDotProduct(&a, &b), 26);
    near(PSVECSquareDistance(&a, &b), 147);
    near(VECDistance(&a, &b), sqrtf(147));
    PSVECNormalize(&a, &result);
    vector(result, (Vec){ 3.0f / 13, 4.0f / 13, 12.0f / 13 });
    PSVECNormalize(&a, &a);
    vector(a, result);
    PSVECScale(&a, &a, 13);
    vector(a, (Vec){ 3, 4, 12 });
    PSVECAdd(&a, &b, &a);
    vector(a, (Vec){ 1, 9, 13 });
    PSVECSubtract(&a, &b, &b);
    vector(b, (Vec){ 3, 4, 12 });

    a = (Vec){ 1, 2, 3 };
    b = (Vec){ 4, 5, 6 };
    PSVECCrossProduct(&a, &b, &a);
    vector(a, (Vec){ -3, 6, -3 });
    a = (Vec){ 1, 2, 3 };
    PSVECCrossProduct(&a, &b, &b);
    vector(b, (Vec){ -3, 6, -3 });

    a = (Vec){ 1, -1, 0 };
    b = (Vec){ 0, 2, 0 };
    VECReflect(&a, &b, &a);
    vector(a, (Vec){ sqrtf(0.5f), sqrtf(0.5f), 0 });
    a = (Vec){ 1, 0, 0 };
    b = (Vec){ 0, 1, 0 };
    VECHalfAngle(&a, &b, &a);
    vector(a, (Vec){ -sqrtf(0.5f), -sqrtf(0.5f), 0 });
    a = (Vec){ 1, 0, 0 };
    b = (Vec){ -1, 0, 0 };
    VECHalfAngle(&a, &b, &a);
    vector(a, (Vec){ 0, 0, 0 });
}

static void test_affine(void)
{
    Mtx a = { { 0, -2, 0, 4 }, { 3, 0, 0, -5 }, { 0, 0, 4, 6 } };
    Mtx b = { { 1, 2, 0, 7 }, { 0, 1, 0, 8 }, { 0, 0, 0.5f, -2 } };
    Mtx expected = { { 0, -2, 0, -12 }, { 3, 6, 0, 16 }, { 0, 0, 2, -2 } };
    Mtx expected_inverse = {
        { 0, 1.0f / 3, 0, 5.0f / 3 },
        { -0.5f, 0, 0, 2 },
        { 0, 0, 0.25f, -1.5f },
    };
    Mtx identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
    Mtx result, temporary, inverse;
    PSMTXIdentity(result);
    matrix(result, identity);
    PSMTXConcat(a, b, result);
    matrix(result, expected);
    PSMTXCopy(a, temporary);
    PSMTXConcat(temporary, b, temporary);
    matrix(temporary, expected);
    PSMTXCopy(b, temporary);
    PSMTXConcat(a, temporary, temporary);
    matrix(temporary, expected);
    PSMTXCopy(a, temporary);
    PSMTXConcat(temporary, temporary, temporary);
    Mtx squared = { { -6, 0, 0, 14 }, { 0, -6, 0, 7 }, { 0, 0, 16, 30 } };
    matrix(temporary, squared);

    check(PSMTXInverse(a, inverse) == 1, "invert nonsingular affine matrix");
    matrix(inverse, expected_inverse);
    PSMTXCopy(a, temporary);
    check(PSMTXInverse(temporary, temporary) == 1, "inverse aliases source");
    matrix(temporary, expected_inverse);
    PSMTXConcat(a, inverse, result);
    matrix(result, identity);
    PSMTXConcat(inverse, a, result);
    matrix(result, identity);

    Vec point = { 7, -3, 2 };
    PSMTXMultVec(a, &point, &point);
    vector(point, (Vec){ 10, 16, 14 });
    PSMTXMultVec(inverse, &point, &point);
    vector(point, (Vec){ 7, -3, 2 });
    PSMTXMultVecSR(a, &point, &point);
    vector(point, (Vec){ 6, 21, 8 });

    PSMTXCopy(a, temporary);
    PSMTXTranspose(temporary, temporary);
    Mtx transposed = { { 0, 3, 0, 0 }, { -2, 0, 0, 0 }, { 0, 0, 4, 0 } };
    matrix(temporary, transposed);
    PSMTXCopy(a, temporary);
    check(PSMTXInvXpose(temporary, temporary) == 1,
          "inverse transpose aliases source");
    Vec normal = { 1, 2, -1 };
    Vec tangent = { 2, -1, 0 };
    PSMTXMultVecSR(temporary, &normal, &normal);
    PSMTXMultVecSR(a, &tangent, &tangent);
    vector(normal, (Vec){ -1, 1.0f / 3, -0.25f });
    near(PSVECDotProduct(&normal, &tangent), 0);

    Mtx singular = { { 1, 2, 3, 9 }, { 2, 4, 6, 8 }, { 0, 0, 1, 7 } };
    PSMTXCopy(b, result);
    check(PSMTXInverse(singular, result) == 0, "singular inverse fails");
    matrix(result, b);
    check(PSMTXInvXpose(singular, result) == 0,
          "singular inverse transpose fails");
    matrix(result, b);
    PSMTXCopy(singular, result);
    check(PSMTXInverse(result, result) == 0, "singular inverse aliases input");
    matrix(result, singular);
}

static void test_rotations(void)
{
    Mtx rotation, expected;
    Vec point = { 0, 1, 0 };
    MTXRotRad(rotation, 'x', MTXDegToRad(90));
    PSMTXMultVec(rotation, &point, &point);
    vector(point, (Vec){ 0, 0, 1 });
    point = (Vec){ 0, 0, 1 };
    MTXRotRad(rotation, 'Y', MTXDegToRad(90));
    PSMTXMultVec(rotation, &point, &point);
    vector(point, (Vec){ 1, 0, 0 });
    PSMTXRotTrig(expected, 'z', 1, 0);
    Vec axis = { 0, 0, 7 };
    PSMTXRotAxisRad(rotation, &axis, MTXDegToRad(90));
    matrix(rotation, expected);
    Quaternion quaternion = { 0, 0, 2, 2 };
    PSMTXQuat(rotation, &quaternion);
    matrix(rotation, expected);
    quaternion = (Quaternion){ 0, 0, 0, 3 };
    PSMTXQuat(rotation, &quaternion);
    PSMTXIdentity(expected);
    matrix(rotation, expected);

    PSMTXTrans(rotation, 1, 2, 3);
    MTXTransApply(rotation, rotation, 4, -2, 7);
    PSMTXTrans(expected, 5, 0, 10);
    matrix(rotation, expected);
    MTXScaleApply(rotation, rotation, 2, 3, 4);
    Mtx scaled = { { 2, 0, 0, 10 }, { 0, 3, 0, 0 }, { 0, 0, 4, 40 } };
    matrix(rotation, scaled);

    Vec plane_point = { 0, 2, 0 };
    Vec plane_normal = { 0, 1, 0 };
    MTXReflect(rotation, &plane_point, &plane_normal);
    point = (Vec){ 3, 5, 7 };
    PSMTXMultVec(rotation, &point, &point);
    vector(point, (Vec){ 3, -1, 7 });
}

/* Evaluate homogeneous projection separately from the affine SDK helpers. */
static Vec project(Mtx44 m, Vec point)
{
    const float input[4] = { point.x, point.y, point.z, 1 };
    float output[4] = { 0 };
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            output[row] += m[row][column] * input[column];
        }
    }
    return (Vec){ output[0] / output[3], output[1] / output[3],
                  output[2] / output[3] };
}

static void test_camera(void)
{
    Mtx view;
    Vec camera = { 0, 0, 5 };
    Vec target = { 0, 0, 0 };
    Vec up = { 0, 1, 0 };
    C_MTXLookAt(view, &camera, &up, &target);
    Vec point;
    PSMTXMultVec(view, &camera, &point);
    vector(point, (Vec){ 0, 0, 0 });
    PSMTXMultVec(view, &target, &point);
    vector(point, (Vec){ 0, 0, -5 });
    camera = (Vec){ 3, 4, 5 };
    MTXLookAt(view, &camera, &up, &target);
    PSMTXMultVec(view, &target, &point);
    vector(point, (Vec){ 0, 0, -sqrtf(50) });
    PSMTXMultVec(view, &camera, &point);
    vector(point, (Vec){ 0, 0, 0 });

    Mtx44 projection;
    MTXPerspective(projection, 90, 2, 1, 11);
    vector(project(projection, (Vec){ 2, 1, -1 }), (Vec){ 1, 1, -1 });
    vector(project(projection, (Vec){ 0, 0, -11 }), (Vec){ 0, 0, 0 });
    MTXFrustum(projection, 3, -1, -2, 4, 2, 20);
    vector(project(projection, (Vec){ 4, 3, -2 }), (Vec){ 1, 1, -1 });
    vector(project(projection, (Vec){ -2, -1, -2 }), (Vec){ -1, -1, -1 });
    MTXOrtho(projection, 3, -1, -2, 4, 2, 20);
    vector(project(projection, (Vec){ 4, 3, -2 }), (Vec){ 1, 1, -1 });
    vector(project(projection, (Vec){ -2, -1, -20 }), (Vec){ -1, -1, 0 });

    MTXLightPerspective(view, 90, 2, 0.5f, 0.5f, 0.5f, 0.5f);
    point = (Vec){ 2, 1, -1 };
    PSMTXMultVec(view, &point, &point);
    vector(point, (Vec){ 1, 1, 1 });
    MTXLightFrustum(view, 3, -1, -2, 4, 2, 0.5f, 0.5f, 0.5f, 0.5f);
    point = (Vec){ 4, 3, -2 };
    PSMTXMultVec(view, &point, &point);
    vector(point, (Vec){ 2, 2, 2 });
    MTXLightOrtho(view, 3, -1, -2, 4, 0.5f, 0.5f, 0.5f, 0.5f);
    point = (Vec){ -2, -1, -2 };
    PSMTXMultVec(view, &point, &point);
    vector(point, (Vec){ 0, 0, 1 });
}

static void test_arrays(void)
{
    Mtx m = { { 0, -2, 0, 4 }, { 3, 0, 0, -5 }, { 0, 0, 4, 6 } };
    Vec points[3] = { { 1, 2, 3 }, { -1, 0, 2 }, { 0, 1, -2 } };
    Vec expected[3] = { { 0, -2, 18 }, { 4, -8, 14 }, { 2, -5, -2 } };
    Vec output[3];
    PSMTXMultVecArray(m, points, output, 3);
    for (size_t i = 0; i < 3; ++i) {
        vector(output[i], expected[i]);
    }
    PSMTXMultVecArray(m, points, points, 3);
    for (size_t i = 0; i < 3; ++i) {
        vector(points[i], expected[i]);
    }
    PSMTXMultVecArray(m, points, points, 0);
    vector(points[0], expected[0]);

    union {
        Mtx matrix;
        ROMtx reordered;
    } storage;
    PSMTXCopy(m, storage.matrix);
    PSMTXReorder(storage.matrix, storage.reordered);
    ROMtx identity;
    Mtx affine_identity;
    PSMTXIdentity(affine_identity);
    PSMTXReorder(affine_identity, identity);
    Vec source[3] = { { 1, 2, 3 }, { -1, 0, 2 }, { 0, 1, -2 } };
    PSMTXROMultVecArray(&storage.reordered, source, output, 3);
    for (size_t i = 0; i < 3; ++i) {
        vector(output[i], expected[i]);
    }
    float weights[] = { 0, 0.25f, 1 };
    PSMTXROSkin2VecArray(&identity, &storage.reordered, weights, source,
                         source, 3);
    vector(source[0], (Vec){ 1, 2, 3 });
    vector(source[1], (Vec){ 0.25f, -2, 5 });
    vector(source[2], expected[2]);

    S16Vec integers[] = { { 1, 2, 3 }, { -32768, 32767, -32768 } };
    PSMTXROMultS16VecArray(&storage.reordered, integers, output, 2);
    vector(output[0], expected[0]);
    vector(output[1], (Vec){ -65530, -98309, -131066 });
    Mtx44 full = {
        { 0, -2, 0, 4 }, { 3, 0, 0, -5 }, { 0, 0, 4, 6 }, { 0, 0, 0, 1 }
    };
    PSMTXMultS16VecArray(&full, integers, output, 2);
    vector(output[0], expected[0]);
    vector(output[1], (Vec){ -65530, -98309, -131066 });
}

static void test_stack(void)
{
    Mtx entries[3];
    MTXStack stack = { .stackBase = entries };
    Mtx scale, translation, expected;
    PSMTXScale(scale, 2, 3, 4);
    PSMTXTrans(translation, 5, 6, 7);
    MTXInitStack(&stack, 3);
    check(MTXGetStackPtr(&stack) == NULL, "empty matrix stack");
    check(MTXPop(&stack) == NULL, "pop empty matrix stack");
    check(MTXPush(&stack, scale) == &entries[0], "push first matrix");
    check(MTXPushFwd(&stack, translation) == &entries[1], "push composition");
    PSMTXConcat(scale, translation, expected);
    matrix(*MTXGetStackPtr(&stack), expected);
    check(MTXPushInv(&stack, scale) == &entries[2],
          "push inverse composition");
    matrix(*MTXGetStackPtr(&stack), translation);
    check(MTXPush(&stack, translation) == NULL, "full matrix stack");
    check(MTXGetStackPtr(&stack) == &entries[2], "overflow preserves stack");
    check(MTXPop(&stack) == &entries[1], "pop stack entry");
    check(MTXPop(&stack) == &entries[0], "pop second stack entry");
    check(MTXPop(&stack) == NULL, "pop final stack entry");
    check(MTXPushInvXpose(&stack, scale) == &entries[0],
          "push inverse transpose");
    PSMTXScale(expected, 0.5f, 1.0f / 3, 0.25f);
    matrix(*MTXGetStackPtr(&stack), expected);
    PSMTXScale(scale, 0, 1, 1);
    check(MTXPushInv(&stack, scale) == NULL, "singular stack inverse fails");
    check(MTXGetStackPtr(&stack) == &entries[0], "failure preserves stack");
}

int main(void)
{
    test_vectors();
    test_affine();
    test_rotations();
    test_camera();
    test_arrays();
    test_stack();
    puts("Native matrix and vector tests passed.");
    return 0;
}
