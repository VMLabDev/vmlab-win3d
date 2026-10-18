/* Repro for dynamically indexed shader inputs and outputs that docs/ds.md
 * left open (docs/shaders2.md):
 *
 *   - vertex shader input ranges (dcl_indexrange v1.xyzw 4 in a VS);
 *   - tessellation-factor ranges in hull and domain shaders;
 *   - output ranges outside a hull shader's patch phases.
 *
 * Each case draws four quads or patches, one per quadrant of a 64x64
 * R32G32B32A32_FLOAT target (four targets for ps-out). Each shader indexes
 * its array with (quadrant + sel) & 3 or a value derived from it; sel comes
 * from a constant buffer, so no compiler can fold the index. The centre pixel
 * of each quadrant is read back for sel 0 to 3 and compared with the value
 * computed on the CPU.
 *
 * d3dcompiler_47 refuses some of these constructs: indexing an output with a
 * register (X3500) or a tessellation factor in a domain shader (X4576), and
 * in a hull shader's join phase it reads tessellation factors only with
 * immediate indices. For those cases the probe compiles a skeleton shader in
 * HLSL that has the signatures and declarations the case needs, replaces its
 * code (all of it, or one hull shader phase) with tokens assembled here,
 * recomputes the DXBC checksum, and prints the disassembly of the result.
 *
 *   d3d11_index2.exe [--warp] [--case NAME] [--asm DIR] [--log FILE] [--asm-only]
 *
 * --asm-only builds and disassembles every shader without a device.
 *
 * Every case gets its own device, so one that removes the device does not
 * hide the others. Exit 0 when every case matches, 1 otherwise. Run once on
 * HARDWARE and once with --warp; both must print the same values.
 * Built by docs/evidence/shaders2/src/build.sh. */
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static FILE *g_log;

static void say(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

#define RT 64

/* ---- HLSL ------------------------------------------------------------- */

/* val(p, c, k): the value of array element k at corner c of quadrant p.
 * mark(p, k): what the hand-assembled output cases write to element k. */
static const char common_hlsl[] =
    "cbuffer C : register(b0) { uint sel; uint3 pad; };\n"
    "float4 val(uint p, uint c, uint k) {\n"
    "  return float4(1000 * c + 100 * p + 10 * k + 1, 1000 * c + 100 * p + 10 * k + 2,\n"
    "                1000 * c + 100 * p + 10 * k + 3, 1000 * c + 100 * p + 10 * k + 4); }\n"
    "uint idx(uint p) { return (p + sel) & 3; }\n"
    "float3 patch_pos(uint p, uint c) {\n"
    "  float2 org = float2(p % 2, p / 2) - 1.0;\n"
    "  return float3(org + float2(c == 1 || c == 2, c >= 2), 0.5); }\n"
    /* Input-assembler vertex (untessellated cases): POS v0, QUAD v1, ATTR v2..v5. */
    "struct VI { float4 pos : POS; uint p : QUAD; float4 a[4] : ATTR; };\n"
    "struct V4 { float4 pos : SV_Position; nointerpolation uint p : QUAD;\n"
    "  nointerpolation float4 a[4] : ATTR; };\n"
    "V4 vs_quad(VI i) { V4 o; o.pos = i.pos; o.p = i.p; o.a = i.a; return o; }\n"
    "struct O1 { float4 pos : SV_Position; nointerpolation float4 v : VAL; };\n"
    "float4 ps_one(O1 i) : SV_Target { return i.v; }\n"
    /* Four-element output (hand-assembled output cases): VAL0..3 in o1..o4. */
    "struct O4 { float4 pos : SV_Position; nointerpolation float4 a[4] : VAL; };\n"
    "float4 ps_sum(O4 i) : SV_Target { return i.a[0] + 10 * i.a[1] + 100 * i.a[2] + 1000 * i.a[3]; }\n"
    /* Tessellated pipeline: control point POS o0, QUAD o1, ATTR o2..o5;
     * patch constants: four edge factors vpc0..3, two inside factors
     * vpc4..5, CONSTS vpc6. */
    "struct CP { float3 pos : POS; nointerpolation uint p : QUAD; float4 a[4] : ATTR; };\n"
    "CP vs_tess(uint v : SV_VertexID) {\n"
    "  CP o; uint p = v / 4, c = v % 4;\n"
    "  o.pos = patch_pos(p, c); o.p = p; o.a[0] = o.a[1] = o.a[2] = o.a[3] = 0; return o; }\n"
    "struct HSC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor;\n"
    "  float4 c : CONSTS; };\n"
    /* Fixed factors: e[k] = 1 + ((k + p) & 1), i = {1, 2}. */
    "HSC hsc_fixed(InputPatch<CP, 4> ip) {\n"
    "  HSC o; [unroll] for (uint k = 0; k < 4; k++) o.e[k] = 1 + ((k + ip[0].p) & 1);\n"
    "  o.i[0] = 1; o.i[1] = 2; o.c = 0; return o; }\n"
    "#define HS_ATTRS [domain(\"quad\")] [partitioning(\"integer\")] [outputtopology(\"triangle_cw\")] \\\n"
    "  [outputcontrolpoints(4)]\n"
    "HS_ATTRS [patchconstantfunc(\"hsc_fixed\")]\n"
    "CP hs_fixed(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n"
    "float4 ds_pos(float2 uv, const OutputPatch<CP, 4> q) { return float4(uv + q[0].pos.xy, 0.5, 1); }\n";

/* ---- DXBC assembly ---------------------------------------------------- */

/* Token values from d3d10TokenizedProgramFormat.hpp and
 * d3d11TokenizedProgramFormat.hpp (Windows WDK). */
enum {
    OP_ADD = 0, OP_AND = 1, OP_IADD = 30, OP_MAD = 50, OP_MOV = 54, OP_RET = 62, OP_UTOF = 86,
    OP_CUSTOMDATA = 53, OP_DCL_INDEX_RANGE = 91, OP_DCL_INPUT = 95, OP_DCL_TEMPS = 104,
    OP_HS_DECLS = 113, OP_HS_CONTROL_POINT_PHASE = 114, OP_HS_FORK_PHASE = 115,
    OP_HS_JOIN_PHASE = 116, OP_EMIT_STREAM = 117,
};
enum {
    T_TEMP = 0, T_INPUT = 1, T_OUTPUT = 2, T_IMM32 = 4, T_CB = 8, T_STREAM = 16,
    T_OUTPUT_CP_ID = 22, T_ICP = 25, T_PATCH = 27, T_DOMAIN = 28,
};
enum { REP_IMM = 0, REP_REL = 2, REP_IMM_REL = 3 };

typedef std::vector<uint32_t> Toks;

struct Index {
    uint32_t imm;
    Toks rel; /* empty: immediate; else the relative operand (register + imm) */
};

static uint32_t mask_bits(const char *m)
{
    uint32_t b = 0;
    for (; *m; m++)
        b |= 1u << (*m == 'x' ? 0 : *m == 'y' ? 1 : *m == 'z' ? 2 : 3);
    return b;
}

static uint32_t swizzle_bits(const char *s)
{
    uint32_t b = 0, n = (uint32_t)strlen(s);
    for (uint32_t i = 0; i < 4; i++) {
        char c = s[i < n ? i : n - 1];
        b |= (uint32_t)(c == 'x' ? 0 : c == 'y' ? 1 : c == 'z' ? 2 : 3) << (2 * i);
    }
    return b;
}

/* An operand. sel is a mask ("xy") for a destination, a swizzle ("xyzw",
 * "x" replicated) for a source; one_comp makes a select-1 operand, as
 * relative indices are encoded. */
static Toks operand(uint32_t type, bool dst, const char *sel, std::vector<Index> idx, bool one_comp = false)
{
    uint32_t t = 2 /* 4 components */ | (type << 12) | ((uint32_t)idx.size() << 20);
    if (dst)
        t |= (0u << 2) | (mask_bits(sel) << 4);
    else if (one_comp)
        t |= (2u << 2) | (swizzle_bits(sel) & 3) << 4;
    else
        t |= (1u << 2) | (swizzle_bits(sel) << 4);
    for (size_t i = 0; i < idx.size(); i++)
        t |= (uint32_t)(idx[i].rel.empty() ? REP_IMM : REP_IMM_REL) << (22 + 3 * i);
    Toks out{t};
    for (const Index &i : idx) {
        out.push_back(i.imm);
        out.insert(out.end(), i.rel.begin(), i.rel.end());
    }
    return out;
}
static Index I(uint32_t imm) { return Index{imm, {}}; }
/* reg[rN.c + imm] */
static Index R(uint32_t temp, char c, uint32_t imm)
{
    char s[2] = {c, 0};
    return Index{imm, operand(T_TEMP, false, s, {I(temp)}, true)};
}
static Toks dst(uint32_t type, const char *mask, std::vector<Index> idx) { return operand(type, true, mask, idx); }
static Toks src(uint32_t type, const char *swz, std::vector<Index> idx) { return operand(type, false, swz, idx); }
static Toks rd(uint32_t n, const char *mask) { return dst(T_TEMP, mask, {I(n)}); }
static Toks rs(uint32_t n, const char *swz) { return src(T_TEMP, swz, {I(n)}); }
static Toks cb0(const char *swz) { return src(T_CB, swz, {I(0), I(0)}); }
/* A one-component register without an index (vOutputControlPointID) and a
 * stream (m0), as d3dcompiler encodes them. */
static Toks src_1c(uint32_t type) { return Toks{1 | (type << 12)}; }
static Toks stream(uint32_t n) { return Toks{(T_STREAM << 12) | (1u << 20), n}; }
static Toks imm1u(uint32_t v) { return Toks{1 /* 1 component */ | (T_IMM32 << 12), v}; }
static Toks imm4f(float a, float b, float c, float d)
{
    Toks t{2 | (T_IMM32 << 12)};
    for (float f : {a, b, c, d}) {
        uint32_t u;
        memcpy(&u, &f, 4);
        t.push_back(u);
    }
    return t;
}

static void ins(Toks &out, uint32_t op, std::vector<Toks> ops, std::vector<uint32_t> extra = {})
{
    size_t start = out.size();
    out.push_back(op);
    for (const Toks &o : ops)
        out.insert(out.end(), o.begin(), o.end());
    out.insert(out.end(), extra.begin(), extra.end());
    out[start] |= (uint32_t)(out.size() - start) << 24;
}

static uint32_t ins_len(const uint32_t *t)
{
    if ((t[0] & 0x7ff) == OP_CUSTOMDATA)
        return t[1];
    return (t[0] >> 24) & 0x7f;
}

static bool is_decl(uint32_t op)
{
    return op == OP_CUSTOMDATA || (op >= 88 && op <= 106) || (op >= 143 && op <= 161) || op == 206;
}

static bool is_phase(uint32_t op) { return op >= OP_HS_DECLS && op <= OP_HS_JOIN_PHASE; }

/* The body of a hand-assembled case: declarations that follow the
 * skeleton's own (dcl_temps comes from here), then the code. */
struct Body {
    Toks decls, code;
};

/* Replace the code of a skeleton program. For a hull shader, phase names
 * the phase to replace (its declarations are kept); 0 means the whole
 * program. The skeleton's dcl_temps in the replaced part is dropped. */
static Toks splice(const uint32_t *shex, uint32_t phase, const Body &b)
{
    uint32_t n = shex[1];
    Toks out{shex[0], 0};
    uint32_t i = 2;
    if (phase) {
        while (i < n && (shex[i] & 0x7ff) != phase) {
            out.insert(out.end(), shex + i, shex + i + ins_len(shex + i));
            i += ins_len(shex + i);
        }
        out.push_back(shex[i]); /* the phase token */
        i += ins_len(shex + i);
    }
    /* Declarations of the replaced part. */
    while (i < n && is_decl(shex[i] & 0x7ff)) {
        if ((shex[i] & 0x7ff) != OP_DCL_TEMPS)
            out.insert(out.end(), shex + i, shex + i + ins_len(shex + i));
        i += ins_len(shex + i);
    }
    out.insert(out.end(), b.decls.begin(), b.decls.end());
    out.insert(out.end(), b.code.begin(), b.code.end());
    /* Skip the skeleton's code up to the next phase. */
    while (i < n && !is_phase(shex[i] & 0x7ff))
        i += ins_len(shex + i);
    out.insert(out.end(), shex + i, shex + n);
    out[1] = (uint32_t)out.size();
    return out;
}

/* ---- DXBC container and checksum ------------------------------------- */

static void md5_transform(uint32_t s[4], const uint32_t m[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint32_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) f = (b & c) | (~b & d), g = i;
        else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
        else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
        else f = c ^ (b | ~d), g = (7 * i) % 16;
        uint32_t t = d;
        d = c;
        c = b;
        uint32_t x = a + f + K[i] + m[g];
        b = b + ((x << S[i]) | (x >> (32 - S[i])));
        a = t;
    }
    s[0] += a, s[1] += b, s[2] += c, s[3] += d;
}

/* The DXBC checksum: MD5 over everything after the checksum field, with
 * the bit count moved to the front of the last block and (bits >> 2) | 1 in
 * its last word. */
static void dxbc_checksum(const uint8_t *data, size_t size, uint32_t out[4])
{
    const uint8_t *p = data + 20;
    uint32_t len = (uint32_t)(size - 20), bits = len * 8;
    uint32_t s[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint32_t m[16];
    uint32_t full = len & ~63u;
    for (uint32_t o = 0; o < full; o += 64) {
        memcpy(m, p + o, 64);
        md5_transform(s, m);
    }
    uint32_t rem = len - full;
    uint8_t blk[64];
    if (rem >= 56) {
        memset(blk, 0, 64);
        memcpy(blk, p + full, rem);
        blk[rem] = 0x80;
        memcpy(m, blk, 64);
        md5_transform(s, m);
        memset(m, 0, 64);
        m[0] = bits;
        m[15] = (bits >> 2) | 1;
        md5_transform(s, m);
    } else {
        memset(blk, 0, 64);
        memcpy(blk, &bits, 4);
        memcpy(blk + 4, p + full, rem);
        blk[4 + rem] = 0x80;
        uint32_t last = (bits >> 2) | 1;
        memcpy(blk + 60, &last, 4);
        memcpy(m, blk, 64);
        md5_transform(s, m);
    }
    memcpy(out, s, 16);
}

static bool checksum_ok(const std::vector<uint8_t> &dxbc)
{
    uint32_t c[4];
    dxbc_checksum(dxbc.data(), dxbc.size(), c);
    return !memcmp(c, dxbc.data() + 4, 16);
}

/* A DXBC container with its SHEX (or SHDR) chunk replaced by code. */
static std::vector<uint8_t> replace_code(const uint8_t *dxbc, const Toks &code)
{
    uint32_t count, offs[32];
    memcpy(&count, dxbc + 28, 4);
    memcpy(offs, dxbc + 32, 4 * count);
    std::vector<std::vector<uint8_t>> chunks;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size;
        memcpy(&size, dxbc + offs[i] + 4, 4);
        std::vector<uint8_t> c(dxbc + offs[i], dxbc + offs[i] + 8 + size);
        if (!memcmp(c.data(), "SHEX", 4) || !memcmp(c.data(), "SHDR", 4)) {
            uint32_t sz = (uint32_t)(code.size() * 4);
            c.resize(8 + sz);
            memcpy(c.data() + 4, &sz, 4);
            memcpy(c.data() + 8, code.data(), sz);
        }
        chunks.push_back(c);
    }
    std::vector<uint8_t> out(32 + 4 * count);
    memcpy(out.data(), dxbc, 24); /* magic, checksum (rewritten below), version */
    memcpy(out.data() + 28, &count, 4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t o = (uint32_t)out.size();
        memcpy(out.data() + 32 + 4 * i, &o, 4);
        out.insert(out.end(), chunks[i].begin(), chunks[i].end());
    }
    uint32_t total = (uint32_t)out.size();
    memcpy(out.data() + 24, &total, 4);
    uint32_t c[4];
    dxbc_checksum(out.data(), out.size(), c);
    memcpy(out.data() + 4, c, 16);
    return out;
}

static const uint32_t *find_code(const uint8_t *dxbc)
{
    uint32_t count, off;
    memcpy(&count, dxbc + 28, 4);
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&off, dxbc + 32 + 4 * i, 4);
        if (!memcmp(dxbc + off, "SHEX", 4) || !memcmp(dxbc + off, "SHDR", 4))
            return (const uint32_t *)(dxbc + off + 8);
    }
    return NULL;
}

/* ---- Hand-assembled bodies ------------------------------------------- */

/* r0.x = (p + sel) & 3 from a uint operand p; r1 = mark(p, idx):
 * (p + 1, sel + 1, idx + 1, 1). */
static void index_and_mark(Toks &c, const Toks &p)
{
    ins(c, OP_IADD, {rd(0, "x"), p, cb0("x")});
    ins(c, OP_AND, {rd(0, "x"), rs(0, "x"), imm1u(3)});
    ins(c, OP_UTOF, {rd(1, "x"), p});
    ins(c, OP_UTOF, {rd(1, "y"), cb0("x")});
    ins(c, OP_UTOF, {rd(1, "z"), rs(0, "x")});
    ins(c, OP_ADD, {rd(1, "xyz"), rs(1, "xyzx"), imm4f(1, 1, 1, 0)});
    ins(c, OP_MOV, {rd(1, "w"), imm4f(0, 0, 0, 1)});
}

static Toks dcl_range(uint32_t type, uint32_t first, const char *mask, uint32_t count)
{
    Toks t;
    ins(t, OP_DCL_INDEX_RANGE, {dst(type, mask, {I(first)})}, {count});
    return t;
}
static Toks dcl_temps(uint32_t n)
{
    Toks t;
    ins(t, OP_DCL_TEMPS, {}, {n});
    return t;
}
static void cat(Toks &a, const Toks &b) { a.insert(a.end(), b.begin(), b.end()); }

/* o1..o4 = 0, then o[r0.x + 1] = r1 (or the range at first). */
static void store_marked(Toks &c, uint32_t first)
{
    for (uint32_t k = 0; k < 4; k++)
        ins(c, OP_MOV, {dst(T_OUTPUT, "xyzw", {I(first + k)}), imm4f(0, 0, 0, 0)});
    ins(c, OP_MOV, {dst(T_OUTPUT, "xyzw", {R(0, 'x', first)}), rs(1, "xyzw")});
}

static Body body_vs_out()
{
    Body b;
    cat(b.decls, dcl_range(T_OUTPUT, 1, "xyzw", 4));
    cat(b.decls, dcl_temps(2));
    ins(b.code, OP_MOV, {dst(T_OUTPUT, "xyzw", {I(0)}), src(T_INPUT, "xyzw", {I(0)})});
    index_and_mark(b.code, src(T_INPUT, "x", {I(1)}));
    store_marked(b.code, 1);
    ins(b.code, OP_RET, {});
    return b;
}

static void ds_position(Toks &c)
{
    ins(c, OP_ADD, {dst(T_OUTPUT, "xy", {I(0)}), src(T_DOMAIN, "xyxx", {}), src(T_ICP, "xyxx", {I(0), I(0)})});
    ins(c, OP_MOV, {dst(T_OUTPUT, "zw", {I(0)}), imm4f(0, 0, 0.5f, 1)});
}

static Body body_ds_out()
{
    Body b;
    cat(b.decls, dcl_range(T_OUTPUT, 1, "xyzw", 4));
    cat(b.decls, dcl_temps(2));
    ds_position(b.code);
    index_and_mark(b.code, src(T_ICP, "x", {I(0), I(1)}));
    store_marked(b.code, 1);
    ins(b.code, OP_RET, {});
    return b;
}

/* Domain shader: (e[idx], i[idx & 1], 0, 1) through vpc[r0.x + 0] and
 * vpc[r0.y + 4]. */
static Body body_ds_tf()
{
    Body b;
    cat(b.decls, dcl_range(T_PATCH, 0, "x", 4));
    cat(b.decls, dcl_range(T_PATCH, 4, "x", 2));
    cat(b.decls, dcl_temps(2));
    ds_position(b.code);
    ins(b.code, OP_IADD, {rd(0, "x"), src(T_ICP, "x", {I(0), I(1)}), cb0("x")});
    ins(b.code, OP_AND, {rd(0, "x"), rs(0, "x"), imm1u(3)});
    ins(b.code, OP_AND, {rd(0, "y"), rs(0, "x"), imm1u(1)});
    ins(b.code, OP_MOV, {rd(1, "x"), src(T_PATCH, "x", {R(0, 'x', 0)})});
    ins(b.code, OP_MOV, {rd(1, "y"), src(T_PATCH, "x", {R(0, 'y', 4)})});
    ins(b.code, OP_MOV, {rd(1, "zw"), imm4f(0, 0, 0, 1)});
    ins(b.code, OP_MOV, {dst(T_OUTPUT, "xyzw", {I(1)}), rs(1, "xyzw")});
    ins(b.code, OP_RET, {});
    return b;
}

/* Hull shader join phase: o6.x = e[idx] + 10 * i[sel & 1] through
 * vpc[r0.x + 0] and vpc[r0.y + 4]. The skeleton's join phase declares
 * vpc0..3, vicp[4][1] and o6.x; vpc4 and vpc5 are declared here. */
static Body body_hs_join_tf()
{
    Body b;
    ins(b.decls, OP_DCL_INPUT, {dst(T_PATCH, "x", {I(4)})});
    ins(b.decls, OP_DCL_INPUT, {dst(T_PATCH, "x", {I(5)})});
    cat(b.decls, dcl_range(T_PATCH, 0, "x", 4));
    cat(b.decls, dcl_range(T_PATCH, 4, "x", 2));
    cat(b.decls, dcl_temps(2));
    ins(b.code, OP_IADD, {rd(0, "x"), src(T_ICP, "x", {I(0), I(1)}), cb0("x")});
    ins(b.code, OP_AND, {rd(0, "x"), rs(0, "x"), imm1u(3)});
    ins(b.code, OP_AND, {rd(0, "y"), cb0("x"), imm1u(1)});
    ins(b.code, OP_MOV, {rd(1, "x"), src(T_PATCH, "x", {R(0, 'x', 0)})});
    ins(b.code, OP_MAD, {dst(T_OUTPUT, "x", {I(6)}), src(T_PATCH, "x", {R(0, 'y', 4)}), imm4f(10, 10, 10, 10),
                          rs(1, "x")});
    ins(b.code, OP_RET, {});
    return b;
}

/* Hull shader control-point phase: pass POS and QUAD through, ATTR o2..o5
 * = 0 except o[r0.x + 2] = mark. The phase reads its input control points
 * as v[n][r], as d3dcompiler declares them there. */
static Body body_hs_cp_out()
{
    Body b;
    cat(b.decls, dcl_range(T_OUTPUT, 2, "xyzw", 4));
    cat(b.decls, dcl_temps(3));
    ins(b.code, OP_MOV, {rd(2, "x"), src_1c(T_OUTPUT_CP_ID)});
    ins(b.code, OP_MOV, {dst(T_OUTPUT, "xyz", {I(0)}), src(T_INPUT, "xyzx", {R(2, 'x', 0), I(0)})});
    ins(b.code, OP_MOV, {dst(T_OUTPUT, "x", {I(1)}), src(T_INPUT, "x", {R(2, 'x', 0), I(1)})});
    index_and_mark(b.code, src(T_INPUT, "x", {R(2, 'x', 0), I(1)}));
    store_marked(b.code, 2);
    ins(b.code, OP_RET, {});
    return b;
}

static Body body_gs_out()
{
    Body b;
    cat(b.decls, dcl_range(T_OUTPUT, 1, "xyzw", 4));
    cat(b.decls, dcl_temps(2));
    index_and_mark(b.code, src(T_INPUT, "x", {I(0), I(1)}));
    for (uint32_t v = 0; v < 3; v++) {
        ins(b.code, OP_MOV, {dst(T_OUTPUT, "xyzw", {I(0)}), src(T_INPUT, "xyzw", {I(v), I(0)})});
        store_marked(b.code, 1);
        ins(b.code, OP_EMIT_STREAM, {stream(0)});
    }
    ins(b.code, OP_RET, {});
    return b;
}

/* Pixel shader: SV_Target0..3 (o0..o3) = 0 except o[r0.x + 0] = mark. */
static Body body_ps_out()
{
    Body b;
    cat(b.decls, dcl_range(T_OUTPUT, 0, "xyzw", 4));
    cat(b.decls, dcl_temps(2));
    index_and_mark(b.code, src(T_INPUT, "x", {I(1)}));
    store_marked(b.code, 0);
    ins(b.code, OP_RET, {});
    return b;
}

/* ---- Cases ------------------------------------------------------------ */

enum { VS, HS, DS, GS, PS, STAGES };
static const char *const stage_target[STAGES] = {"vs_5_0", "hs_5_0", "ds_5_0", "gs_5_0", "ps_5_0"};

struct Stage {
    const char *entry;       /* HLSL entry point (the skeleton when asm is set) */
    Body (*asm_body)();      /* hand-assembled code, or NULL */
    uint32_t phase;          /* hull shader phase the code replaces */
};

struct Case {
    const char *name;
    const char *what;
    bool tess;
    unsigned targets; /* render targets; quadrant values come from target 0 unless 4 */
    Stage st[STAGES];
    const char *hlsl;  /* appended to common_hlsl */
    /* Expected value of render target rt for quadrant p at this sel. */
    void (*expect)(unsigned p, unsigned sel, unsigned rt, float *e);
};

static void val(unsigned p, unsigned c, unsigned k, float *e)
{
    for (int i = 0; i < 4; i++)
        e[i] = 1000.0f * c + 100.0f * p + 10.0f * k + i + 1;
}
static unsigned idx(unsigned p, unsigned sel) { return (p + sel) & 3; }
static float edge(unsigned p, unsigned k) { return 1.0f + ((k + p) & 1); }
static const float inside[2] = {1, 2};

static void e_vs_in(unsigned p, unsigned sel, unsigned, float *e) { val(p, 0, idx(p, sel), e); }

/* mark(p, idx) at element idx, summed with weights 1, 10, 100, 1000. */
static void e_marked(unsigned p, unsigned sel, unsigned, float *e)
{
    unsigned i = idx(p, sel);
    float w = i == 0 ? 1.0f : i == 1 ? 10.0f : i == 2 ? 100.0f : 1000.0f;
    e[0] = w * (p + 1), e[1] = w * (sel + 1), e[2] = w * (i + 1), e[3] = w;
}
static void e_ps_out(unsigned p, unsigned sel, unsigned rt, float *e)
{
    unsigned i = idx(p, sel);
    if (rt != i) {
        e[0] = e[1] = e[2] = e[3] = 0;
        return;
    }
    e[0] = (float)(p + 1), e[1] = (float)(sel + 1), e[2] = (float)(i + 1), e[3] = 1;
}
/* hs-tf: the hull shader writes e[k] = 1 + ((k + p + sel) & 1),
 * i[k] = 1 + ((k + sel) & 1); the domain shader returns
 * (e0 + 10 e1, e2 + 10 e3, i0, i1). */
static void e_hs_tf(unsigned p, unsigned sel, unsigned, float *e)
{
    float f[4];
    for (unsigned k = 0; k < 4; k++)
        f[k] = 1.0f + ((k + p + sel) & 1);
    e[0] = f[0] + 10 * f[1], e[1] = f[2] + 10 * f[3];
    e[2] = 1.0f + (sel & 1), e[3] = 1.0f + ((1 + sel) & 1);
}
static void e_hs_join(unsigned p, unsigned sel, unsigned, float *e)
{
    e[0] = edge(p, idx(p, sel)), e[1] = inside[sel & 1], e[2] = 0, e[3] = 1;
}
static void e_hs_join_tf(unsigned p, unsigned sel, unsigned, float *e)
{
    e[0] = edge(p, idx(p, sel)) + 10 * inside[sel & 1], e[1] = inside[sel & 1], e[2] = 0, e[3] = 1;
}
static void e_ds_tf(unsigned p, unsigned sel, unsigned, float *e)
{
    unsigned i = idx(p, sel);
    e[0] = edge(p, i), e[1] = inside[i & 1], e[2] = 0, e[3] = 1;
}

/* Skeletons and stages shared by several cases. */
#define HS_JOIN_HLSL                                                                          \
    "HSC hsc_join(InputPatch<CP, 4> ip) {\n"                                                  \
    "  HSC o; [unroll] for (uint k = 0; k < 4; k++) o.e[k] = 1 + ((k + ip[0].p) & 1);\n"      \
    "  o.i[0] = 1; o.i[1] = 2;\n"                                                             \
    "  o.c = float4(o.e[idx(ip[0].p)], o.i[sel & 1], 0, 0); return o; }\n"                    \
    "HS_ATTRS [patchconstantfunc(\"hsc_join\")]\n"                                            \
    "CP hs_join(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n"    \
    "[domain(\"quad\")] O1 ds_c(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n" \
    "  O1 o; o.pos = ds_pos(uv, q); o.v = float4(c.c.xy, 0, 1); return o; }\n"

static const Case cases[] = {
    /* Compiled by d3dcompiler_47. */
    {"vs-in", "vertex shader reads input-assembler input a[idx] (dcl_indexrange v2.xyzw 4)", false, 1,
     {{"vs_in"}, {}, {}, {}, {"ps_one"}},
     "O1 vs_in(VI i) { O1 o; o.pos = i.pos; o.v = i.a[idx(i.p)]; return o; }\n",
     e_vs_in},
    {"hs-tf", "hull shader fork phases write e[k] and i[k] through o[r + 0] and o[r + 4] (tessellation factor ranges)",
     true, 1,
     {{"vs_tess"}, {"hs_tf"}, {"ds_tf"}, {}, {"ps_one"}},
     "HSC hsc_tf(InputPatch<CP, 4> ip) {\n"
     "  HSC o; [unroll] for (uint k = 0; k < 4; k++) o.e[k] = 1 + ((k + ip[0].p + sel) & 1);\n"
     "  [unroll] for (uint j = 0; j < 2; j++) o.i[j] = 1 + ((j + sel) & 1); o.c = 0; return o; }\n"
     "HS_ATTRS [patchconstantfunc(\"hsc_tf\")]\n"
     "CP hs_tf(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) { return p[i]; }\n"
     "[domain(\"quad\")] O1 ds_tf(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q);\n"
     "  o.v = float4(c.e[0] + 10 * c.e[1], c.e[2] + 10 * c.e[3], c.i[0], c.i[1]); return o; }\n",
     e_hs_tf},
    {"hs-join", "hull shader join phase reads the tessellation factors vpc0..vpc3 (immediate indices)", true, 1,
     {{"vs_tess"}, {"hs_join"}, {"ds_c"}, {}, {"ps_one"}}, HS_JOIN_HLSL, e_hs_join},
    /* Hand-assembled code in a d3dcompiler_47 skeleton. */
    {"hs-join-tf", "hull shader join phase reads vpc[r + 0] and vpc[r + 4] (tessellation factor ranges, assembled)",
     true, 1,
     {{"vs_tess"}, {"hs_join", body_hs_join_tf, OP_HS_JOIN_PHASE}, {"ds_c"}, {}, {"ps_one"}}, HS_JOIN_HLSL,
     e_hs_join_tf},
    {"ds-tf", "domain shader reads vpc[r + 0] and vpc[r + 4] (tessellation factor ranges, assembled)", true, 1,
     {{"vs_tess"}, {"hs_fixed"}, {"ds_tf_skel", body_ds_tf}, {}, {"ps_one"}},
     "[domain(\"quad\")] O1 ds_tf_skel(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q);\n"
     "  o.v = float4(c.e[0] + c.e[1] + c.e[2] + c.e[3] + c.i[0] + c.i[1], q[0].p + sel, 0, 1); return o; }\n",
     e_ds_tf},
    {"vs-out", "vertex shader writes o[r + 1] (output range o1..o4, assembled)", false, 1,
     {{"vs_out_skel", body_vs_out}, {}, {}, {}, {"ps_sum"}},
     "O4 vs_out_skel(VI i) { O4 o; o.pos = i.pos; o.a[0] = o.a[1] = o.a[2] = o.a[3] = i.p + sel; return o; }\n",
     e_marked},
    {"hs-cp-out", "hull shader control-point phase writes o[r + 2] (output range o2..o5, assembled)", true, 1,
     {{"vs_tess"}, {"hs_cp_skel", body_hs_cp_out, OP_HS_CONTROL_POINT_PHASE}, {"ds_sum"}, {}, {"ps_one"}},
     "HS_ATTRS [patchconstantfunc(\"hsc_fixed\")]\n"
     "CP hs_cp_skel(InputPatch<CP, 4> p, uint i : SV_OutputControlPointID) {\n"
     "  CP o; o.pos = p[i].pos; o.p = p[i].p; o.a[0] = o.a[1] = o.a[2] = o.a[3] = p[i].p + sel; return o; }\n"
     "[domain(\"quad\")] O1 ds_sum(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O1 o; o.pos = ds_pos(uv, q);\n"
     "  o.v = q[0].a[0] + 10 * q[0].a[1] + 100 * q[0].a[2] + 1000 * q[0].a[3]; return o; }\n",
     e_marked},
    {"ds-out", "domain shader writes o[r + 1] (output range o1..o4, assembled)", true, 1,
     {{"vs_tess"}, {"hs_fixed"}, {"ds_out_skel", body_ds_out}, {}, {"ps_sum"}},
     "[domain(\"quad\")] O4 ds_out_skel(HSC c, float2 uv : SV_DomainLocation, const OutputPatch<CP, 4> q) {\n"
     "  O4 o; o.pos = ds_pos(uv, q); o.a[0] = o.a[1] = o.a[2] = o.a[3] = q[0].p + sel; return o; }\n",
     e_marked},
    {"gs-out", "geometry shader writes o[r + 1] before each emit (output range o1..o4, assembled)", false, 1,
     {{"vs_quad"}, {}, {}, {"gs_out_skel", body_gs_out}, {"ps_sum"}},
     "[maxvertexcount(3)] void gs_out_skel(triangle V4 t[3], inout TriangleStream<O4> s) {\n"
     "  [unroll] for (int i = 0; i < 3; i++) { O4 o; o.pos = t[i].pos;\n"
     "    o.a[0] = o.a[1] = o.a[2] = o.a[3] = t[0].p + sel; s.Append(o); } }\n",
     e_marked},
    {"ps-out", "pixel shader writes SV_Target[r] (output range o0..o3 over four render targets, assembled)", false, 4,
     {{"vs_quad"}, {}, {}, {}, {"ps_out_skel", body_ps_out}},
     "struct T4 { float4 t[4] : SV_Target; };\n"
     "T4 ps_out_skel(V4 i) { T4 o; o.t[0] = o.t[1] = o.t[2] = o.t[3] = i.p + sel; return o; }\n",
     e_ps_out},
};

/* ---- Running ---------------------------------------------------------- */

typedef HRESULT(WINAPI *d3dcompile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                       ID3DInclude *, const char *, const char *, UINT, UINT,
                                       ID3DBlob **, ID3DBlob **);
typedef HRESULT(WINAPI *d3ddisasm_fn)(const void *, SIZE_T, UINT, const char *, ID3DBlob **);
static d3dcompile_fn p_compile;
static d3ddisasm_fn p_disasm;
static const wchar_t *g_asm_dir;
static bool g_asm_only;

static void show_disassembly(const Case &c, const char *target, const void *code, size_t size)
{
    ID3DBlob *text = NULL;
    HRESULT hr = p_disasm ? p_disasm(code, size, 0, NULL, &text) : E_FAIL;
    if (FAILED(hr)) {
        say("  %s: D3DDisassemble 0x%08lx", target, (unsigned long)hr);
        return;
    }
    const char *s = (const char *)text->GetBufferPointer();
    if (g_asm_dir) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\%hs-%hs.asm.txt", g_asm_dir, c.name, target);
        FILE *f = _wfopen(path, L"w");
        if (f) {
            fputs(s, f);
            fclose(f);
        }
    }
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char line[512];
        snprintf(line, sizeof(line), "%.*s", (int)(n < 500 ? n : 500), s);
        if (strstr(line, "indexrange") || strstr(line, "[r") || strstr(line, "+ r") || strstr(line, "_phase"))
            say("  %s: %s", target, line + strspn(line, " "));
        s += n + (e ? 1 : 0);
    }
    text->Release();
}

/* The bytecode for one stage: compiled, and for an assembled stage spliced. */
static bool stage_code(const Case &c, int stage, std::vector<uint8_t> &out)
{
    static char src[32768];
    snprintf(src, sizeof(src), "%s%s", common_hlsl, c.hlsl);
    ID3DBlob *code = NULL, *err = NULL;
    const char *target = stage_target[stage];
    HRESULT hr = p_compile(src, strlen(src), c.name, NULL, NULL, c.st[stage].entry, target, 0, 0, &code, &err);
    if (FAILED(hr))
        say("  D3DCompile %s %s: 0x%08lx %s", c.st[stage].entry, target, (unsigned long)hr,
            err ? (const char *)err->GetBufferPointer() : "");
    if (err)
        err->Release();
    if (!code)
        return false;
    const uint8_t *b = (const uint8_t *)code->GetBufferPointer();
    out.assign(b, b + code->GetBufferSize());
    code->Release();
    if (c.st[stage].asm_body) {
        if (!checksum_ok(out)) {
            say("  %s: the checksum of d3dcompiler's own bytecode does not verify", target);
            return false;
        }
        Toks shex = splice(find_code(out.data()), c.st[stage].phase, c.st[stage].asm_body());
        out = replace_code(out.data(), shex);
        say("  %s: %s code assembled by the probe (%u tokens)", target,
            c.st[stage].phase ? "hull shader phase" : "shader", (unsigned)shex.size());
    }
    show_disassembly(c, target, out.data(), out.size());
    return true;
}

struct Vertex {
    float pos[4];
    UINT p;
    float a[4][4];
};

/* Returns the number of mismatching values, or -1 when the case could not run. */
static int run_case(const Case &c, D3D_DRIVER_TYPE type)
{
    if (g_asm_only) {
        for (int s = 0; s < STAGES; s++) {
            std::vector<uint8_t> b;
            if (c.st[s].entry && !stage_code(c, s, b))
                return -1;
        }
        return 0;
    }
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *dc = NULL;
    HRESULT hr = D3D11CreateDevice(NULL, type, NULL, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &dc);
    if (FAILED(hr)) {
        say("  D3D11CreateDevice: 0x%08lx", (unsigned long)hr);
        return -1;
    }

    ID3D11VertexShader *vs = NULL;
    ID3D11HullShader *hs = NULL;
    ID3D11DomainShader *ds = NULL;
    ID3D11GeometryShader *gs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11InputLayout *layout = NULL;
    bool ok = true;
    for (int s = 0; s < STAGES && ok; s++) {
        if (!c.st[s].entry)
            continue;
        std::vector<uint8_t> b;
        if (!stage_code(c, s, b)) {
            ok = false;
            break;
        }
        const void *p = b.data();
        SIZE_T n = b.size();
        switch (s) {
        case VS:
            hr = dev->CreateVertexShader(p, n, NULL, &vs);
            if (SUCCEEDED(hr) && !c.tess) {
                static const D3D11_INPUT_ELEMENT_DESC el[] = {
                    {"POS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"QUAD", 0, DXGI_FORMAT_R32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"ATTR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"ATTR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"ATTR", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"ATTR", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 68, D3D11_INPUT_PER_VERTEX_DATA, 0},
                };
                HRESULT lhr = dev->CreateInputLayout(el, 6, p, n, &layout);
                say("  CreateInputLayout 0x%08lx", (unsigned long)lhr);
                ok = SUCCEEDED(lhr);
            }
            say("  CreateVertexShader 0x%08lx", (unsigned long)hr);
            break;
        case HS:
            hr = dev->CreateHullShader(p, n, NULL, &hs);
            say("  CreateHullShader 0x%08lx", (unsigned long)hr);
            break;
        case DS:
            hr = dev->CreateDomainShader(p, n, NULL, &ds);
            say("  CreateDomainShader 0x%08lx", (unsigned long)hr);
            break;
        case GS:
            hr = dev->CreateGeometryShader(p, n, NULL, &gs);
            say("  CreateGeometryShader 0x%08lx", (unsigned long)hr);
            break;
        case PS:
            hr = dev->CreatePixelShader(p, n, NULL, &ps);
            say("  CreatePixelShader 0x%08lx", (unsigned long)hr);
            break;
        }
        ok = ok && SUCCEEDED(hr);
    }
    hr = dev->GetDeviceRemovedReason();
    if (FAILED(hr)) {
        say("  device removed: 0x%08lx", (unsigned long)hr);
        ok = false;
    }
    if (!ok) {
        if (layout) layout->Release();
        if (vs) vs->Release();
        if (hs) hs->Release();
        if (ds) ds->Release();
        if (gs) gs->Release();
        if (ps) ps->Release();
        dc->Release();
        dev->Release();
        return -1;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = RT;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt[4] = {}, *staging = NULL;
    ID3D11RenderTargetView *rtv[4] = {};
    for (unsigned t = 0; t < c.targets; t++) {
        dev->CreateTexture2D(&td, NULL, &rt[t]);
        dev->CreateRenderTargetView(rt[t], NULL, &rtv[t]);
    }
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, NULL, &staging);

    UINT cb[4] = {};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(cb);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer *cbuf = NULL, *vbuf = NULL;
    dev->CreateBuffer(&bd, NULL, &cbuf);

    /* Untessellated cases: quadrant p as two triangles, ATTR k = val(p, 0, k). */
    Vertex verts[24];
    static const float corner[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    for (unsigned v = 0; v < 24; v++) {
        unsigned p = v / 6;
        Vertex &x = verts[v];
        x.pos[0] = (float)(p % 2) - 1 + corner[v % 6][0];
        x.pos[1] = (float)(p / 2) - 1 + corner[v % 6][1];
        x.pos[2] = 0.5f;
        x.pos[3] = 1;
        x.p = p;
        for (unsigned k = 0; k < 4; k++)
            val(p, 0, k, x.a[k]);
    }
    bd.ByteWidth = sizeof(verts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {verts, 0, 0};
    dev->CreateBuffer(&bd, &init, &vbuf);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState *rs = NULL;
    dev->CreateRasterizerState(&rd, &rs);

    int bad = 0;
    for (UINT sel = 0; sel < 4 && bad >= 0; sel++) {
        cb[0] = sel;
        dc->UpdateSubresource(cbuf, 0, NULL, cb, 0, 0);
        const float clear[4] = {-1, -1, -1, -1};
        for (unsigned t = 0; t < c.targets; t++)
            dc->ClearRenderTargetView(rtv[t], clear);
        dc->OMSetRenderTargets(c.targets, rtv, NULL);
        D3D11_VIEWPORT vp = {0, 0, RT, RT, 0, 1};
        dc->RSSetViewports(1, &vp);
        dc->RSSetState(rs);
        UINT stride = sizeof(Vertex), offset = 0;
        dc->IASetInputLayout(layout);
        ID3D11Buffer *vb = c.tess ? NULL : vbuf;
        dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        dc->IASetPrimitiveTopology(c.tess ? D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
                                          : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->VSSetShader(vs, NULL, 0);
        dc->HSSetShader(hs, NULL, 0);
        dc->DSSetShader(ds, NULL, 0);
        dc->GSSetShader(gs, NULL, 0);
        dc->PSSetShader(ps, NULL, 0);
        dc->VSSetConstantBuffers(0, 1, &cbuf);
        dc->HSSetConstantBuffers(0, 1, &cbuf);
        dc->DSSetConstantBuffers(0, 1, &cbuf);
        dc->GSSetConstantBuffers(0, 1, &cbuf);
        dc->PSSetConstantBuffers(0, 1, &cbuf);
        dc->Draw(c.tess ? 16 : 24, 0);
        for (unsigned t = 0; t < c.targets && bad >= 0; t++) {
            dc->CopyResource(staging, rt[t]);
            D3D11_MAPPED_SUBRESOURCE m;
            hr = dc->Map(staging, 0, D3D11_MAP_READ, 0, &m);
            if (FAILED(hr)) {
                say("  Map: 0x%08lx", (unsigned long)hr);
                bad = -1;
                break;
            }
            for (unsigned p = 0; p < 4; p++) {
                /* Quadrant p spans clip x (p % 2) - 1 .. (p % 2) and y (p / 2) - 1 .. (p / 2);
                 * clip y points up, rows go down. */
                unsigned px = (p % 2) * (RT / 2) + RT / 4;
                unsigned py = (1 - p / 2) * (RT / 2) + RT / 4;
                const float *v = (const float *)((const char *)m.pData + py * m.RowPitch) + px * 4;
                float e[4];
                c.expect(p, sel, t, e);
                bool match = true;
                for (int k = 0; k < 4; k++)
                    if (fabsf(v[k] - e[k]) > 1e-3f * (1 + fabsf(e[k])))
                        match = false;
                bad += !match;
                char rtname[16] = "";
                if (c.targets > 1)
                    snprintf(rtname, sizeof(rtname), " rt %u", t);
                say("  sel %u%s quad %u: got (%g, %g, %g, %g) want (%g, %g, %g, %g) %s", sel, rtname, p, v[0], v[1],
                    v[2], v[3], e[0], e[1], e[2], e[3], match ? "ok" : "MISMATCH");
            }
            dc->Unmap(staging, 0);
        }
    }
    hr = dev->GetDeviceRemovedReason();
    if (FAILED(hr)) {
        say("  device removed: 0x%08lx", (unsigned long)hr);
        bad = -1;
    }
    dc->ClearState();
    rs->Release();
    vbuf->Release();
    cbuf->Release();
    staging->Release();
    for (unsigned t = 0; t < c.targets; t++) {
        rtv[t]->Release();
        rt[t]->Release();
    }
    if (layout) layout->Release();
    if (vs) vs->Release();
    if (hs) hs->Release();
    if (ds) ds->Release();
    if (gs) gs->Release();
    if (ps) ps->Release();
    dc->Release();
    dev->Release();
    return bad;
}

int wmain(int argc, wchar_t **argv)
{
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
    const wchar_t *only = NULL;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--warp"))
            type = D3D_DRIVER_TYPE_WARP;
        else if (!wcscmp(argv[i], L"--case") && i + 1 < argc)
            only = argv[++i];
        else if (!wcscmp(argv[i], L"--asm") && i + 1 < argc)
            g_asm_dir = argv[++i];
        else if (!wcscmp(argv[i], L"--asm-only"))
            g_asm_only = true;
        else if (!wcscmp(argv[i], L"--log") && i + 1 < argc)
            g_log = _wfopen(argv[++i], L"w");
    }
    say("d3d11_index2: driver type %s, %u-bit", type == D3D_DRIVER_TYPE_WARP ? "WARP" : "HARDWARE",
        (unsigned)(sizeof(void *) * 8));

    HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    p_compile = m ? (d3dcompile_fn)(void *)GetProcAddress(m, "D3DCompile") : NULL;
    p_disasm = m ? (d3ddisasm_fn)(void *)GetProcAddress(m, "D3DDisassemble") : NULL;
    if (!p_compile) {
        say("cannot load D3DCompile from d3dcompiler_47.dll");
        return 1;
    }

    int failed = 0, ran = 0;
    for (const Case &c : cases) {
        wchar_t wname[64];
        swprintf(wname, 64, L"%hs", c.name);
        if (only && wcscmp(only, wname))
            continue;
        say("case %s: %s", c.name, c.what);
        int bad = run_case(c, type);
        ran++;
        if (bad)
            failed++;
        say("RESULT %s %s", c.name, bad < 0 ? "FAIL (could not run)" : bad ? "FAIL (mismatch)" : "PASS");
    }
    say("%d of %d cases pass", ran - failed, ran);
    if (g_log)
        fclose(g_log);
    return failed ? 1 : 0;
}
