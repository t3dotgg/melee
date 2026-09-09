#include <Runtime/platform.h>

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sysdolphin/baselib/bytecode.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>

/* These stubs isolate the production evaluator from platform services. */
static size_t live_allocations;
static size_t evaluations;

void* HSD_MemAlloc(ssize_t size)
{
    void* result = malloc(size);
    assert(result != NULL);
    live_allocations++;
    return result;
}

void HSD_Free(void* allocation)
{
    assert(allocation != NULL);
    assert(live_allocations > 0);
    live_allocations--;
    free(allocation);
}

void OSReport(char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void HSD_Panic(char* file, u32 line, char* message)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, message);
    abort();
}

void(__assert)(char* file, u32 line, char* message)
{
    HSD_Panic(file, line, message);
}

s32 HSD_Randi(s32 max_val)
{
    assert(max_val > 0);
    return max_val - 1;
}

f32 HSD_Randf(void)
{
    return 0.25f;
}

typedef struct {
    u8 code[1024];
    size_t length;
} Program;

static void emit(Program* program, u8 byte)
{
    assert(program->length < sizeof(program->code));
    program->code[program->length++] = byte;
}

static void push_bits(Program* program, u32 bits)
{
    emit(program, 6);
    emit(program, bits >> 24);
    emit(program, bits >> 16);
    emit(program, bits >> 8);
    emit(program, bits);
}

static void push_float(Program* program, f32 value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    push_bits(program, bits);
}

static void expect(Program* program, const f32* args, s32 count, f32 expected)
{
    f32 result;
    emit(program, 1);
    result = HSD_ByteCodeEval(program->code, args, count);
    if (fabsf(result - expected) > 0.0001f || isnan(result)) {
        fprintf(stderr, "evaluation %zu: expected %.9g, got %.9g\n",
                evaluations, expected, result);
        abort();
    }
    assert(live_allocations == 0);
    evaluations++;
}

static void test_unary(void)
{
    static const struct {
        u8 opcode;
        f32 value;
        f32 expected;
    } floats[] = {
        { 0x09, 2.5f, -2.5f }, { 0x0C, 99.0f, 0.25f }, { 0x0D, 30.0f, 0.5f },
        { 0x0E, 60.0f, 0.5f }, { 0x0F, 45.0f, 1.0f },  { 0x10, 0.5f, 30.0f },
        { 0x11, 0.5f, 60.0f }, { 0x12, 1.0f, 45.0f },  { 0x13, 1.0f, 0.0f },
        { 0x14, 0.0f, 1.0f },  { 0x15, -2.5f, 2.5f },  { 0x15, 2.5f, 2.5f },
        { 0x16, 9.0f, 3.0f },
    };
    static const struct {
        u8 opcode;
        s32 value;
        f32 expected;
    } integers[] = {
        { 0x0A, -9, 9.0f }, { 0x0B, 99, 1.0f }, { 0x28, -9, 9.0f },
        { 0x28, 9, 9.0f },  { 0x31, 0, 1.0f },  { 0x31, -3, 0.0f },
    };
    for (size_t i = 0; i < sizeof(floats) / sizeof(floats[0]); i++) {
        Program program = { 0 };
        push_float(&program, floats[i].value);
        emit(&program, floats[i].opcode);
        expect(&program, NULL, 0, floats[i].expected);
    }
    for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); i++) {
        Program program = { 0 };
        push_bits(&program, integers[i].value);
        emit(&program, integers[i].opcode);
        emit(&program, 8);
        expect(&program, NULL, 0, integers[i].expected);
    }
    {
        Program program = { 0 };
        push_float(&program, -3.75f);
        emit(&program, 7);
        emit(&program, 8);
        expect(&program, NULL, 0, -3.0f);
    }
    {
        Program program = { 0 };
        push_bits(&program, (u32) -123456);
        emit(&program, 8);
        expect(&program, NULL, 0, -123456.0f);
    }
}

static void test_binary(void)
{
    static const struct {
        u8 opcode;
        f32 left;
        f32 right;
        f32 expected;
    } floats[] = {
        { 0x17, -9.0f, 4.0f, -5.0f },  { 0x18, -9.0f, 4.0f, -13.0f },
        { 0x19, -9.0f, 4.0f, -36.0f }, { 0x1A, -9.0f, 4.0f, -2.25f },
        { 0x1B, -9.0f, 4.0f, -1.0f },  { 0x21, 3.0f, 4.0f, 81.0f },
        { 0x22, 7.0f, -2.0f, -2.0f },  { 0x22, -2.0f, 7.0f, -2.0f },
        { 0x23, -2.0f, 7.0f, 7.0f },   { 0x23, 7.0f, -2.0f, 7.0f },
        { 0x26, 1.0f, 1.0f, 45.0f },   { 0x26, 1.0f, 0.0f, 90.0f },
        { 0x26, -1.0f, 0.0f, -90.0f }, { 0x33, 1.0f, 2.0f, 1.0f },
        { 0x34, 1.0f, 2.0f, 0.0f },    { 0x35, 1.0f, 1.0f, 1.0f },
        { 0x36, 1.0f, 1.0f, 1.0f },    { 0x37, 1.0f, 2.0f, 0.0f },
        { 0x38, 1.0f, 2.0f, 1.0f },
    };
    static const struct {
        u8 opcode;
        s32 left;
        s32 right;
        f32 expected;
    } integers[] = {
        { 0x1C, -9, 4, -5.0f },  { 0x1D, -9, 4, -13.0f },
        { 0x1E, -9, 4, -36.0f }, { 0x1F, -9, 4, -2.0f },
        { 0x20, -9, 4, -1.0f },  { 0x24, 7, -2, -2.0f },
        { 0x24, -2, 7, -2.0f },  { 0x25, -2, 7, 7.0f },
        { 0x25, 7, -2, 7.0f },   { 0x27, -9, -4, -4.0f },
        { 0x29, -1, 2, 1.0f },   { 0x2A, -1, 2, 0.0f },
        { 0x2B, -1, -1, 1.0f },  { 0x2C, -1, -1, 1.0f },
        { 0x2D, -1, 2, 0.0f },   { 0x2E, -1, 2, 1.0f },
        { 0x2F, -1, 2, 1.0f },   { 0x2F, -1, 0, 0.0f },
        { 0x30, -1, 0, 1.0f },   { 0x30, 0, 0, 0.0f },
        { 0x32, -1, 0, 1.0f },   { 0x32, -1, 2, 0.0f },
        { 0x39, -9, 3, 3.0f },   { 0x3A, -12, 3, -9.0f },
        { 0x3B, -9, 3, -12.0f },
    };
    for (size_t i = 0; i < sizeof(floats) / sizeof(floats[0]); i++) {
        Program program = { 0 };
        push_float(&program, floats[i].left);
        push_float(&program, floats[i].right);
        emit(&program, floats[i].opcode);
        if (floats[i].opcode >= 0x33) {
            emit(&program, 8);
        }
        expect(&program, NULL, 0, floats[i].expected);
    }
    for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); i++) {
        Program program = { 0 };
        push_bits(&program, integers[i].left);
        push_bits(&program, integers[i].right);
        emit(&program, integers[i].opcode);
        emit(&program, 8);
        expect(&program, NULL, 0, integers[i].expected);
    }
}

static void test_arguments_and_stack(void)
{
    f32 args[257] = { 0 };
    Program program = { 0 };
    args[0] = -2.5f;
    args[256] = 7.0f;
    emit(&program, 0);
    emit(&program, 2);
    emit(&program, 0);
    emit(&program, 0);
    emit(&program, 2);
    emit(&program, 1);
    emit(&program, 0);
    emit(&program, 0x3C);
    emit(&program, 1);
    emit(&program, 0x17);
    emit(&program, 0x17);
    expect(&program, args, 257, 2.0f);

    program = (Program){ 0 };
    push_float(&program, 42.0f);
    for (int i = 0; i < 50; i++) {
        push_float(&program, i);
    }
    emit(&program, 5);
    emit(&program, 50);
    expect(&program, NULL, 0, 42.0f);

    program = (Program){ 0 };
    push_float(&program, 5.0f);
    emit(&program, 5);
    emit(&program, 2);
    push_float(&program, 17.0f);
    push_float(&program, 42.0f);
    expect(&program, NULL, 0, 42.0f);
}

static void test_branches(void)
{
    for (int condition = 0; condition >= -1; condition--) {
        Program program = { 0 };
        push_float(&program, 100.0f);
        push_bits(&program, condition);
        emit(&program, 3);
        emit(&program, 0);
        emit(&program, 8);
        push_float(&program, -10.0f);
        emit(&program, 4);
        emit(&program, 0);
        emit(&program, 5);
        push_float(&program, 42.0f);
        emit(&program, 0x17);
        expect(&program, NULL, 0, condition ? 142.0f : 90.0f);
    }
}

int main(void)
{
    assert(sizeof(void*) == 8);
    assert(HSD_ByteCodeEval(NULL, NULL, 0) == 0.0f);
    test_unary();
    test_binary();
    test_arguments_and_stack();
    test_branches();
    printf("scene bytecode: %zu evaluations passed\n", evaluations);
    return 0;
}
