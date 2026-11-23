// PocketDoomBuilder — GDExtension spike
// pd_core_extension.cpp
//
// Wraps the existing C++ WAD/map core (wad.cpp, mapio.cpp, map.cpp, etc.)
// as a Godot GDExtension class "PDCore" callable from GDScript.
//
// This is a spike: it validates that the C++ core links into a GDExtension
// and that GDScript can call into it through the Godot 4.7 ABI.
//
// Build (see ../ext/build.sh / CI workflow):
//   g++ -shared -fPIC -std=c++17 -O2
//     -I Godot/tools -I Android/app/src/main/cpp/src
//     pd_core_extension.cpp <core .cpp files> -o pd_core.so

#include "gdextension_interface.h"

#include "wad.h"
#include "mapio.h"
#include "map.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Cached interface function pointers (populated in the entry function)
// ---------------------------------------------------------------------------

static GDExtensionInterfaceClassdbRegisterExtensionClass6 g_register_class6 = nullptr;
static GDExtensionInterfaceClassdbRegisterExtensionClassMethod g_register_method = nullptr;
static GDExtensionInterfaceGetVariantFromTypeConstructor g_get_from_ctor = nullptr;
static GDExtensionInterfaceGetVariantToTypeConstructor g_get_to_ctor = nullptr;
static GDExtensionInterfaceGetVariantGetInternalPtrFunc g_get_internal_ptr = nullptr;

// Cached per-type constructor / accessor function pointers
static GDExtensionVariantFromTypeConstructorFunc g_from_string = nullptr;
static GDExtensionVariantFromTypeConstructorFunc g_from_stringname = nullptr;
static GDExtensionVariantFromTypeConstructorFunc g_from_int = nullptr;
static GDExtensionTypeFromVariantConstructorFunc g_to_string = nullptr;
static GDExtensionVariantGetInternalPtrFunc g_stringname_ptr = nullptr;

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-32 helpers (Godot Strings are UTF-32 internally)
// ---------------------------------------------------------------------------

static std::u32string utf8_to_u32(const std::string &s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            out.push_back((char32_t)c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            out.push_back((((char32_t)c & 0x1F) << 6) | ((char32_t)(unsigned char)s[i + 1] & 0x3F));
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            out.push_back((((char32_t)c & 0x0F) << 12) | (((char32_t)(unsigned char)s[i + 1] & 0x3F) << 6) | ((char32_t)(unsigned char)s[i + 2] & 0x3F));
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            out.push_back((((char32_t)c & 0x07) << 18) | (((char32_t)(unsigned char)s[i + 1] & 0x3F) << 12) | (((char32_t)(unsigned char)s[i + 2] & 0x3F) << 6) | ((char32_t)(unsigned char)s[i + 3] & 0x3F));
            i += 4;
        } else {
            out.push_back(0xFFFD);
            i += 1;
        }
    }
    return out;
}

static std::string u32_to_utf8(const std::u32string &s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Variant <-> C++ conversion helpers
// ---------------------------------------------------------------------------

// Read a String argument from a Variant (returns a C++ copy).
static std::string variant_to_string(GDExtensionConstVariantPtr variant) {
    char32_t *raw = nullptr;
    g_to_string((GDExtensionUninitializedTypePtr)&raw, (GDExtensionVariantPtr)variant);
    if (!raw) return std::string();
    std::u32string u32;
    for (char32_t *p = raw; *p; ++p) u32.push_back(*p);
    return u32_to_utf8(u32);
}

// Write a C++ string as a String Variant return value.
static void return_string(GDExtensionVariantPtr r_return, const std::string &utf8) {
    std::u32string u32 = utf8_to_u32(utf8);
    char32_t *raw = new char32_t[u32.size() + 1];
    for (size_t i = 0; i < u32.size(); ++i) raw[i] = u32[i];
    raw[u32.size()] = 0;
    g_from_string((GDExtensionUninitializedVariantPtr)r_return, (GDExtensionTypePtr)&raw);
    delete[] raw;
}

// Write an int return value.
static void return_int(GDExtensionVariantPtr r_return, int64_t value) {
    g_from_int((GDExtensionUninitializedVariantPtr)r_return, (GDExtensionTypePtr)&value);
}

// Build a StringName and return its internal pointer (for class/method names).
static GDExtensionConstStringNamePtr make_stringname(const char *utf8) {
    std::u32string u32 = utf8_to_u32(utf8);
    char32_t *raw = new char32_t[u32.size() + 1];
    for (size_t i = 0; i < u32.size(); ++i) raw[i] = u32[i];
    raw[u32.size()] = 0;
    GDExtensionVariantPtr variant = nullptr;
    g_from_stringname((GDExtensionUninitializedVariantPtr)&variant, (GDExtensionTypePtr)&raw);
    // The Variant holds a reference to the StringName; extract the raw pointer.
    GDExtensionConstStringNamePtr name_ptr =
        (GDExtensionConstStringNamePtr)g_stringname_ptr(variant);
    delete[] raw;
    return name_ptr;
}

// ---------------------------------------------------------------------------
// Instance state
// ---------------------------------------------------------------------------

struct PDCoreInstance {
    pdb::WadFile wad;
    bool has_wad = false;
    std::string wad_path;      // original file path (for save)
    pdb::MapSet loaded_map;    // currently loaded map (for save_map)
    std::string loaded_map_name;
    bool has_map = false;
};

static PDCoreInstance *to_instance(GDExtensionClassInstancePtr p_instance) {
    return static_cast<PDCoreInstance *>(p_instance);
}

// ---------------------------------------------------------------------------
// Method implementations
// ---------------------------------------------------------------------------

// open_wad(path: String) -> int
// Reads a WAD file from disk, returns the number of maps (0 on failure).
static void pdcore_open_wad(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                            const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count,
                            GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    std::string path = variant_to_string(p_args[0]);

    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return_int(r_return, -1);
        return;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(size);
    if (size > 0 && std::fread(bytes.data(), 1, size, f) != (size_t)size) {
        std::fclose(f);
        return_int(r_return, -1);
        return;
    }
    std::fclose(f);

    std::vector<std::string> errors;
    if (!pdb::readWad(bytes, self->wad, errors)) {
        return_int(r_return, -1);
        return;
    }
    self->has_wad = true;
    self->wad_path = path;
    self->has_map = false;
    int count = (int)pdb::listMaps(self->wad).size();
    return_int(r_return, count);
}

// list_maps() -> String
// Returns comma-separated map names in the currently open WAD.
static void pdcore_list_maps(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                             const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                             GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_wad) {
        return_string(r_return, "");
        return;
    }
    std::vector<std::string> maps = pdb::listMaps(self->wad);
    std::string result;
    for (size_t i = 0; i < maps.size(); ++i) {
        if (i > 0) result += ",";
        result += maps[i];
    }
    return_string(r_return, result);
}

// load_map(name: String) -> String
// Loads a map by name, returns a summary string ("format=...; warnings=...") or "error: ...".
static void pdcore_load_map(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                            const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count,
                            GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_wad) {
        return_string(r_return, "error: no wad open");
        return;
    }
    std::string name = variant_to_string(p_args[0]);
    pdb::MapSet map;
    pdb::MapLoadResult result = pdb::loadMap(self->wad, name, map);
    if (!result.ok) {
        return_string(r_return, "error: " + result.error);
        return;
    }
    // Store the loaded map so save_map() can write it back.
    self->loaded_map = map;
    self->loaded_map_name = name;
    self->has_map = true;
    std::string summary = "format=";
    switch (result.format) {
        case pdb::MapFormat::Doom:    summary += "doom"; break;
        case pdb::MapFormat::Hexen:   summary += "hexen"; break;
        case pdb::MapFormat::UDMF:    summary += "udmf"; break;
        default:                      summary += "unknown"; break;
    }
    if (!result.warnings.empty()) {
        summary += "; warnings=";
        for (size_t i = 0; i < result.warnings.size(); ++i) {
            if (i > 0) summary += ",";
            summary += result.warnings[i];
        }
    }
    return_string(r_return, summary);
}

// close() -> void
static void pdcore_close(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                         const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                         GDExtensionVariantPtr /*r_return*/, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    self->wad = pdb::WadFile();
    self->has_wad = false;
    self->wad_path.clear();
    self->loaded_map = pdb::MapSet();
    self->loaded_map_name.clear();
    self->has_map = false;
}

// save_map() -> String
// Saves the currently loaded map back into the WAD and writes the WAD to disk.
// Returns "ok" or "error: ...".
static void pdcore_save_map(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                            const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                            GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_wad) {
        return_string(r_return, "error: no wad open");
        return;
    }
    if (!self->has_map) {
        return_string(r_return, "error: no map loaded");
        return;
    }
    std::string error;
    if (!pdb::saveMap(self->wad, self->loaded_map, error)) {
        return_string(r_return, "error: " + error);
        return;
    }
    // Write the updated WAD back to disk.
    if (!self->wad_path.empty()) {
        std::vector<uint8_t> bytes = pdb::writeWad(self->wad);
        FILE *f = std::fopen(self->wad_path.c_str(), "wb");
        if (f) {
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
        }
    }
    return_string(r_return, "ok");
}

// save_wad_as(path: String) -> int
// Writes the current WAD (with any loaded map edits) to a new path.
// Returns 0 on success, -1 on failure.
static void pdcore_save_wad_as(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                               const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count,
                               GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_wad) {
        return_int(r_return, -1);
        return;
    }
    std::string path = variant_to_string(p_args[0]);
    // If a map is loaded, save it into the WAD first so the output is current.
    if (self->has_map) {
        std::string error;
        pdb::saveMap(self->wad, self->loaded_map, error);
    }
    std::vector<uint8_t> bytes = pdb::writeWad(self->wad);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return_int(r_return, -1);
        return;
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    self->wad_path = path;
    return_int(r_return, 0);
}

// get_wad_path() -> String
static void pdcore_get_wad_path(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                                const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                                GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    return_string(r_return, self->wad_path);
}

// get_current_map() -> String
static void pdcore_get_current_map(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                                   const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                                   GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_map) {
        return_string(r_return, "");
        return;
    }
    return_string(r_return, self->loaded_map_name);
}

// get_map_count() -> int
static void pdcore_get_map_count(void * /*method_userdata*/, GDExtensionClassInstancePtr p_instance,
                                 const GDExtensionConstVariantPtr * /*p_args*/, GDExtensionInt /*p_argument_count*/,
                                 GDExtensionVariantPtr r_return, GDExtensionCallError * /*r_error*/) {
    PDCoreInstance *self = to_instance(p_instance);
    if (!self->has_wad) {
        return_int(r_return, 0);
        return;
    }
    return_int(r_return, (int)pdb::listMaps(self->wad).size());
}

// ---------------------------------------------------------------------------
// Instance lifecycle
// ---------------------------------------------------------------------------

static GDExtensionObjectPtr pdcore_create_instance(void * /*class_userdata*/, GDExtensionBool /*p_notify_postinitialize*/) {
    return (GDExtensionObjectPtr)new PDCoreInstance();
}

static void pdcore_free_instance(void * /*class_userdata*/, GDExtensionClassInstancePtr p_instance) {
    delete static_cast<PDCoreInstance *>(p_instance);
}

// ---------------------------------------------------------------------------
// Class + method registration
// ---------------------------------------------------------------------------

static void pdcore_register_class(GDExtensionClassLibraryPtr p_library) {
    // Build StringNames for class and methods.
    GDExtensionConstStringNamePtr s_class = make_stringname("PDCore");
    GDExtensionConstStringNamePtr s_parent = make_stringname("RefCounted");

    GDExtensionConstStringNamePtr s_open_wad = make_stringname("open_wad");
    GDExtensionConstStringNamePtr s_list_maps = make_stringname("list_maps");
    GDExtensionConstStringNamePtr s_load_map = make_stringname("load_map");
    GDExtensionConstStringNamePtr s_close = make_stringname("close");
    GDExtensionConstStringNamePtr s_save_map = make_stringname("save_map");
    GDExtensionConstStringNamePtr s_save_wad_as = make_stringname("save_wad_as");
    GDExtensionConstStringNamePtr s_get_wad_path = make_stringname("get_wad_path");
    GDExtensionConstStringNamePtr s_get_current_map = make_stringname("get_current_map");
    GDExtensionConstStringNamePtr s_get_map_count = make_stringname("get_map_count");

    // PropertyInfo for argument/return metadata.
    GDExtensionPropertyInfo arg_path = {};
    arg_path.type = GDEXTENSION_VARIANT_TYPE_STRING;
    arg_path.name = (GDExtensionStringNamePtr)make_stringname("path");
    GDExtensionPropertyInfo arg_name = {};
    arg_name.type = GDEXTENSION_VARIANT_TYPE_STRING;
    arg_name.name = (GDExtensionStringNamePtr)make_stringname("name");

    GDExtensionPropertyInfo ret_int = {};
    ret_int.type = GDEXTENSION_VARIANT_TYPE_INT;
    ret_int.name = (GDExtensionStringNamePtr)make_stringname("count");

    GDExtensionPropertyInfo ret_str = {};
    ret_str.type = GDEXTENSION_VARIANT_TYPE_STRING;
    ret_str.name = (GDExtensionStringNamePtr)make_stringname("result");

    GDExtensionClassMethodArgumentMetadata no_meta = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;

    // Register the class itself (methods are registered separately).
    GDExtensionClassCreationInfo6 class_info = {};
    class_info.create_instance_func = pdcore_create_instance;
    class_info.free_instance_func = pdcore_free_instance;
    // is_virtual / is_abstract / is_exposed / is_runtime default to false (0).

    g_register_class6(p_library, s_class, s_parent, &class_info);

    // Register methods individually via classdb_register_extension_class_method.
    GDExtensionClassMethodInfo m;
    memset(&m, 0, sizeof(m));

    // open_wad(path: String) -> int
    m.name = (GDExtensionStringNamePtr)s_open_wad;
    m.method_userdata = nullptr;
    m.call_func = pdcore_open_wad;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_int;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 1;
    m.arguments_info = &arg_path;
    m.arguments_metadata = &no_meta;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // list_maps() -> String
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_list_maps;
    m.method_userdata = nullptr;
    m.call_func = pdcore_list_maps;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_str;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // load_map(name: String) -> String
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_load_map;
    m.method_userdata = nullptr;
    m.call_func = pdcore_load_map;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_str;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 1;
    m.arguments_info = &arg_name;
    m.arguments_metadata = &no_meta;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // close() -> void
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_close;
    m.method_userdata = nullptr;
    m.call_func = pdcore_close;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = false;
    m.return_value_info = nullptr;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // save_map() -> String
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_save_map;
    m.method_userdata = nullptr;
    m.call_func = pdcore_save_map;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_str;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // save_wad_as(path: String) -> int
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_save_wad_as;
    m.method_userdata = nullptr;
    m.call_func = pdcore_save_wad_as;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_int;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 1;
    m.arguments_info = &arg_path;
    m.arguments_metadata = &no_meta;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // get_wad_path() -> String
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_get_wad_path;
    m.method_userdata = nullptr;
    m.call_func = pdcore_get_wad_path;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_str;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // get_current_map() -> String
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_get_current_map;
    m.method_userdata = nullptr;
    m.call_func = pdcore_get_current_map;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_str;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);

    // get_map_count() -> int
    memset(&m, 0, sizeof(m));
    m.name = (GDExtensionStringNamePtr)s_get_map_count;
    m.method_userdata = nullptr;
    m.call_func = pdcore_get_map_count;
    m.method_flags = GDEXTENSION_METHOD_FLAG_NORMAL;
    m.has_return_value = true;
    m.return_value_info = &ret_int;
    m.return_value_metadata = GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    m.argument_count = 0;
    m.arguments_info = nullptr;
    m.arguments_metadata = nullptr;
    m.default_argument_count = 0;
    g_register_method(p_library, s_class, &m);
}

// ---------------------------------------------------------------------------
// Initialization callbacks
// ---------------------------------------------------------------------------

static void pdcore_initialize(void * /*p_userdata*/, GDExtensionInitializationLevel p_level) {
    if (p_level == GDEXTENSION_INITIALIZATION_SCENE) {
        // p_library is captured via the static g_register_class6 target; but
        // we need the library pointer. It is passed to the entry function and
        // stored here.
        extern GDExtensionClassLibraryPtr g_pdcore_library;
        pdcore_register_class(g_pdcore_library);
    }
}

static void pdcore_deinitialize(void * /*p_userdata*/, GDExtensionInitializationLevel /*p_level*/) {
    // No cleanup needed for the spike.
}

// Library pointer captured from the entry function (needed by the SCENE
// initialization callback).
GDExtensionClassLibraryPtr g_pdcore_library = nullptr;

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" GDExtensionBool pd_core_extension_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {

    // Load interface functions we need.
    g_register_class6 = (GDExtensionInterfaceClassdbRegisterExtensionClass6)
        p_get_proc_address("classdb_register_extension_class6");
    g_register_method = (GDExtensionInterfaceClassdbRegisterExtensionClassMethod)
        p_get_proc_address("classdb_register_extension_class_method");
    g_get_from_ctor = (GDExtensionInterfaceGetVariantFromTypeConstructor)
        p_get_proc_address("get_variant_from_type_constructor");
    g_get_to_ctor = (GDExtensionInterfaceGetVariantToTypeConstructor)
        p_get_proc_address("get_variant_to_type_constructor");
    g_get_internal_ptr = (GDExtensionInterfaceGetVariantGetInternalPtrFunc)
        p_get_proc_address("variant_get_ptr_internal_getter");

    // Cache per-type constructors.
    g_from_string = g_get_from_ctor(GDEXTENSION_VARIANT_TYPE_STRING);
    g_from_stringname = g_get_from_ctor(GDEXTENSION_VARIANT_TYPE_STRING_NAME);
    g_from_int = g_get_from_ctor(GDEXTENSION_VARIANT_TYPE_INT);
    g_to_string = g_get_to_ctor(GDEXTENSION_VARIANT_TYPE_STRING);
    g_stringname_ptr = g_get_internal_ptr(GDEXTENSION_VARIANT_TYPE_STRING_NAME);

    // Capture the library pointer for the SCENE init callback.
    g_pdcore_library = p_library;

    // Fill in the initialization struct.
    r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    r_initialization->userdata = nullptr;
    r_initialization->initialize = pdcore_initialize;
    r_initialization->deinitialize = pdcore_deinitialize;

    return (GDExtensionBool)true;
}
