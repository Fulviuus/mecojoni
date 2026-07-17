/* SPDX-License-Identifier: MPL-2.0 */
/*
 * C ABI for embedding the Mecojoni compiler and runtime.
 *
 * Implemented by the `mecojoni-ffi` Rust crate (src/lib.rs); this header is
 * the authoritative caller-facing contract. General rules:
 *
 *  - Every `Meco*` object is an opaque heap pointer owned by the caller and
 *    released with the matching `meco_*_free` function. Passing null to a
 *    `*_free` function is a no-op.
 *  - There is no global state. Distinct objects may be used from distinct
 *    threads, but one object must not be used from two threads at once.
 *  - Functions returning `int32_t` return MECO_STATUS_OK (0) on success.
 *    On MECO_STATUS_ERROR the optional `out_error` parameter (when non-null)
 *    receives a MecoError the caller must free. MECO_STATUS_INVALID_ARGUMENT
 *    reports null/malformed pointers and writes no error object.
 *  - `MecoStrView` results are UTF-8, explicitly sized, and additionally
 *    NUL-terminated at data[len]. They borrow from the queried object and
 *    stay valid until that object is freed. A null `data` means "absent".
 *  - `const char *` parameters are NUL-terminated UTF-8 owned by the caller;
 *    they are copied before the call returns.
 */
#ifndef MECOJONI_H
#define MECOJONI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- status codes -------------------------------------------------- */

#define MECO_STATUS_OK 0
#define MECO_STATUS_ERROR 1
#define MECO_STATUS_INVALID_ARGUMENT 2

/* ---- data binding kinds -------------------------------------------- */

#define MECO_VALUE_TEXT 0u
#define MECO_VALUE_NUMBER 1u
#define MECO_VALUE_BOOLEAN 2u
#define MECO_VALUE_ENUM 3u

/* ---- artifact debug profiles ---------------------------------------- */

#define MECO_PROFILE_FULL 0u
#define MECO_PROFILE_MAPPED 1u
#define MECO_PROFILE_STRIPPED 2u

/* ---- opaque objects -------------------------------------------------- */

typedef struct MecoGrammar MecoGrammar;
typedef struct MecoPackageBuilder MecoPackageBuilder;
typedef struct MecoGenerated MecoGenerated;
typedef struct MecoArtifact MecoArtifact;
typedef struct MecoError MecoError;

/* ---- plain structs (layout shared with Rust `#[repr(C)]`) ----------- */

typedef struct MecoStrView {
    const char *data; /* UTF-8; NUL at data[len]; null when absent */
    size_t len;       /* bytes, excluding the trailing NUL */
} MecoStrView;

typedef struct MecoDataBinding {
    const char *name;    /* declared input name */
    const char *text;    /* payload for TEXT and ENUM kinds */
    int64_t numerator;   /* payload for NUMBER kind */
    uint64_t denominator; /* payload for NUMBER kind; must be non-zero */
    uint32_t kind;       /* one of MECO_VALUE_* */
    uint8_t boolean;     /* payload for BOOLEAN kind; 0 or 1 */
} MecoDataBinding;

typedef struct MecoGenerateOptions {
    const char *entry;            /* qualified public entry; null or "" for default */
    const MecoDataBinding *data;  /* may be null when data_count is 0 */
    size_t data_count;
    uint64_t seed;                /* splitmix64 seed; same seed => same text */
    /* Deterministic work limits; 0 selects the interactive default. */
    uint32_t max_depth;
    uint32_t max_expansions;
    uint32_t max_output_scalars;
    uint32_t max_output_bytes;
    uint32_t max_sampler_words;
} MecoGenerateOptions;

/* ---- versioning ------------------------------------------------------ */

/* Version of this C ABI. Check before any other call. */
uint32_t meco_ffi_abi_version(void);
/* mecojoni-core Rust API version linked into this library. */
uint32_t meco_core_api_version(void);
/* Frozen artifact format identifier, currently "bytecode/1". */
MecoStrView meco_bytecode_version(void);

/* ---- errors ---------------------------------------------------------- */

/* Stable diagnostic code of the first failure, e.g. "E_BYTECODE_CORRUPT". */
MecoStrView meco_error_code(const MecoError *error);
/* Every diagnostic rendered as one line each. */
MecoStrView meco_error_message(const MecoError *error);
void meco_error_free(MecoError *error);

/* ---- package building and compilation -------------------------------- */

MecoPackageBuilder *meco_package_builder_new(void);
/* Adds one module. `source_utf8`/`source_len` need not be NUL-terminated. */
int32_t meco_package_builder_add_module(MecoPackageBuilder *builder,
                                        const char *canonical_id,
                                        const char *display_name,
                                        const uint8_t *source_utf8,
                                        size_t source_len,
                                        MecoError **out_error);
/* Resolves one authored `imports:` path of `module_canonical_id` to the
 * canonical ID of another added module. Every authored import needs exactly
 * one resolution. */
int32_t meco_package_builder_resolve_import(MecoPackageBuilder *builder,
                                            const char *module_canonical_id,
                                            const char *authored_path,
                                            const char *target_canonical_id,
                                            MecoError **out_error);
/* Compiles the accumulated package rooted at `root_canonical_id`; null or ""
 * selects the first added module. The builder stays reusable. */
int32_t meco_package_builder_compile(const MecoPackageBuilder *builder,
                                     const char *root_canonical_id,
                                     MecoGrammar **out_grammar,
                                     MecoError **out_error);
void meco_package_builder_free(MecoPackageBuilder *builder);

/* Single-module convenience: compile one importless `.meco` source. */
int32_t meco_grammar_compile(const char *name,
                             const uint8_t *source_utf8,
                             size_t source_len,
                             MecoGrammar **out_grammar,
                             MecoError **out_error);

/* ---- artifacts (compiled `.mecob` bytecode) --------------------------- */

/* Decodes and verifies a `bytecode/1` artifact under the standard hostile-
 * input limits. No partial grammar is ever returned. */
int32_t meco_grammar_decode(const uint8_t *bytes,
                            size_t len,
                            MecoGrammar **out_grammar,
                            MecoError **out_error);
/* Encodes a compiled grammar; `debug_profile` is one of MECO_PROFILE_*. */
int32_t meco_grammar_encode(const MecoGrammar *grammar,
                            uint32_t debug_profile,
                            MecoArtifact **out_artifact,
                            MecoError **out_error);
const uint8_t *meco_artifact_data(const MecoArtifact *artifact);
size_t meco_artifact_len(const MecoArtifact *artifact);
void meco_artifact_free(MecoArtifact *artifact);

/* ---- grammar introspection -------------------------------------------- */

size_t meco_grammar_entry_count(const MecoGrammar *grammar);
/* Qualified public entry name, or an absent view when out of range. */
MecoStrView meco_grammar_entry(const MecoGrammar *grammar, size_t index);
/* Absent view when the package declares no default entry. */
MecoStrView meco_grammar_default_entry(const MecoGrammar *grammar);
size_t meco_grammar_rule_count(const MecoGrammar *grammar);
size_t meco_grammar_production_count(const MecoGrammar *grammar);
size_t meco_grammar_warning_count(const MecoGrammar *grammar);
MecoStrView meco_grammar_warning_code(const MecoGrammar *grammar, size_t index);
MecoStrView meco_grammar_warning_message(const MecoGrammar *grammar, size_t index);
void meco_grammar_free(MecoGrammar *grammar);

/* ---- generation -------------------------------------------------------- */

/* One stateless deterministic `weighted/1` generation. Entries that produce
 * complete localized messages require a host formatter and are reported as
 * E_FORMATTER_REQUIRED for now. */
int32_t meco_grammar_generate(const MecoGrammar *grammar,
                              const MecoGenerateOptions *options,
                              MecoGenerated **out_generated,
                              MecoError **out_error);
MecoStrView meco_generated_text(const MecoGenerated *generated);
MecoStrView meco_generated_entry(const MecoGenerated *generated);
uint32_t meco_generated_expansions(const MecoGenerated *generated);
uint32_t meco_generated_sampler_words(const MecoGenerated *generated);
void meco_generated_free(MecoGenerated *generated);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MECOJONI_H */
