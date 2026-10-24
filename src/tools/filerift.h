#pragma once
#include <string>
#include <vector>

namespace filerift {

    // Decodes a binary protobuf (e.g., "scene", "scl", "gdata", etc.).
    //
    // The decode is all-or-nothing: malformed messages, excessive nesting, or an
    // unsupported type never return a partial document that an editor could
    // save back over the original.
    //
    // Failure reporting: pass `error` and the function returns an empty string
    // with the reason in `*error`. Leave `error` null and it throws instead,
    // which is what the existing callers rely on to abort a save.
    std::string decode_protobuf(const std::string& bytes, const std::string& filetype,
                                std::string* error = nullptr);

    // Encodes FileRift's plain text markup back into binary protobuf.
    //(e.g., "scene", "scl", etc.)
    //
    // Throws std::runtime_error with a "FileRift line N: ..." message when the
    // markup cannot be encoded (syntax, unknown tag, impossible value, Lua
    // compile failure). Callers must treat a throw as "file untouched": the
    // function never returns a partially encoded message.
    //
    // Program messages keep both representations: when a chunk of Lua source is
    // written without an explicit `Bytes : @compile`, the equivalent Lua 5.1
    // bytecode is appended as field 2 before the message closes.
    std::string recode_markup(const std::string& text, const std::string& filetype);

    // True when `bytes` is plausibly FileRift text markup (valid UTF-8 with no
    // NUL or control bytes) rather than a protobuf blob. Use this instead of
    // matching a handful of header prefixes: decoded files that begin with a
    // preserved comment ('# ...') otherwise look binary, and binaries that
    // happen to start with "Object{" look like markup.
    bool looks_like_markup_text(const std::string& bytes);

    // Why `chunk` cannot be loaded by this build's Lua 5.1, or an empty string
    // when it can. Reports truncation, a foreign version/endianness/word size,
    // and 32-bit lua_Number chunks — the shapes luaU_undump accepts just far
    // enough to walk invalid memory.
    std::string lua_bytecode_problem(const std::string& chunk);

    // Check if a filetype or extension is supported by FileRift
    bool is_supported_filetype(const std::string& filetype);

    // Normalize a filetype or extension to its canonical schema name (e.g. "scn" -> "scene", "Map" -> "scmap")
    std::string normalize_filetype(const std::string& filetype);

    // Identify the FileRift filetype from file path and/or content header
    std::string detect_filetype(const std::string& path_or_ext, const std::string& header_or_bytes = "");

    // Returns the list of all canonical FileRift filetypes
    const std::vector<std::string>& supported_filetypes();

    // Our generic lua extractor
    std::string extract_lua_generic(const std::string& bytes);

    // Compiles a plaintext Lua source chunk to Lua 5.1 bytecode with the host
    // runtime embedded in this component (luaL_loadbuffer -> lua_dump). The
    // chunk is compiled, never executed. Returns an empty string on failure and,
    // when `error` is non-null, fills it with the compiler/dump message.
    //
    // Dual-form regulation (Swordigo keeps source AND bytecode per Program):
    // input that is already a complete Lua 5.1 bytecode chunk for this build's
    // ABI is returned byte-identical instead of being re-parsed - Lua 5.1's
    // unchecked undump must never see a chunk that did not come from the
    // parser. Foreign versions (5.2/5.3/5.4), truncated dumps, and dumps built
    // for another word size or number format are rejected: the shipped engine
    // would otherwise hand them to Lua 5.1's undump, which performs no bounds
    // checks.
    std::string compile_lua_to_bytecode(const std::string& source,
                                        const std::string& name = "script",
                                        std::string* error = nullptr);

} // namespace IS filerift
