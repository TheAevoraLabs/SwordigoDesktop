// boulder_collision_fields_test — pins the CollisionShapeComponent wire format
// that boulder writes against what the real engine actually reads.
//
// WHY THIS EXISTS
// ---------------
// A ground object's collision is written as protobuf fields inside the
// CollisionShapeComponent extension (121).  Nothing about the in-editor preview
// would notice if those numbers moved: Ruby draws ground meshes from the parsed
// vertex data, so a shape written at the wrong field numbers still renders and
// still looks fine — it just means something different to the game.  The only
// way to catch that is to assert the numbers themselves.
//
// The numbers pinned here come from libswordigo 1.4.13 (arm64) and are
// corroborated three ways:
//
//   1. Caver::Proto::protobuf_AddDesc_Scene_2eproto (arm64 0x41A8DC) assigns the
//      component extensions: ShapeComponent = 120, CollisionShapeComponent = 121.
//   2. CollisionShapeComponent::MergePartialFromCodedStream (arm64 0x3EB32C)
//      dispatches on the field number and enforces the wire type, giving both:
//          field  2, 3, 4, 5 -> varint          (IsGround, Collides,
//                                                ReceivesDamage, InflictsDamage)
//          field  6, 7       -> 32-bit float    (MinDepth, MaxDepth)
//          field  8          -> varint          (SpecialType)
//          field  9, 10, 12  -> length-delim    (OnCollide/OnCollisionEnd/
//                                                OnReceiveDamage Programs)
//          field 11          -> varint          (Enabled)
//          field 13          -> 32-bit float    (Friction)
//          field 14          -> varint          (UnsafeGround)
//   3. src/tools/filerift.cpp declares the same numbers with those names, so a
//      divergence between the two would be caught here too.
//
// The second half of the test asserts a shape-level fact rather than a field
// number: across the 202 decoded shipped scenes, plain terrain writes
// IsGround + MinDepth + MaxDepth + Enabled and LEAVES THE BREAKABLE TRIO OUT
// (5,345 of 6,588 CollisionShapeComponent messages; all 8 ground shapes that do
// set SpecialType are breakable props — box, keybox, boulder, sign, questvase).
// Generating terrain that claims to be breakable and damageable is a behaviour
// change, not a detail, so it must stay opt-in.

#include "tools/boulder.h"
#include "platform/protobuf_reader.h"

#include <cstdio>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// Defined by the app's main.cpp; libswcore references it, so a standalone
// binary has to provide it (same as the other scene tests).
std::string g_instance_assets_dir = "assets";

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__);                \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace {

// One decoded field of a CollisionShapeComponent payload.
struct DecodedField {
    proto::WireType wire = proto::WIRE_VARINT;
    uint64_t varint = 0;
    float flt = 0.0f;
};

using Payload = std::map<uint32_t, DecodedField>;

// Scene{ Object{ Component{ ... CollisionShapeComponent } } } -> the inner
// CollisionShapeComponent payload, decoded field by field.
bool extract_collision_payload(const std::string& scene_bytes, Payload& out) {
    proto::Reader scene(scene_bytes);
    proto::Field f;
    while (scene.read_field(f)) {
        if (f.field_number != 1 || f.wire_type != proto::WIRE_LEN) continue;  // Object
        proto::Reader obj(f.bytes_val);
        proto::Field o;
        while (obj.read_field(o)) {
            if (o.field_number != 3 || o.wire_type != proto::WIRE_LEN) continue;  // Component
            proto::Reader comp(o.bytes_val);
            proto::Field c;
            bool is_collision = false;
            Payload payload;
            while (comp.read_field(c)) {
                if (c.field_number == 121 && c.wire_type == proto::WIRE_LEN) {
                    is_collision = true;
                    proto::Reader csc(c.bytes_val);
                    proto::Field sf;
                    while (csc.read_field(sf)) {
                        DecodedField d;
                        d.wire = sf.wire_type;
                        d.varint = sf.varint_val;
                        d.flt = sf.float_val;
                        payload[sf.field_number] = d;
                    }
                }
            }
            if (is_collision) { out = payload; return true; }
        }
    }
    return false;
}

const char* wire_name(proto::WireType w) {
    switch (w) {
        case proto::WIRE_VARINT: return "varint";
        case proto::WIRE_I32:    return "float32";
        case proto::WIRE_LEN:    return "length-delimited";
        default:                 return "other";
    }
}

// Assert one field is present with the exact wire type the engine's parser
// demands for that number.
void expect_wire(const Payload& p, uint32_t num, proto::WireType want, const char* name) {
    auto it = p.find(num);
    if (it == p.end()) {
        std::printf("FAIL: field %u (%s) missing — the engine reads %s at %u\n",
                    num, name, wire_name(want), num);
        ++g_failures;
        return;
    }
    if (it->second.wire != want) {
        std::printf("FAIL: field %u (%s) is %s, engine requires %s\n",
                    num, name, wire_name(it->second.wire), wire_name(want));
        ++g_failures;
    }
}

const DecodedField* find(const Payload& p, uint32_t num) {
    auto it = p.find(num);
    return it == p.end() ? nullptr : &it->second;
}

std::string make_swdm(const boulder::GroundMesh& gm) {
    return boulder::serialize_swdm(gm);
}

boulder::GroundMesh base_mesh() {
    boulder::GroundMesh gm;
    gm.polygon = {{0.0, 0.0}, {800.0, 0.0}, {800.0, 400.0}, {0.0, 400.0}};
    gm.min_depth = -45.0;
    gm.max_depth = 45.0;
    gm.z = 40.0;
    return gm;
}

}  // namespace

int main() {
    // ── 1. Default terrain shape ──────────────────────────────────────────
    // IsGround(2) + MinDepth(6, float) + MaxDepth(7, float) + Enabled(11),
    // exactly the shipped terrain pattern.
    {
        const boulder::GroundMesh gm = base_mesh();
        const std::string bin =
            boulder::generate_ground_mesh_object(make_swdm(gm), "pin_terrain", gm.z);
        CHECK(!bin.empty(), "boulder generated a terrain object");

        Payload p;
        if (extract_collision_payload(bin, p)) {
            std::printf("terrain CollisionShape fields:");
            for (const auto& kv : p) std::printf(" %u(%s)", kv.first, wire_name(kv.second.wire));
            std::printf("\n");

            expect_wire(p, 2,  proto::WIRE_VARINT, "IsGround");
            expect_wire(p, 6,  proto::WIRE_I32,    "MinDepth");
            expect_wire(p, 7,  proto::WIRE_I32,    "MaxDepth");
            expect_wire(p, 11, proto::WIRE_VARINT, "Enabled");

            const DecodedField* is_ground = find(p, 2);
            CHECK(is_ground && is_ground->varint == 1, "IsGround must be 1");

            const DecodedField* enabled = find(p, 11);
            CHECK(enabled && enabled->varint == 1, "Enabled must be 1");

            const DecodedField* dmin = find(p, 6);
            const DecodedField* dmax = find(p, 7);
            CHECK(dmin && std::fabs(dmin->flt - (-45.0f)) < 1e-4f,
                  "MinDepth lands at 6 with the mesh's min_depth");
            CHECK(dmax && std::fabs(dmax->flt - 45.0f) < 1e-4f,
                  "MaxDepth lands at 7 with the mesh's max_depth");

            // The breakable trio must NOT be present on terrain.  If this
            // starts failing, generated ground silently became destructible.
            CHECK(p.find(3) == p.end(), "terrain must not write Collides (3)");
            CHECK(p.find(4) == p.end(), "terrain must not write ReceivesDamage (4)");
            CHECK(p.find(5) == p.end(), "terrain must not write InflictsDamage (5)");
            CHECK(p.find(8) == p.end(), "terrain must not write SpecialType (8)");
        } else {
            std::printf("FAIL: no CollisionShapeComponent (extension 121) in the generated object\n");
            ++g_failures;
        }
    }

    // ── 2. Breakable prop shape (opt-in) ──────────────────────────────────
    // The trio the shipped props carry, all together, at 3 / 4 / 8 — plus the
    // depths at 6 / 7, which is what proves the two groups don't overlap.
    {
        boulder::GroundMesh gm = base_mesh();
        gm.collides         = true;
        gm.receives_damage  = true;
        gm.has_special_type = true;
        gm.special_type     = 7;      // breakable box (box, box_big, boulder, sign)
        const std::string bin =
            boulder::generate_ground_mesh_object(make_swdm(gm), "pin_breakable", gm.z);
        CHECK(!bin.empty(), "boulder generated a breakable object");

        Payload p;
        if (extract_collision_payload(bin, p)) {
            std::printf("breakable CollisionShape fields:");
            for (const auto& kv : p) std::printf(" %u(%s)", kv.first, wire_name(kv.second.wire));
            std::printf("\n");

            expect_wire(p, 3, proto::WIRE_VARINT, "Collides");
            expect_wire(p, 4, proto::WIRE_VARINT, "ReceivesDamage");
            expect_wire(p, 8, proto::WIRE_VARINT, "SpecialType");
            // Depths keep their own numbers even alongside the trio.
            expect_wire(p, 6, proto::WIRE_I32, "MinDepth");
            expect_wire(p, 7, proto::WIRE_I32, "MaxDepth");

            const DecodedField* coll   = find(p, 3);
            const DecodedField* dmg    = find(p, 4);
            const DecodedField* spec   = find(p, 8);
            CHECK(coll && coll->varint == 1, "Collides must be 1 when opted in");
            CHECK(dmg  && dmg->varint  == 1, "ReceivesDamage must be 1 when opted in");
            CHECK(spec && spec->varint == 7, "SpecialType must carry the requested enum value");
        } else {
            std::printf("FAIL: no CollisionShapeComponent in the breakable object\n");
            ++g_failures;
        }
    }

    // ── 3. SpecialType 0 must still be writable ───────────────────────────
    // The enum's zero value is meaningful, so "has_special_type" — not the
    // value — has to decide whether the field is emitted.
    {
        boulder::GroundMesh gm = base_mesh();
        gm.has_special_type = true;
        gm.special_type     = 0;
        const std::string bin =
            boulder::generate_ground_mesh_object(make_swdm(gm), "pin_zero_enum", gm.z);
        Payload p;
        if (!bin.empty() && extract_collision_payload(bin, p)) {
            const DecodedField* spec = find(p, 8);
            CHECK(spec && spec->varint == 0,
                  "explicit SpecialType 0 must be written, not dropped");
        } else {
            std::printf("FAIL: could not decode the zero-enum object\n");
            ++g_failures;
        }
    }

    // ── 4. Enabled 0 must be written, not treated as absent ───────────────
    // 35 shipped shapes carry Enabled : 0; boulder's default is 1.
    {
        boulder::GroundMesh gm = base_mesh();
        gm.enabled = false;
        const std::string bin =
            boulder::generate_ground_mesh_object(make_swdm(gm), "pin_disabled", gm.z);
        Payload p;
        if (!bin.empty() && extract_collision_payload(bin, p)) {
            const DecodedField* en = find(p, 11);
            CHECK(en && en->varint == 0, "Enabled 0 must be emitted explicitly");
        } else {
            std::printf("FAIL: could not decode the disabled object\n");
            ++g_failures;
        }
    }

    // ── 5. Cross-check against a real shipped scene ───────────────────────
    // Optional data file: skip rather than fail, like the other scene tests.
    // Decoding a vanilla ground shape with the SAME field numbers boulder emits
    // is what ties this test to the engine instead of to our own reading.
    {
        static const char* kCandidates[] = {
            "throwback/assets/resources/forest_part1.scene",
            "../throwback/assets/resources/forest_part1.scene",
        };
        std::string path;
        for (const char* c : kCandidates) {
            FILE* fp = std::fopen(c, "rb");
            if (fp) { std::fclose(fp); path = c; break; }
        }
        if (path.empty()) {
            std::printf("SKIP: vanilla scene not present, cross-check not run\n");
        } else {
            std::string bytes;
            FILE* fp = std::fopen(path.c_str(), "rb");
            if (fp) {
                char buf[65536];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) bytes.append(buf, n);
                std::fclose(fp);
            }
            // A vanilla scene holds many objects; walk them all and require that
            // every ground collision shape it does contain uses the numbers and
            // wire types we emit (min one shape, or the check is vacuous).
            int seen = 0;
            proto::Reader scene(bytes);
            proto::Field f;
            while (scene.read_field(f)) {
                if (f.field_number != 1 || f.wire_type != proto::WIRE_LEN) continue;
                proto::Reader obj(f.bytes_val);
                proto::Field o;
                Payload p;
                bool is_collision = false;
                while (obj.read_field(o)) {
                    if (o.field_number != 3 || o.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader comp(o.bytes_val);
                    proto::Field c;
                    while (comp.read_field(c)) {
                        if (c.field_number != 121 || c.wire_type != proto::WIRE_LEN) continue;
                        is_collision = true;
                        proto::Reader csc(c.bytes_val);
                        proto::Field sf;
                        while (csc.read_field(sf)) {
                            DecodedField d;
                            d.wire = sf.wire_type;
                            d.varint = sf.varint_val;
                            d.flt = sf.float_val;
                            p[sf.field_number] = d;
                        }
                    }
                }
                if (!is_collision) continue;
                ++seen;
                // 6 and 7 are the depths; when present they must be floats.
                if (auto* d = find(p, 6)) {
                    if (d->wire != proto::WIRE_I32) {
                        std::printf("FAIL: vanilla MinDepth(6) is not a float — schema mismatch\n");
                        ++g_failures;
                    }
                }
                if (auto* d = find(p, 7)) {
                    if (d->wire != proto::WIRE_I32) {
                        std::printf("FAIL: vanilla MaxDepth(7) is not a float — schema mismatch\n");
                        ++g_failures;
                    }
                }
                if (auto* d = find(p, 2)) {
                    if (d->wire != proto::WIRE_VARINT) {
                        std::printf("FAIL: vanilla IsGround(2) is not a varint\n");
                        ++g_failures;
                    }
                }
            }
            CHECK(seen > 0, "vanilla scene contained CollisionShapeComponents to compare");
            std::printf("cross-checked %d vanilla collision shape(s) in %s\n", seen, path.c_str());
        }
    }

    std::fflush(stdout);
    if (g_failures == 0) {
        std::printf("PASS: CollisionShapeComponent wire format pinned to the decompiled engine\n");
        return 0;
    }
    std::printf("FAIL: %d collision-field assertion(s) failed\n", g_failures);
    return 1;
}
