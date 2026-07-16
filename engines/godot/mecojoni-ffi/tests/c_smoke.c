/* SPDX-License-Identifier: MPL-2.0 */
/*
 * C smoke test for include/mecojoni.h. Not built by cargo; compile manually
 * against the built library, e.g. from the repository root:
 *
 *   cargo build -p mecojoni-ffi --release
 *   cc godot/mecojoni-ffi/tests/c_smoke.c -Igodot/mecojoni-ffi/include \
 *      target/release/libmecojoni_ffi.a -o /tmp/meco_smoke
 *   /tmp/meco_smoke examples/hello.meco path/to/hello.mecob
 *
 * Exercising the header from C (not just Rust unit tests) catches any drift
 * between the declared and implemented ABI: a mismatched struct layout or
 * signature fails loudly here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mecojoni.h"

static int failures = 0;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        if (condition) {                                                       \
            printf("ok   %s\n", label);                                        \
        } else {                                                               \
            printf("FAIL %s\n", label);                                        \
            failures += 1;                                                     \
        }                                                                      \
    } while (0)

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)size);
    size_t read = fread(bytes, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(bytes);
        return NULL;
    }
    *out_len = read;
    return bytes;
}

static void print_error(const char *label, MecoError *error) {
    MecoStrView code = meco_error_code(error);
    MecoStrView message = meco_error_message(error);
    printf("     %s: %s: %s\n", label, code.data ? code.data : "?",
           message.data ? message.data : "?");
}

static void exercise_grammar(const char *label, MecoGrammar *grammar) {
    size_t entries = meco_grammar_entry_count(grammar);
    MecoStrView default_entry = meco_grammar_default_entry(grammar);
    printf("     %s: %zu entries, %zu rules, %zu productions, default=%s\n",
           label, entries, meco_grammar_rule_count(grammar),
           meco_grammar_production_count(grammar),
           default_entry.data ? default_entry.data : "(none)");
    CHECK(entries > 0, "grammar exposes entries");

    MecoGenerateOptions options;
    memset(&options, 0, sizeof options);
    char first[256] = {0};
    for (uint64_t seed = 0; seed < 3; seed += 1) {
        options.seed = seed;
        MecoGenerated *generated = NULL;
        MecoError *error = NULL;
        int32_t status =
            meco_grammar_generate(grammar, &options, &generated, &error);
        if (status != MECO_STATUS_OK) {
            print_error("generate", error);
            meco_error_free(error);
            CHECK(0, "generation succeeds");
            return;
        }
        MecoStrView text = meco_generated_text(generated);
        printf("     seed %llu -> %s\n", (unsigned long long)seed, text.data);
        CHECK(text.len > 0 && text.data[text.len] == '\0',
              "text is sized and NUL-terminated");
        if (seed == 0) {
            snprintf(first, sizeof first, "%s", text.data);
        }
        meco_generated_free(generated);
    }

    /* Determinism: seed 0 must reproduce the first text. */
    options.seed = 0;
    MecoGenerated *replay = NULL;
    if (meco_grammar_generate(grammar, &options, &replay, NULL) ==
        MECO_STATUS_OK) {
        CHECK(strcmp(meco_generated_text(replay).data, first) == 0,
              "same seed replays the same text");
        meco_generated_free(replay);
    } else {
        CHECK(0, "replay generation succeeds");
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <source.meco> <artifact.mecob>\n", argv[0]);
        return 2;
    }

    CHECK(meco_ffi_abi_version() == 1, "ABI version is 1");
    MecoStrView bytecode = meco_bytecode_version();
    CHECK(bytecode.data != NULL && strcmp(bytecode.data, "bytecode/1") == 0,
          "bytecode version is bytecode/1");

    /* Compile source text. */
    size_t source_len = 0;
    uint8_t *source = read_file(argv[1], &source_len);
    if (source == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    MecoGrammar *compiled = NULL;
    MecoError *error = NULL;
    int32_t status = meco_grammar_compile("main.meco", source, source_len,
                                          &compiled, &error);
    free(source);
    CHECK(status == MECO_STATUS_OK, "source compiles");
    if (status != MECO_STATUS_OK) {
        print_error("compile", error);
        meco_error_free(error);
        return 1;
    }
    exercise_grammar("compiled", compiled);

    /* Round-trip through an in-memory artifact. */
    MecoArtifact *artifact = NULL;
    status = meco_grammar_encode(compiled, MECO_PROFILE_FULL, &artifact, &error);
    CHECK(status == MECO_STATUS_OK && meco_artifact_len(artifact) > 0,
          "grammar encodes to an artifact");
    if (status == MECO_STATUS_OK) {
        MecoGrammar *round = NULL;
        status = meco_grammar_decode(meco_artifact_data(artifact),
                                     meco_artifact_len(artifact), &round, &error);
        CHECK(status == MECO_STATUS_OK, "encoded artifact decodes");
        meco_grammar_free(round);
        meco_artifact_free(artifact);
    }
    meco_grammar_free(compiled);

    /* Decode a CLI-produced .mecob from disk. */
    size_t artifact_len = 0;
    uint8_t *artifact_bytes = read_file(argv[2], &artifact_len);
    if (artifact_bytes == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    MecoGrammar *loaded = NULL;
    status = meco_grammar_decode(artifact_bytes, artifact_len, &loaded, &error);
    CHECK(status == MECO_STATUS_OK, "CLI artifact decodes");
    if (status == MECO_STATUS_OK) {
        exercise_grammar("artifact", loaded);
        meco_grammar_free(loaded);
    } else {
        print_error("decode", error);
        meco_error_free(error);
    }

    /* Hostile input must fail cleanly with a stable code. */
    memset(artifact_bytes, 0xFF, artifact_len < 64 ? artifact_len : 64);
    MecoGrammar *corrupt = NULL;
    status = meco_grammar_decode(artifact_bytes, artifact_len, &corrupt, &error);
    CHECK(status == MECO_STATUS_ERROR && corrupt == NULL,
          "corrupt artifact is rejected");
    if (status == MECO_STATUS_ERROR) {
        MecoStrView code = meco_error_code(error);
        CHECK(code.data != NULL && strncmp(code.data, "E_", 2) == 0,
              "rejection carries a stable E_* code");
        meco_error_free(error);
    }
    free(artifact_bytes);

    printf(failures == 0 ? "SMOKE OK\n" : "SMOKE FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
