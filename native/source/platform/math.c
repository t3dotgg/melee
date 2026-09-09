/* Portable implementations of the Dolphin SDK math API.
 * C equations come from extern/dolphin/src/dolphin/mtx. Both the C_ and PS
 * names operate on host float arrays. They have no CPU or guest-memory state.
 */
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <dolphin/mtx.h>

#ifndef MELEE_NATIVE
#error This file belongs to the native source build.
#endif

void C_VECAdd(Vec* a, Vec* b, Vec* c)
{
    assert(a);
    assert(b);
    assert(c);
    c->x = a->x + b->x;
    c->y = a->y + b->y;
    c->z = a->z + b->z;
}

void C_VECSubtract(Vec* a, Vec* b, Vec* c)
{
    assert(a);
    assert(b);
    assert(c);
    c->x = a->x - b->x;
    c->y = a->y - b->y;
    c->z = a->z - b->z;
}

void C_VECScale(Vec* src, Vec* dst, f32 scale)
{
    assert(src);
    assert(dst);
    dst->x = (src->x * scale);
    dst->y = (src->y * scale);
    dst->z = (src->z * scale);
}

void C_VECNormalize(Vec* src, Vec* unit)
{
    f32 mag;

    assert(src);
    assert(unit);
    mag = (src->z * src->z) + ((src->x * src->x) + (src->y * src->y));
    assert(0.0f != mag);
    mag = 1.0f / sqrtf(mag);
    unit->x = src->x * mag;
    unit->y = src->y * mag;
    unit->z = src->z * mag;
}

f32 C_VECSquareMag(Vec* v)
{
    f32 sqmag;

    assert(v);

    sqmag = v->z * v->z + ((v->x * v->x) + (v->y * v->y));
    return sqmag;
}

f32 C_VECMag(Vec* v)
{
    return sqrtf(VECSquareMag(v));
}

f32 C_VECDotProduct(Vec* a, Vec* b)
{
    f32 dot;

    assert(a);
    assert(b);
    dot = (a->z * b->z) + ((a->x * b->x) + (a->y * b->y));
    return dot;
}

void C_VECCrossProduct(Vec* a, Vec* b, Vec* axb)
{
    Vec vTmp;

    assert(a);
    assert(b);
    assert(axb);

    vTmp.x = (a->y * b->z) - (a->z * b->y);
    vTmp.y = (a->z * b->x) - (a->x * b->z);
    vTmp.z = (a->x * b->y) - (a->y * b->x);
    axb->x = vTmp.x;
    axb->y = vTmp.y;
    axb->z = vTmp.z;
}

void VECHalfAngle(Vec* a, Vec* b, Vec* half)
{
    Vec aTmp;
    Vec bTmp;
    Vec hTmp;

    assert(a);
    assert(b);
    assert(half);
    aTmp.x = -a->x;
    aTmp.y = -a->y;
    aTmp.z = -a->z;
    bTmp.x = -b->x;
    bTmp.y = -b->y;
    bTmp.z = -b->z;
    VECNormalize(&aTmp, &aTmp);
    VECNormalize(&bTmp, &bTmp);
    VECAdd(&aTmp, &bTmp, &hTmp);
    if (VECDotProduct(&hTmp, &hTmp) > 0.0f) {
        VECNormalize(&hTmp, half);
        return;
    }
    *half = hTmp;
}

void VECReflect(Vec* src, Vec* normal, Vec* dst)
{
    f32 cosA;
    Vec uI;
    Vec uN;

    assert(src);
    assert(normal);
    assert(dst);

    uI.x = -src->x;
    uI.y = -src->y;
    uI.z = -src->z;
    VECNormalize(&uI, &uI);
    VECNormalize(normal, &uN);
    cosA = VECDotProduct(&uI, &uN);
    dst->x = (2.0f * uN.x * cosA) - uI.x;
    dst->y = (2.0f * uN.y * cosA) - uI.y;
    dst->z = (2.0f * uN.z * cosA) - uI.z;
    VECNormalize(dst, dst);
}

f32 C_VECSquareDistance(Vec* a, Vec* b)
{
    Vec diff;

    diff.x = a->x - b->x;
    diff.y = a->y - b->y;
    diff.z = a->z - b->z;
    return (diff.z * diff.z) + ((diff.x * diff.x) + (diff.y * diff.y));
}

f32 VECDistance(Vec* a, Vec* b)
{
    return sqrtf(VECSquareDistance(a, b));
}

void C_MTXIdentity(Mtx m)
{
    assert(m);
    m[0][0] = 1;
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = 1;
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = 1;
    m[2][3] = 0;
}

void C_MTXCopy(Mtx src, Mtx dst)
{
    assert(src);
    assert(dst);
    if (src != dst) {
        dst[0][0] = src[0][0];
        dst[0][1] = src[0][1];
        dst[0][2] = src[0][2];
        dst[0][3] = src[0][3];
        dst[1][0] = src[1][0];
        dst[1][1] = src[1][1];
        dst[1][2] = src[1][2];
        dst[1][3] = src[1][3];
        dst[2][0] = src[2][0];
        dst[2][1] = src[2][1];
        dst[2][2] = src[2][2];
        dst[2][3] = src[2][3];
    }
}

void C_MTXConcat(Mtx a, Mtx b, Mtx ab)
{
    Mtx mTmp;
    f32(*m)[4];

    assert(a);
    assert(b);
    assert(ab);

    if (ab == a || ab == b) {
        m = mTmp;
    } else {
        m = ab;
    }

    m[0][0] =
        0 + a[0][2] * b[2][0] + ((a[0][0] * b[0][0]) + (a[0][1] * b[1][0]));
    m[0][1] =
        0 + a[0][2] * b[2][1] + ((a[0][0] * b[0][1]) + (a[0][1] * b[1][1]));
    m[0][2] =
        0 + a[0][2] * b[2][2] + ((a[0][0] * b[0][2]) + (a[0][1] * b[1][2]));
    m[0][3] = a[0][3] +
              (a[0][2] * b[2][3] + (a[0][0] * b[0][3] + (a[0][1] * b[1][3])));

    m[1][0] =
        0 + a[1][2] * b[2][0] + ((a[1][0] * b[0][0]) + (a[1][1] * b[1][0]));
    m[1][1] =
        0 + a[1][2] * b[2][1] + ((a[1][0] * b[0][1]) + (a[1][1] * b[1][1]));
    m[1][2] =
        0 + a[1][2] * b[2][2] + ((a[1][0] * b[0][2]) + (a[1][1] * b[1][2]));
    m[1][3] = a[1][3] +
              (a[1][2] * b[2][3] + (a[1][0] * b[0][3] + (a[1][1] * b[1][3])));

    m[2][0] =
        0 + a[2][2] * b[2][0] + ((a[2][0] * b[0][0]) + (a[2][1] * b[1][0]));
    m[2][1] =
        0 + a[2][2] * b[2][1] + ((a[2][0] * b[0][1]) + (a[2][1] * b[1][1]));
    m[2][2] =
        0 + a[2][2] * b[2][2] + ((a[2][0] * b[0][2]) + (a[2][1] * b[1][2]));
    m[2][3] = a[2][3] +
              (a[2][2] * b[2][3] + (a[2][0] * b[0][3] + (a[2][1] * b[1][3])));

    if (m == mTmp) {
        C_MTXCopy(mTmp, ab);
    }
}

void C_MTXTranspose(Mtx src, Mtx xPose)
{
    Mtx mTmp;
    f32(*m)[4];

    assert(src);
    assert(xPose);

    if (src == xPose) {
        m = mTmp;
    } else {
        m = xPose;
    }

    m[0][0] = src[0][0];
    m[0][1] = src[1][0];
    m[0][2] = src[2][0];
    m[0][3] = 0;
    m[1][0] = src[0][1];
    m[1][1] = src[1][1];
    m[1][2] = src[2][1];
    m[1][3] = 0;
    m[2][0] = src[0][2];
    m[2][1] = src[1][2];
    m[2][2] = src[2][2];
    m[2][3] = 0;
    if (m == mTmp) {
        C_MTXCopy(mTmp, xPose);
    }
}

u32 C_MTXInverse(Mtx src, Mtx inv)
{
    Mtx mTmp;
    f32(*m)[4];
    f32 det;

    assert(src);
    assert(inv);

    if (src == inv) {
        m = mTmp;
    } else {
        m = inv;
    }
    det = ((((src[2][1] * (src[0][2] * src[1][0])) +
             ((src[2][2] * (src[0][0] * src[1][1])) +
              (src[2][0] * (src[0][1] * src[1][2])))) -
            (src[0][2] * (src[2][0] * src[1][1]))) -
           (src[2][2] * (src[1][0] * src[0][1]))) -
          (src[1][2] * (src[0][0] * src[2][1]));
    if (0 == det) {
        return 0;
    }
    det = 1 / det;
    m[0][0] = (det * +((src[1][1] * src[2][2]) - (src[2][1] * src[1][2])));
    m[0][1] = (det * -((src[0][1] * src[2][2]) - (src[2][1] * src[0][2])));
    m[0][2] = (det * +((src[0][1] * src[1][2]) - (src[1][1] * src[0][2])));

    m[1][0] = (det * -((src[1][0] * src[2][2]) - (src[2][0] * src[1][2])));
    m[1][1] = (det * +((src[0][0] * src[2][2]) - (src[2][0] * src[0][2])));
    m[1][2] = (det * -((src[0][0] * src[1][2]) - (src[1][0] * src[0][2])));

    m[2][0] = (det * +((src[1][0] * src[2][1]) - (src[2][0] * src[1][1])));
    m[2][1] = (det * -((src[0][0] * src[2][1]) - (src[2][0] * src[0][1])));
    m[2][2] = (det * +((src[0][0] * src[1][1]) - (src[1][0] * src[0][1])));

    m[0][3] = ((-m[0][0] * src[0][3]) - (m[0][1] * src[1][3])) -
              (m[0][2] * src[2][3]);
    m[1][3] = ((-m[1][0] * src[0][3]) - (m[1][1] * src[1][3])) -
              (m[1][2] * src[2][3]);
    m[2][3] = ((-m[2][0] * src[0][3]) - (m[2][1] * src[1][3])) -
              (m[2][2] * src[2][3]);

    if (m == mTmp) {
        C_MTXCopy(mTmp, inv);
    }
    return 1;
}

u32 C_MTXInvXpose(Mtx src, Mtx invX)
{
    Mtx mTmp;
    f32(*m)[4];
    f32 det;

    assert(src);
    assert(invX);

    if (src == invX) {
        m = mTmp;
    } else {
        m = invX;
    }
    det = ((((src[2][1] * (src[0][2] * src[1][0])) +
             ((src[2][2] * (src[0][0] * src[1][1])) +
              (src[2][0] * (src[0][1] * src[1][2])))) -
            (src[0][2] * (src[2][0] * src[1][1]))) -
           (src[2][2] * (src[1][0] * src[0][1]))) -
          (src[1][2] * (src[0][0] * src[2][1]));
    if (0 == det) {
        return 0;
    }
    det = 1 / det;
    m[0][0] = (det * +((src[1][1] * src[2][2]) - (src[2][1] * src[1][2])));
    m[0][1] = (det * -((src[1][0] * src[2][2]) - (src[2][0] * src[1][2])));
    m[0][2] = (det * +((src[1][0] * src[2][1]) - (src[2][0] * src[1][1])));

    m[1][0] = (det * -((src[0][1] * src[2][2]) - (src[2][1] * src[0][2])));
    m[1][1] = (det * +((src[0][0] * src[2][2]) - (src[2][0] * src[0][2])));
    m[1][2] = (det * -((src[0][0] * src[2][1]) - (src[2][0] * src[0][1])));

    m[2][0] = (det * +((src[0][1] * src[1][2]) - (src[1][1] * src[0][2])));
    m[2][1] = (det * -((src[0][0] * src[1][2]) - (src[1][0] * src[0][2])));
    m[2][2] = (det * +((src[0][0] * src[1][1]) - (src[1][0] * src[0][1])));

    m[0][3] = 0;
    m[1][3] = 0;
    m[2][3] = 0;

    if (m == mTmp) {
        C_MTXCopy(mTmp, invX);
    }
    return 1;
}

void MTXRotRad(Mtx m, char axis, f32 rad)
{
    f32 sinA;
    f32 cosA;

    assert(m);
    sinA = sinf(rad);
    cosA = cosf(rad);
    MTXRotTrig(m, axis, sinA, cosA);
}

void C_MTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA)
{
    assert(m);
    switch (axis) {
    case 'x':
    case 'X':
        m[0][0] = 1;
        m[0][1] = 0;
        m[0][2] = 0;
        m[0][3] = 0;
        m[1][0] = 0;
        m[1][1] = cosA;
        m[1][2] = -sinA;
        m[1][3] = 0;
        m[2][0] = 0;
        m[2][1] = sinA;
        m[2][2] = cosA;
        m[2][3] = 0;
        break;
    case 'y':
    case 'Y':
        m[0][0] = cosA;
        m[0][1] = 0;
        m[0][2] = sinA;
        m[0][3] = 0;
        m[1][0] = 0;
        m[1][1] = 1;
        m[1][2] = 0;
        m[1][3] = 0;
        m[2][0] = -sinA;
        m[2][1] = 0;
        m[2][2] = cosA;
        m[2][3] = 0;
        break;
    case 'z':
    case 'Z':
        m[0][0] = cosA;
        m[0][1] = -sinA;
        m[0][2] = 0;
        m[0][3] = 0;
        m[1][0] = sinA;
        m[1][1] = cosA;
        m[1][2] = 0;
        m[1][3] = 0;
        m[2][0] = 0;
        m[2][1] = 0;
        m[2][2] = 1;
        m[2][3] = 0;
        break;
    default:
        assert(FALSE);
        break;
    }
}

void C_MTXRotAxisRad(Mtx m, Vec* axis, f32 rad)
{
    Vec vN;
    f32 s;
    f32 c;
    f32 t;
    f32 x;
    f32 y;
    f32 z;
    f32 xSq;
    f32 ySq;
    f32 zSq;

    assert(m);
    assert(axis);

    s = sinf(rad);
    c = cosf(rad);
    t = 1 - c;
    VECNormalize(axis, &vN);
    x = vN.x;
    y = vN.y;
    z = vN.z;
    xSq = (x * x);
    ySq = (y * y);
    zSq = (z * z);
    m[0][0] = (c + (t * xSq));
    m[0][1] = (y * (t * x)) - (s * z);
    m[0][2] = (z * (t * x)) + (s * y);
    m[0][3] = 0;
    m[1][0] = ((y * (t * x)) + (s * z));
    m[1][1] = (c + (t * ySq));
    m[1][2] = ((z * (t * y)) - (s * x));
    m[1][3] = 0;
    m[2][0] = ((z * (t * x)) - (s * y));
    m[2][1] = ((z * (t * y)) + (s * x));
    m[2][2] = (c + (t * zSq));
    m[2][3] = 0;
}

void MTXTransApply(Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT)
{
    assert(src);
    assert(dst);

    if (src != dst) {
        dst[0][0] = src[0][0];
        dst[0][1] = src[0][1];
        dst[0][2] = src[0][2];
        dst[1][0] = src[1][0];
        dst[1][1] = src[1][1];
        dst[1][2] = src[1][2];
        dst[2][0] = src[2][0];
        dst[2][1] = src[2][1];
        dst[2][2] = src[2][2];
    }
    dst[0][3] = (src[0][3] + xT);
    dst[1][3] = (src[1][3] + yT);
    dst[2][3] = (src[2][3] + zT);
}

void C_MTXScale(Mtx m, f32 xS, f32 yS, f32 zS)
{
    assert(m);
    m[0][0] = xS;
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = yS;
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = zS;
    m[2][3] = 0;
}

void MTXScaleApply(Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS)
{
    assert(src);
    assert(dst);
    dst[0][0] = (src[0][0] * xS);
    dst[0][1] = (src[0][1] * xS);
    dst[0][2] = (src[0][2] * xS);
    dst[0][3] = (src[0][3] * xS);
    dst[1][0] = (src[1][0] * yS);
    dst[1][1] = (src[1][1] * yS);
    dst[1][2] = (src[1][2] * yS);
    dst[1][3] = (src[1][3] * yS);
    dst[2][0] = (src[2][0] * zS);
    dst[2][1] = (src[2][1] * zS);
    dst[2][2] = (src[2][2] * zS);
    dst[2][3] = (src[2][3] * zS);
}

void C_MTXQuat(Mtx m, QuaternionPtr q)
{
    f32 s;
    f32 xs;
    f32 ys;
    f32 zs;
    f32 wx;
    f32 wy;
    f32 wz;
    f32 xx;
    f32 xy;
    f32 xz;
    f32 yy;
    f32 yz;
    f32 zz;

    assert(m);
    assert(q);
    assert(q->x || q->y || q->z || q->w);
    s = 2 /
        ((q->w * q->w) + ((q->z * q->z) + ((q->x * q->x) + (q->y * q->y))));
    xs = q->x * s;
    ys = q->y * s;
    zs = q->z * s;
    wx = q->w * xs;
    wy = q->w * ys;
    wz = q->w * zs;
    xx = q->x * xs;
    xy = q->x * ys;
    xz = q->x * zs;
    yy = q->y * ys;
    yz = q->y * zs;
    zz = q->z * zs;
    m[0][0] = (1 - (yy + zz));
    m[0][1] = (xy - wz);
    m[0][2] = (xz + wy);
    m[0][3] = 0;
    m[1][0] = (xy + wz);
    m[1][1] = (1 - (xx + zz));
    m[1][2] = (yz - wx);
    m[1][3] = 0;
    m[2][0] = (xz - wy);
    m[2][1] = (yz + wx);
    m[2][2] = (1 - (xx + yy));
    m[2][3] = 0;
}

void MTXReflect(Mtx m, Vec* p, Vec* n)
{
    f32 vxy;
    f32 vxz;
    f32 vyz;
    f32 pdotn;

    vxy = -2 * n->x * n->y;
    vxz = -2 * n->x * n->z;
    vyz = -2 * n->y * n->z;
    pdotn = 2 * VECDotProduct(p, n);
    m[0][0] = (1 - (2 * n->x * n->x));
    m[0][1] = vxy;
    m[0][2] = vxz;
    m[0][3] = (pdotn * n->x);
    m[1][0] = vxy;
    m[1][1] = (1 - (2 * n->y * n->y));
    m[1][2] = vyz;
    m[1][3] = (pdotn * n->y);
    m[2][0] = vxz;
    m[2][1] = vyz;
    m[2][2] = (1 - (2 * n->z * n->z));
    m[2][3] = (pdotn * n->z);
}

void MTXLookAt(Mtx m, Vec* camPos, Vec* camUp, Vec* target)
{
    Vec vLook;
    Vec vRight;
    Vec vUp;

    assert(m);
    assert(camPos);
    assert(camUp);
    assert(target);

    vLook.x = camPos->x - target->x;
    vLook.y = camPos->y - target->y;
    vLook.z = camPos->z - target->z;
    VECNormalize(&vLook, &vLook);
    VECCrossProduct(camUp, &vLook, &vRight);
    VECNormalize(&vRight, &vRight);
    VECCrossProduct(&vLook, &vRight, &vUp);
    m[0][0] = vRight.x;
    m[0][1] = vRight.y;
    m[0][2] = vRight.z;
    m[0][3] = -((camPos->z * vRight.z) +
                ((camPos->x * vRight.x) + (camPos->y * vRight.y)));
    m[1][0] = vUp.x;
    m[1][1] = vUp.y;
    m[1][2] = vUp.z;
    m[1][3] =
        -((camPos->z * vUp.z) + ((camPos->x * vUp.x) + (camPos->y * vUp.y)));
    m[2][0] = vLook.x;
    m[2][1] = vLook.y;
    m[2][2] = vLook.z;
    m[2][3] = -((camPos->z * vLook.z) +
                ((camPos->x * vLook.x) + (camPos->y * vLook.y)));
}

void C_MTXLookAt(Mtx m, Vec* camPos, Vec* camUp, Vec* target)
{
    Vec vLook;
    Vec vRight;
    Vec vUp;

    vLook.x = camPos->x - target->x;
    vLook.y = camPos->y - target->y;
    vLook.z = camPos->z - target->z;
    VECNormalize(&vLook, &vLook);

    VECCrossProduct(camUp, &vLook, &vRight);
    VECNormalize(&vRight, &vRight);
    VECCrossProduct(&vLook, &vRight, &vUp);

    m[0][0] = vRight.x;
    m[0][1] = vRight.y;
    m[0][2] = vRight.z;
    m[0][3] = -((camPos->z * vRight.z) +
                ((camPos->x * vRight.x) + (camPos->y * vRight.y)));

    m[1][0] = vUp.x;
    m[1][1] = vUp.y;
    m[1][2] = vUp.z;
    m[1][3] =
        -((camPos->z * vUp.z) + ((camPos->x * vUp.x) + (camPos->y * vUp.y)));

    m[2][0] = vLook.x;
    m[2][1] = vLook.y;
    m[2][2] = vLook.z;
    m[2][3] = -((camPos->z * vLook.z) +
                ((camPos->x * vLook.x) + (camPos->y * vLook.y)));
}

void MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS,
                     f32 scaleT, f32 transS, f32 transT)
{
    f32 tmp;

    assert(m);
    assert((t != b));
    assert((l != r));

    tmp = 1 / (r - l);
    m[0][0] = (scaleS * (2 * n * tmp));
    m[0][1] = 0;
    m[0][2] = (scaleS * (tmp * (r + l))) - transS;
    m[0][3] = 0;
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (scaleT * (2 * n * tmp));
    m[1][2] = (scaleT * (tmp * (t + b))) - transT;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = -1;
    m[2][3] = 0;
}

void MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT,
                         f32 transS, f32 transT)
{
    f32 angle;
    f32 cot;

    assert(m);
    assert((fovY > 0.0) && (fovY < 180.0));
    assert(0 != aspect);

    angle = (0.5f * fovY);
    angle = angle * 0.017453293f;
    cot = 1 / tanf(angle);
    m[0][0] = (scaleS * (cot / aspect));
    m[0][1] = 0;
    m[0][2] = -transS;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = (cot * scaleT);
    m[1][2] = -transT;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = -1;
    m[2][3] = 0;
}

void MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT,
                   f32 transS, f32 transT)
{
    f32 tmp;

    assert(m);
    assert((t != b));
    assert((l != r));
    tmp = 1 / (r - l);
    m[0][0] = (2 * tmp * scaleS);
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = (transS + (scaleS * (tmp * -(r + l))));
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (2 * tmp * scaleT);
    m[1][2] = 0;
    m[1][3] = (transT + (scaleT * (tmp * -(t + b))));
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = 0;
    m[2][3] = 1;
}

void C_MTXMultVec(Mtx44 m, Vec* src, Vec* dst)
{
    Vec vTmp;

    assert(m);
    assert(src);
    assert(dst);

    vTmp.x = m[0][3] +
             ((m[0][2] * src->z) + ((m[0][0] * src->x) + (m[0][1] * src->y)));
    vTmp.y = m[1][3] +
             ((m[1][2] * src->z) + ((m[1][0] * src->x) + (m[1][1] * src->y)));
    vTmp.z = m[2][3] +
             ((m[2][2] * src->z) + ((m[2][0] * src->x) + (m[2][1] * src->y)));
    dst->x = vTmp.x;
    dst->y = vTmp.y;
    dst->z = vTmp.z;
}

void C_MTXMultVecArray(Mtx m, Vec* srcBase, Vec* dstBase, u32 count)
{
    u32 i;
    Vec vTmp;

    assert(m);
    assert(srcBase);
    assert(dstBase);

    for (i = 0; i < count; i++) {
        vTmp.x = m[0][3] + ((m[0][2] * srcBase->z) +
                            ((m[0][0] * srcBase->x) + (m[0][1] * srcBase->y)));
        vTmp.y = m[1][3] + ((m[1][2] * srcBase->z) +
                            ((m[1][0] * srcBase->x) + (m[1][1] * srcBase->y)));
        vTmp.z = m[2][3] + ((m[2][2] * srcBase->z) +
                            ((m[2][0] * srcBase->x) + (m[2][1] * srcBase->y)));
        dstBase->x = vTmp.x;
        dstBase->y = vTmp.y;
        dstBase->z = vTmp.z;
        srcBase++;
        dstBase++;
    }
}

void C_MTXMultVecSR(Mtx44 m, Vec* src, Vec* dst)
{
    Vec vTmp;

    assert(m);
    assert(src);
    assert(dst);
    vTmp.x = (m[0][2] * src->z) + ((m[0][0] * src->x) + (m[0][1] * src->y));
    vTmp.y = (m[1][2] * src->z) + ((m[1][0] * src->x) + (m[1][1] * src->y));
    vTmp.z = (m[2][2] * src->z) + ((m[2][0] * src->x) + (m[2][1] * src->y));
    dst->x = vTmp.x;
    dst->y = vTmp.y;
    dst->z = vTmp.z;
}

void MTXMultVecArraySR(Mtx44 m, Vec* srcBase, Vec* dstBase, u32 count)
{
    u32 i;
    Vec vTmp;

    assert(m);
    assert(srcBase);
    assert(dstBase);

    for (i = 0; i < count; i++) {
        vTmp.x = (m[0][2] * srcBase->z) +
                 ((m[0][0] * srcBase->x) + (m[0][1] * srcBase->y));
        vTmp.y = (m[1][2] * srcBase->z) +
                 ((m[1][0] * srcBase->x) + (m[1][1] * srcBase->y));
        vTmp.z = (m[2][2] * srcBase->z) +
                 ((m[2][0] * srcBase->x) + (m[2][1] * srcBase->y));
        dstBase->x = vTmp.x;
        dstBase->y = vTmp.y;
        dstBase->z = vTmp.z;
        srcBase++;
        dstBase++;
    }
}

void MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
    f32 tmp;

    assert(m);
    assert(t != b);
    assert(l != r);
    assert(n != f);
    tmp = 1 / (r - l);
    m[0][0] = (2 * n * tmp);
    m[0][1] = 0;
    m[0][2] = (tmp * (r + l));
    m[0][3] = 0;
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (2 * n * tmp);
    m[1][2] = (tmp * (t + b));
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-n * tmp);
    m[2][3] = (tmp * -(f * n));
    m[3][0] = 0;
    m[3][1] = 0;
    m[3][2] = -1;
    m[3][3] = 0;
}

void MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f)
{
    f32 angle;
    f32 cot;
    f32 tmp;

    assert(m);
    assert((fovY > 0.0) && (fovY < 180.0));
    assert(0.0f != aspect);

    angle = (0.5f * fovY);
    angle = angle * 0.017453293f;
    cot = 1 / tanf(angle);
    m[0][0] = (cot / aspect);
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = (cot);
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-n * tmp);
    m[2][3] = (tmp * -(f * n));
    m[3][0] = 0;
    m[3][1] = 0;
    m[3][2] = -1;
    m[3][3] = 0;
}

void MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
    f32 tmp;

    assert(m);
    assert(t != b);
    assert(l != r);
    assert(n != f);
    tmp = 1 / (r - l);
    m[0][0] = 2 * tmp;
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = (tmp * -(r + l));
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = 2 * tmp;
    m[1][2] = 0;
    m[1][3] = (tmp * -(t + b));
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-1 * tmp);
    m[2][3] = (-f * tmp);
    m[3][0] = 0;
    m[3][1] = 0;
    m[3][2] = 0;
    m[3][3] = 1;
}

/* Keep both SDK symbol families available to game source. */
void PSVECAdd(Vec* a, Vec* b, Vec* c)
{
    C_VECAdd(a, b, c);
}

void PSVECSubtract(Vec* a, Vec* b, Vec* c)
{
    C_VECSubtract(a, b, c);
}

void PSVECScale(Vec* src, Vec* dst, f32 scale)
{
    C_VECScale(src, dst, scale);
}

void PSVECNormalize(Vec* src, Vec* unit)
{
    C_VECNormalize(src, unit);
}

f32 PSVECSquareMag(Vec* v)
{
    return C_VECSquareMag(v);
}

f32 PSVECMag(Vec* v)
{
    return C_VECMag(v);
}

f32 PSVECDotProduct(Vec* a, Vec* b)
{
    return C_VECDotProduct(a, b);
}

void PSVECCrossProduct(Vec* a, Vec* b, Vec* axb)
{
    C_VECCrossProduct(a, b, axb);
}

f32 PSVECSquareDistance(Vec* a, Vec* b)
{
    return C_VECSquareDistance(a, b);
}

void PSMTXIdentity(Mtx m)
{
    C_MTXIdentity(m);
}

void PSMTXCopy(Mtx src, Mtx dst)
{
    C_MTXCopy(src, dst);
}

void PSMTXConcat(Mtx a, Mtx b, Mtx ab)
{
    C_MTXConcat(a, b, ab);
}

void PSMTXTranspose(Mtx src, Mtx xPose)
{
    C_MTXTranspose(src, xPose);
}

u32 PSMTXInverse(Mtx src, Mtx inv)
{
    return C_MTXInverse(src, inv);
}

u32 PSMTXInvXpose(Mtx src, Mtx invX)
{
    return C_MTXInvXpose(src, invX);
}

void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA)
{
    C_MTXRotTrig(m, axis, sinA, cosA);
}

void PSMTXRotAxisRad(Mtx m, Vec* axis, f32 rad)
{
    C_MTXRotAxisRad(m, axis, rad);
}

void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS)
{
    C_MTXScale(m, xS, yS, zS);
}

void PSMTXQuat(Mtx m, QuaternionPtr q)
{
    C_MTXQuat(m, q);
}

void PSMTXMultVec(Mtx44 m, Vec* src, Vec* dst)
{
    C_MTXMultVec(m, src, dst);
}

void PSMTXMultVecArray(Mtx m, Vec* srcBase, Vec* dstBase, u32 count)
{
    C_MTXMultVecArray(m, srcBase, dstBase, count);
}

void PSMTXMultVecSR(Mtx44 m, Vec* src, Vec* dst)
{
    C_MTXMultVecSR(m, src, dst);
}

/* MTXTrans is a macro in release SDK headers. Export both names. */
#undef MTXTrans
void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT)
{
    C_MTXIdentity(m);
    m[0][3] = xT;
    m[1][3] = yT;
    m[2][3] = zT;
}

void MTXTrans(Mtx m, f32 xT, f32 yT, f32 zT)
{
    PSMTXTrans(m, xT, yT, zT);
}

/* ROMtx stores the same affine matrix by columns. */
void PSMTXReorder(Mtx src, ROMtx dest)
{
    ROMtx result;
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            result[column][row] = src[row][column];
        }
    }
    memcpy(dest, result, sizeof(result));
}

static Vec transform_reordered(ROMtx m, Vec src)
{
    Vec result = {
        m[0][0] * src.x + m[1][0] * src.y + m[2][0] * src.z + m[3][0],
        m[0][1] * src.x + m[1][1] * src.y + m[2][1] * src.z + m[3][1],
        m[0][2] * src.x + m[1][2] * src.y + m[2][2] * src.z + m[3][2],
    };
    return result;
}

void PSMTXROMultVecArray(ROMtx* m, Vec* srcBase, Vec* dstBase, u32 count)
{
    for (u32 i = 0; i < count; ++i) {
        dstBase[i] = transform_reordered(*m, srcBase[i]);
    }
}

void PSMTXROSkin2VecArray(ROMtx* m0, ROMtx* m1, f32* wtBase, Vec* srcBase,
                          Vec* dstBase, u32 count)
{
    for (u32 i = 0; i < count; ++i) {
        /* The SDK gives m1 weight w and m0 weight 1 - w. */
        ROMtx blended;
        const f32 weight = wtBase[i];
        for (size_t column = 0; column < 4; ++column) {
            for (size_t row = 0; row < 3; ++row) {
                const f32 a = (*m0)[column][row];
                blended[column][row] = a + weight * ((*m1)[column][row] - a);
            }
        }
        dstBase[i] = transform_reordered(blended, srcBase[i]);
    }
}

/* These SDK functions load signed 16-bit values with a scale of zero. */
void PSMTXROMultS16VecArray(ROMtx* m, S16Vec* srcBase, Vec* dstBase, u32 count)
{
    for (u32 i = 0; i < count; ++i) {
        const Vec source = { srcBase[i].x, srcBase[i].y, srcBase[i].z };
        dstBase[i] = transform_reordered(*m, source);
    }
}

void PSMTXMultS16VecArray(Mtx44* m, S16Vec* srcBase, Vec* dstBase, u32 count)
{
    for (u32 i = 0; i < count; ++i) {
        Vec source = { srcBase[i].x, srcBase[i].y, srcBase[i].z };
        C_MTXMultVec(*m, &source, &dstBase[i]);
    }
}

void MTXInitStack(MTXStack* stack, u32 count)
{
    assert(stack && stack->stackBase && count);
    stack->numMtx = count;
    stack->stackPtr = NULL;
}

static Mtx* next_stack_entry(MTXStack* stack)
{
    assert(stack && stack->stackBase);
    size_t count = stack->stackPtr
                       ? (size_t) (stack->stackPtr - stack->stackBase) + 1
                       : 0;
    if (count >= stack->numMtx) {
        return NULL;
    }
    return stack->stackBase + count;
}

Mtx* MTXPush(MTXStack* stack, Mtx m)
{
    Mtx* next = next_stack_entry(stack);
    if (next) {
        C_MTXCopy(m, *next);
        stack->stackPtr = next;
    }
    return next;
}

Mtx* MTXPushFwd(MTXStack* stack, Mtx m)
{
    Mtx* next = next_stack_entry(stack);
    if (next) {
        if (stack->stackPtr) {
            C_MTXConcat(*stack->stackPtr, m, *next);
        } else {
            C_MTXCopy(m, *next);
        }
        stack->stackPtr = next;
    }
    return next;
}

Mtx* MTXPushInv(MTXStack* stack, Mtx m)
{
    Mtx inverse;
    Mtx* next = next_stack_entry(stack);
    if (!next || !C_MTXInverse(m, inverse)) {
        return NULL;
    }
    if (stack->stackPtr) {
        C_MTXConcat(inverse, *stack->stackPtr, *next);
    } else {
        C_MTXCopy(inverse, *next);
    }
    stack->stackPtr = next;
    return next;
}

Mtx* MTXPushInvXpose(MTXStack* stack, Mtx m)
{
    Mtx inverse;
    if (!C_MTXInvXpose(m, inverse)) {
        return NULL;
    }
    return MTXPushFwd(stack, inverse);
}

Mtx* MTXPop(MTXStack* stack)
{
    assert(stack && stack->stackBase);
    if (stack->stackPtr) {
        stack->stackPtr =
            stack->stackPtr == stack->stackBase ? NULL : stack->stackPtr - 1;
    }
    return stack->stackPtr;
}

Mtx* MTXGetStackPtr(MTXStack* stack)
{
    assert(stack && stack->stackBase);
    return stack->stackPtr;
}
