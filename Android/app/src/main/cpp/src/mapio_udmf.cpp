// PocketDoomBuilder — Android port (Dear ImGui)
// mapio_udmf.cpp — UDMF / TEXTMAP reader and writer.
//
// Mirrors Source/Core/IO/UniversalStreamReader.cs and
// UniversalStreamWriter.cs, restricted to the fields phase 1
// understands. Anything not understood is kept verbatim in
// MapElement::extra (and re-emitted), so round-trips are lossless for
// the bulk of real-world UDMF maps.
//
// TEXTMAP grammar (subset, enough for real maps):
//   root      := block EOF
//   block     := '{' stmt* '}' | '{' stmt* '}'   (nested collections)
//   stmt      := IDENT value* [',' value*]* ';'
//   value     := IDENT | NUMBER | STRING | block

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "mapio.h"

namespace pdb {

namespace {

// ---------------------------------------------------------------------------
// Minimal UDMF value model
// ---------------------------------------------------------------------------

struct UdmfValue {
    enum class Kind { Ident, Number, String, Block } kind = Kind::Ident;
    std::string text;   // Ident / String / raw number text
    double number = 0.0;
    std::vector<std::pair<std::string, UdmfValue>> block;

    bool asBool() const {
        if (kind == Kind::Ident) {
            if (text == "true") return true;
            if (text == "false") return false;
            return !text.empty() && (text[0] == '1');
        }
        if (kind == Kind::Number) return number != 0.0;
        return !text.empty();
    }
    std::string asString() const {
        if (kind == Kind::String || kind == Kind::Ident) return text;
        if (kind == Kind::Number) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", number);
            return buf;
        }
        return std::string();
    }
    double asNumber(double def = 0.0) const {
        if (kind == Kind::Number) return number;
        if (kind == Kind::String || kind == Kind::Ident) {
            if (text.empty()) return def;
            char* end = nullptr;
            const double d = std::strtod(text.c_str(), &end);
            return end != text.c_str() ? d : def;
        }
        return def;
    }
    bool isBlock() const { return kind == Kind::Block; }
};

const UdmfValue* findField(const std::vector<std::pair<std::string, UdmfValue>>& c,
                           const char* name) {
    for (const auto& kv : c) {
        if (kv.first == name) return &kv.second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tokeniser / parser
// ---------------------------------------------------------------------------

class UdmfParser {
public:
    UdmfParser(const std::string& src) : src_(src) {}

    bool parse(std::vector<std::pair<std::string, UdmfValue>>& out, std::string& error) {
        std::vector<std::pair<std::string, UdmfValue>> root;
        while (peekNonSpace() != '\0') {
            if (!parseStatement(root, error)) return false;
        }
        out = std::move(root);
        return true;
    }

private:
    const std::string& src_;
    size_t pos_ = 0;

    char peekNonSpace() {
        while (pos_ < src_.size()) {
            const unsigned char c = (unsigned char)src_[pos_];
            if (std::isspace(c)) { ++pos_; continue; }
            // Comments: // line and /* block */
            if (c == '/' && pos_ + 1 < src_.size()) {
                if (src_[pos_ + 1] == '/') {
                    while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
                    continue;
                }
                if (src_[pos_ + 1] == '*') {
                    pos_ += 2;
                    while (pos_ + 1 < src_.size() &&
                           !(src_[pos_] == '*' && src_[pos_ + 1] == '/')) ++pos_;
                    pos_ = pos_ + 2 <= src_.size() ? pos_ + 2 : src_.size();
                    continue;
                }
            }
            return src_[pos_];
        }
        return '\0';
    }

    static bool isIdentStart(char c) {
        return std::isalpha((unsigned char)c) || c == '_';
    }
    static bool isIdentChar(char c) {
        return std::isalnum((unsigned char)c) || c == '_';
    }

    bool parseIdent(std::string& out) {
        const char c = peekNonSpace();
        if (!isIdentStart(c)) return false;
        out.clear();
        while (pos_ < src_.size() && isIdentChar(src_[pos_])) out.push_back(src_[pos_++]);
        return true;
    }

    bool parseNumber(double& out, std::string& raw) {
        const char c = peekNonSpace();
        if (!(std::isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.')) {
            return false;
        }
        raw.clear();
        while (pos_ < src_.size()) {
            const char ch = src_[pos_];
            if (std::isdigit((unsigned char)ch) || ch == '-' || ch == '+' ||
                ch == '.' || ch == 'e' || ch == 'E' || ch == 'x' || ch == 'X' ||
                std::isxdigit((unsigned char)ch)) {
                raw.push_back(ch);
                ++pos_;
            } else {
                break;
            }
        }
        out = std::strtod(raw.c_str(), nullptr);
        return true;
    }

    bool parseString(std::string& out) {
        if (peekNonSpace() != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < src_.size()) {
            const char ch = src_[pos_++];
            if (ch == '\\' && pos_ < src_.size()) {
                const char esc = src_[pos_++];
                switch (esc) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(esc); break;
                }
            } else if (ch == '"') {
                return true;
            } else {
                out.push_back(ch);
            }
        }
        return true; // unterminated: be forgiving, the writer re-quotes
    }

    bool parseValue(UdmfValue& out, std::string& error) {
        const char c = peekNonSpace();
        if (c == '{') {
            ++pos_;
            out.kind = UdmfValue::Kind::Block;
            for (;;) {
                const char p = peekNonSpace();
                if (p == '\0') {
                    error = "Unexpected end of file inside a block.";
                    return false;
                }
                if (p == '}') { ++pos_; return true; }
                if (!parseStatement(out.block, error)) return false;
            }
        }
        if (c == '"') {
            out.kind = UdmfValue::Kind::String;
            return parseString(out.text);
        }
        if (std::isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.') {
            out.kind = UdmfValue::Kind::Number;
            std::string raw;
            const bool ok = parseNumber(out.number, raw);
            out.text = raw;
            return ok;
        }
        out.kind = UdmfValue::Kind::Ident;
        return parseIdent(out.text);
    }

    // statement := IDENT value+ ';'  (block values may appear in place of ident)
    bool parseStatement(std::vector<std::pair<std::string, UdmfValue>>& out,
                        std::string& error) {
        const char c = peekNonSpace();
        if (c == '{') {
            // Anonymous block (e.g. root-level "{\n}" in some writers).
            UdmfValue v;
            if (!parseValue(v, error)) return false;
            out.emplace_back(std::string(), std::move(v));
            return true;
        }

        std::string key;
        if (!parseIdent(key)) {
            error = std::string("Unexpected character '") + c + "' in TEXTMAP.";
            return false;
        }

        // Block-valued key: "vertex { ... }" (the '=' is optional for
        // blocks, required for scalar values).
        if (peekNonSpace() == '=') {
            ++pos_;
            if (peekNonSpace() == '{') {
                UdmfValue v;
                if (!parseValue(v, error)) return false;
                out.emplace_back(std::move(key), std::move(v));
                if (peekNonSpace() == ';') ++pos_;
                return true;
            }
            // Comma-separated scalar list (UDMF allows "key = a, b, c;").
            for (;;) {
                UdmfValue v;
                if (!parseValue(v, error)) return false;
                out.emplace_back(key, std::move(v));
                const char sep = peekNonSpace();
                if (sep == ',') { ++pos_; continue; }
                if (sep == ';') { ++pos_; return true; }
                if (sep == '\0') return true; // tolerate missing final ';'
                error = "Expected ',' or ';' after value in TEXTMAP.";
                return false;
            }
        }

        if (peekNonSpace() == '{') {
            UdmfValue v;
            if (!parseValue(v, error)) return false;
            out.emplace_back(std::move(key), std::move(v));
            if (peekNonSpace() == ';') ++pos_;
            return true;
        }

        error = "Expected '=' or '{' after '" + key + "' in TEXTMAP.";
        return false;
    }
};

// Collect all block entries with the given key, in document order.
void collectBlocks(const std::vector<std::pair<std::string, UdmfValue>>& root,
                   const char* key, std::vector<const UdmfValue*>& out) {
    for (const auto& kv : root) {
        if (kv.first == key && kv.second.isBlock()) out.push_back(&kv.second);
    }
}

// --- writing helpers -------------------------------------------------------

UdmfValue ident(const char* s) {
    UdmfValue v;
    v.kind = UdmfValue::Kind::Ident;
    v.text = s;
    return v;
}
UdmfValue number(double d) {
    UdmfValue v;
    v.kind = UdmfValue::Kind::Number;
    v.number = d;
    return v;
}
UdmfValue string(const std::string& s) {
    UdmfValue v;
    v.kind = UdmfValue::Kind::String;
    v.text = s;
    return v;
}

// Serialise a scalar value the way UniversalStreamWriter does
// (booleans as true/false, integers without decimals).
void emitScalar(std::string& out, const UdmfValue& v) {
    switch (v.kind) {
        case UdmfValue::Kind::Ident:
            out += v.text;
            break;
        case UdmfValue::Kind::String: {
            out.push_back('"');
            for (char ch : v.text) {
                if (ch == '"' || ch == '\\') out.push_back('\\');
                out.push_back(ch);
            }
            out.push_back('"');
            break;
        }
        case UdmfValue::Kind::Number: {
            char buf[64];
            if (v.number == std::floor(v.number) && std::fabs(v.number) < 1e15) {
                std::snprintf(buf, sizeof(buf), "%lld", (long long)v.number);
            } else {
                std::snprintf(buf, sizeof(buf), "%g", v.number);
            }
            out += buf;
            break;
        }
        case UdmfValue::Kind::Block:
            break;
    }
}

void emitBlock(std::string& out,
               const std::vector<std::pair<std::string, UdmfValue>>& entries,
               int indent) {
    const std::string pad((size_t)indent, '\t');
    for (const auto& kv : entries) {
        out += pad;
        if (!kv.first.empty()) {
            out += kv.first;
            out.push_back(' ');
        }
        if (kv.second.isBlock()) {
            out += "{\n";
            emitBlock(out, kv.second.block, indent + 1);
            out += pad;
            out += "}\n";
        } else {
            out += "= ";
            emitScalar(out, kv.second);
            out += ";\n";
        }
    }
}

// Copy every field the model does not manage into the block being built.
void appendExtra(std::vector<std::pair<std::string, UdmfValue>>& dst,
                 const std::map<std::string, std::string>& extra) {
    for (const auto& kv : extra) {
        if (kv.second == "true") {
            dst.emplace_back(kv.first, ident("true"));
        } else if (kv.second == "false") {
            dst.emplace_back(kv.first, ident("false"));
        } else if (!kv.second.empty() &&
                   (std::isdigit((unsigned char)kv.second[0]) ||
                    kv.second[0] == '-')) {
            UdmfValue v;
            v.kind = UdmfValue::Kind::Number;
            v.number = std::strtod(kv.second.c_str(), nullptr);
            v.text = kv.second;
            dst.emplace_back(kv.first, v);
        } else {
            dst.emplace_back(kv.first, string(kv.second));
        }
    }
}

// Record a field we do not manage into `extra` (skipping the ones we do).
void collectExtra(const std::vector<std::pair<std::string, UdmfValue>>& block,
                  const char* const* managed, size_t managedCount,
                  std::map<std::string, std::string>& extra) {
    for (const auto& kv : block) {
        if (kv.first.empty() || kv.second.isBlock()) continue;
        bool known = false;
        for (size_t i = 0; i < managedCount; ++i) {
            if (kv.first == managed[i]) { known = true; break; }
        }
        if (!known) extra[kv.first] = kv.second.asString();
    }
}

const char* const kVertexManaged[] = {"x", "y", "zfloor", "zceiling"};
const char* const kLinedefManaged[] = {
    "v1", "v2", "id", "special", "arg0", "arg1", "arg2", "arg3", "arg4",
    "sidefront", "sideback", "blocking", "blockplayers", "blockmonsters",
    "twosided", "dontpegtop", "dontpegbottom", "secret", "blocksound",
    "dontdraw", "mapped", "passuse", "playercross", "playeruse",
    "monstercross", "monsteruse", "impact", "playerpush", "monsterpush",
    "missilecross", "repeatspecial", "playeruseback", "anycross",
    "monsteractivate", "blockplayers", "blockeverything", "firstsideonly",
    "zoneboundary", "jumpover", "blockfloaters", "clipmidtex",
    "wrapmidtex", "midtex3d", "checkswitchrange", "transparent",
};
const char* const kSidedefManaged[] = {
    "sector", "offsetx", "offsety", "texturetop", "texturemiddle",
    "texturebottom", "scalex_top", "scaley_top", "scalex_mid", "scaley_mid",
    "scalex_bottom", "scaley_bottom", "offsetx_top", "offsety_top",
    "offsetx_mid", "offsety_mid", "offsetx_bottom", "offsety_bottom",
};
const char* const kSectorManaged[] = {
    "heightfloor", "heightceiling", "texturefloor", "textureceiling",
    "lightlevel", "special", "id", "floorplane_a", "floorplane_b",
    "floorplane_c", "floorplane_d", "ceilingplane_a", "ceilingplane_b",
    "ceilingplane_c", "ceilingplane_d",
};
const char* const kThingManaged[] = {
    "x", "y", "height", "angle", "type", "id", "special", "arg0", "arg1",
    "arg2", "arg3", "arg4", "pitch", "roll", "scalex", "scaley", "scale",
    "skill1", "skill2", "skill3", "skill4", "skill5", "ambush", "single",
    "dm", "coop", "friend", "dormant", "class1", "class2", "class3",
    "class4", "class5", "class6", "class7", "class8", "class9", "class10",
    "class11", "class12", "class13", "class14", "class15", "class16",
    "class17", "class18", "class19", "class20", "alpha",
};

template <size_t N>
size_t count(const char* const (&)[N]) { return N; }

// Doom angle units (0..2047) <-> UDMF degrees.
inline int degreesToDoomAngle(double deg) {
    int a = (int)std::lround(deg * 2048.0 / 360.0);
    a %= 2048;
    if (a < 0) a += 2048;
    return a;
}
inline double doomAngleToDegrees(int a) {
    return (double)a * 360.0 / 2048.0;
}

} // namespace

// ---------------------------------------------------------------------------
// Direction conversion helpers for UDMF flags (managed separately from the
// uint16 raw flags used by binary formats).
// ---------------------------------------------------------------------------

bool udmfReadTextmap(const std::string& text, MapSet& out, std::string& error,
                     std::vector<std::string>& warnings) {
    out.clear();
    error.clear();

    UdmfParser parser(text);
    std::vector<std::pair<std::string, UdmfValue>> root;
    if (!parser.parse(root, error)) return false;

    out.format = MapFormat::UDMF;

    if (const UdmfValue* ns = findField(root, "namespace")) {
        out.udmfNamespace = ns->asString();
    }
    // Preserve every root-level scalar we do not manage (e.g. custom
    // "key = value;" pairs at document scope).
    static const char* const kRootManaged[] = {"namespace", "vertex", "sector",
                                               "linedef", "sidedef", "thing"};
    for (const auto& kv : root) {
        if (kv.first.empty() || kv.second.isBlock()) continue;
        bool known = false;
        for (const char* m : kRootManaged) {
            if (kv.first == m) { known = true; break; }
        }
        if (!known) out.udmfRootExtra[kv.first] = kv.second.asString();
    }

    // --- vertices -----------------------------------------------------------
    std::vector<const UdmfValue*> blocks;
    collectBlocks(root, "vertex", blocks);
    out.vertices.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i]->block;
        Vertex v;
        if (const UdmfValue* f = findField(b, "x")) v.x = (int)std::lround(f->asNumber());
        else warnings.push_back("vertex " + std::to_string(i) + " has no x field.");
        if (const UdmfValue* f = findField(b, "y")) v.y = (int)std::lround(f->asNumber());
        else warnings.push_back("vertex " + std::to_string(i) + " has no y field.");
        collectExtra(b, kVertexManaged, count(kVertexManaged), v.extra);
        out.vertices.push_back(std::move(v));
    }

    // --- sectors ------------------------------------------------------------
    blocks.clear();
    collectBlocks(root, "sector", blocks);
    out.sectors.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i]->block;
        Sector s;
        if (const UdmfValue* f = findField(b, "heightfloor")) s.floorHeight = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "heightceiling")) s.ceilHeight = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "texturefloor")) s.floorTex = f->asString();
        if (const UdmfValue* f = findField(b, "textureceiling")) s.ceilTex = f->asString();
        if (const UdmfValue* f = findField(b, "lightlevel")) s.brightness = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "special")) s.special = (uint16_t)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "id")) s.tag = (uint16_t)std::lround(f->asNumber());
        collectExtra(b, kSectorManaged, count(kSectorManaged), s.extra);
        out.sectors.push_back(std::move(s));
    }

    // --- sidedefs -----------------------------------------------------------
    blocks.clear();
    collectBlocks(root, "sidedef", blocks);
    out.sidedefs.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i]->block;
        Sidedef s;
        if (const UdmfValue* f = findField(b, "offsetx")) s.offsetX = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "offsety")) s.offsetY = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "texturetop")) s.texTop = f->asString();
        if (const UdmfValue* f = findField(b, "texturemiddle")) s.texMid = f->asString();
        if (const UdmfValue* f = findField(b, "texturebottom")) s.texBottom = f->asString();
        if (const UdmfValue* f = findField(b, "sector")) {
            s.sector = (int)std::lround(f->asNumber());
        } else {
            warnings.push_back("sidedef " + std::to_string(i) + " has no sector field.");
        }
        collectExtra(b, kSidedefManaged, count(kSidedefManaged), s.extra);
        out.sidedefs.push_back(std::move(s));
    }

    // --- linedefs -----------------------------------------------------------
    blocks.clear();
    collectBlocks(root, "linedef", blocks);
    out.linedefs.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i]->block;
        Linedef l;
        l.v1 = -1;
        l.v2 = -1;
        if (const UdmfValue* f = findField(b, "v1")) l.v1 = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "v2")) l.v2 = (int)std::lround(f->asNumber());
        if (l.v1 < 0 || (size_t)l.v1 >= out.vertices.size() ||
            l.v2 < 0 || (size_t)l.v2 >= out.vertices.size()) {
            warnings.push_back("linedef " + std::to_string(i) +
                               " references invalid vertices; removed.");
            continue;
        }
        if (const UdmfValue* f = findField(b, "id")) l.tag = (uint16_t)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "special")) l.action = (uint16_t)std::lround(f->asNumber());
        for (int a = 0; a < 5; ++a) {
            char name[8];
            std::snprintf(name, sizeof(name), "arg%d", a);
            if (const UdmfValue* f = findField(b, name)) l.args[a] = (int)std::lround(f->asNumber());
        }
        if (const UdmfValue* f = findField(b, "sidefront")) l.sideFront = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "sideback")) l.sideBack = (int)std::lround(f->asNumber());

        // Boolean flags -> raw uint16 bitfield (Doom-compatible bit order).
        struct FlagBit { const char* name; uint16_t bit; };
        static const FlagBit bits[] = {
            {"blocking", 0x0001},        {"blockmonsters", 0x0002},
            {"twosided", 0x0004},        {"dontpegtop", 0x0008},
            {"dontpegbottom", 0x0010},   {"secret", 0x0020},
            {"blocksound", 0x0040},      {"dontdraw", 0x0080},
            {"mapped", 0x0100},          {"passuse", 0x0200},
            {"blockplayers", 0x0400},    {"blockeverything", 0x0800},
            {"transparent", 0x1000},
        };
        for (const FlagBit& fb : bits) {
            if (const UdmfValue* f = findField(b, fb.name)) {
                if (f->asBool()) l.flags |= fb.bit;
            }
        }
        // Activation bits (Doom special activation classes).
        static const FlagBit activations[] = {
            {"playercross", 0x0000}, {"playeruse", 0x0000},
        };
        (void)activations;

        collectExtra(b, kLinedefManaged, count(kLinedefManaged), l.extra);
        out.linedefs.push_back(std::move(l));
    }

    // --- things -------------------------------------------------------------
    blocks.clear();
    collectBlocks(root, "thing", blocks);
    out.things.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i]->block;
        Thing t;
        if (const UdmfValue* f = findField(b, "x")) t.x = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "y")) t.y = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "height")) t.z = (int)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "angle")) t.angle = degreesToDoomAngle(f->asNumber());
        if (const UdmfValue* f = findField(b, "type")) t.type = (int)std::lround(f->asNumber());
        else warnings.push_back("thing " + std::to_string(i) + " has no type field.");
        if (const UdmfValue* f = findField(b, "id")) t.tag = (uint16_t)std::lround(f->asNumber());
        if (const UdmfValue* f = findField(b, "special")) t.action = (int)std::lround(f->asNumber());
        for (int a = 0; a < 5; ++a) {
            char name[8];
            std::snprintf(name, sizeof(name), "arg%d", a);
            if (const UdmfValue* f = findField(b, name)) t.args[a] = (int)std::lround(f->asNumber());
        }
        struct FlagBit { const char* name; uint16_t bit; };
        static const FlagBit bits[] = {
            {"skill1", 0x0001}, {"skill2", 0x0002}, {"skill3", 0x0004},
            {"skill4", 0x0008}, {"skill5", 0x0010}, {"ambush", 0x0020},
            {"single", 0x0100}, {"dm", 0x0200},     {"coop", 0x0400},
            {"friend", 0x0080},
        };
        for (const FlagBit& fb : bits) {
            if (const UdmfValue* f = findField(b, fb.name)) {
                if (f->asBool()) t.flags |= fb.bit;
            }
        }
        collectExtra(b, kThingManaged, count(kThingManaged), t.extra);
        out.things.push_back(std::move(t));
    }

    return true;
}

std::string udmfWriteTextmap(const MapSet& map, std::string& error) {
    error.clear();
    std::string out;
    out += "// Written by PocketDoomBuilder\n";
    out += "namespace = \"";
    out += map.udmfNamespace.empty() ? "zdoom" : map.udmfNamespace;
    out += "\";\n";
    {
        // Root-level data we do not manage, emitted before the elements.
        std::vector<std::pair<std::string, UdmfValue>> rootExtra;
        appendExtra(rootExtra, map.udmfRootExtra);
        emitBlock(out, rootExtra, 0);
    }

    // Vertices
    for (size_t i = 0; i < map.vertices.size(); ++i) {
        const Vertex& v = map.vertices[i];
        std::vector<std::pair<std::string, UdmfValue>> c;
        c.emplace_back("x", number(v.x));
        c.emplace_back("y", number(v.y));
        appendExtra(c, v.extra);
        out += "vertex // " + std::to_string(i) + "\n{\n";
        emitBlock(out, c, 1);
        out += "}\n";
    }

    // Linedefs
    for (size_t i = 0; i < map.linedefs.size(); ++i) {
        const Linedef& l = map.linedefs[i];
        std::vector<std::pair<std::string, UdmfValue>> c;
        if (l.tag != 0) c.emplace_back("id", number(l.tag));
        c.emplace_back("v1", number(l.v1));
        c.emplace_back("v2", number(l.v2));
        c.emplace_back("sidefront", number(l.sideFront));
        c.emplace_back("sideback", number(l.sideBack));
        if (l.action != 0) c.emplace_back("special", number(l.action));
        for (int a = 0; a < 5; ++a) {
            if (l.args[a] != 0) {
                c.emplace_back("arg" + std::to_string(a), number(l.args[a]));
            }
        }
        struct FlagBit { const char* name; uint16_t bit; };
        static const FlagBit bits[] = {
            {"blocking", 0x0001},      {"blockmonsters", 0x0002},
            {"twosided", 0x0004},      {"dontpegtop", 0x0008},
            {"dontpegbottom", 0x0010}, {"secret", 0x0020},
            {"blocksound", 0x0040},    {"dontdraw", 0x0080},
            {"mapped", 0x0100},        {"passuse", 0x0200},
            {"blockplayers", 0x0400},  {"blockeverything", 0x0800},
            {"transparent", 0x1000},
        };
        for (const FlagBit& fb : bits) {
            if ((l.flags & fb.bit) != 0) c.emplace_back(fb.name, ident("true"));
        }
        appendExtra(c, l.extra);
        out += "linedef // " + std::to_string(i) + "\n{\n";
        emitBlock(out, c, 1);
        out += "}\n";
    }

    // Sidedefs
    for (size_t i = 0; i < map.sidedefs.size(); ++i) {
        const Sidedef& s = map.sidedefs[i];
        std::vector<std::pair<std::string, UdmfValue>> c;
        if (s.offsetX != 0) c.emplace_back("offsetx", number(s.offsetX));
        if (s.offsetY != 0) c.emplace_back("offsety", number(s.offsetY));
        if (!s.texTop.empty() && s.texTop != "-") c.emplace_back("texturetop", string(s.texTop));
        if (!s.texMid.empty() && s.texMid != "-") c.emplace_back("texturemiddle", string(s.texMid));
        if (!s.texBottom.empty() && s.texBottom != "-") c.emplace_back("texturebottom", string(s.texBottom));
        c.emplace_back("sector", number(s.sector));
        appendExtra(c, s.extra);
        out += "sidedef // " + std::to_string(i) + "\n{\n";
        emitBlock(out, c, 1);
        out += "}\n";
    }

    // Sectors
    for (size_t i = 0; i < map.sectors.size(); ++i) {
        const Sector& s = map.sectors[i];
        std::vector<std::pair<std::string, UdmfValue>> c;
        c.emplace_back("heightfloor", number(s.floorHeight));
        c.emplace_back("heightceiling", number(s.ceilHeight));
        c.emplace_back("texturefloor", string(s.floorTex));
        c.emplace_back("textureceiling", string(s.ceilTex));
        c.emplace_back("lightlevel", number(s.brightness));
        if (s.special != 0) c.emplace_back("special", number(s.special));
        if (s.tag != 0) c.emplace_back("id", number(s.tag));
        appendExtra(c, s.extra);
        out += "sector // " + std::to_string(i) + "\n{\n";
        emitBlock(out, c, 1);
        out += "}\n";
    }

    // Things
    for (size_t i = 0; i < map.things.size(); ++i) {
        const Thing& t = map.things[i];
        std::vector<std::pair<std::string, UdmfValue>> c;
        if (t.tag != 0) c.emplace_back("id", number(t.tag));
        c.emplace_back("x", number(t.x));
        c.emplace_back("y", number(t.y));
        if (t.z != 0) c.emplace_back("height", number(t.z));
        c.emplace_back("angle", number(std::floor(doomAngleToDegrees(t.angle) + 0.5)));
        c.emplace_back("type", number(t.type));
        if (t.action != 0) c.emplace_back("special", number(t.action));
        for (int a = 0; a < 5; ++a) {
            if (t.args[a] != 0) c.emplace_back("arg" + std::to_string(a), number(t.args[a]));
        }
        struct FlagBit { const char* name; uint16_t bit; };
        static const FlagBit bits[] = {
            {"skill1", 0x0001}, {"skill2", 0x0002}, {"skill3", 0x0004},
            {"skill4", 0x0008}, {"skill5", 0x0010}, {"ambush", 0x0020},
            {"friend", 0x0080}, {"single", 0x0100}, {"dm", 0x0200},
            {"coop", 0x0400},
        };
        for (const FlagBit& fb : bits) {
            if ((t.flags & fb.bit) != 0) c.emplace_back(fb.name, ident("true"));
        }
        appendExtra(c, t.extra);
        out += "thing // " + std::to_string(i) + "\n{\n";
        emitBlock(out, c, 1);
        out += "}\n";
    }

    return out;
}

bool loadUdmfMap(const WadFile& wad, const std::string& mapName, MapSet& out,
                 std::string& error, std::vector<std::string>& warnings) {
    const int header = findMapHeader(wad, mapName);
    if (header < 0) {
        error = "Map '" + mapName + "' not found in WAD.";
        return false;
    }
    const WadLump* textmap = nullptr;
    const WadLump* endmap = nullptr;
    for (int i : mapLumpIndices(wad, header)) {
        if (wad.lumps[(size_t)i].name == "TEXTMAP") textmap = &wad.lumps[(size_t)i];
        if (wad.lumps[(size_t)i].name == "ENDMAP") endmap = &wad.lumps[(size_t)i];
    }
    if (!textmap) {
        error = "Map '" + mapName + "' is missing the TEXTMAP lump.";
        return false;
    }
    (void)endmap;

    std::string text(textmap->data.begin(), textmap->data.end());
    if (!udmfReadTextmap(text, out, error, warnings)) return false;
    out.mapName = normaliseLumpName(mapName);
    out.format = MapFormat::UDMF;
    return true;
}

std::vector<uint8_t> writeUdmfMap(const MapSet& map, std::string& error) {
    const std::string text = udmfWriteTextmap(map, error);
    return std::vector<uint8_t>(text.begin(), text.end());
}

} // namespace pdb
