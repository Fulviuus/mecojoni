// SPDX-License-Identifier: MPL-2.0
//! Exercises the exported C ABI exactly as a foreign caller would: through
//! the `extern "C"` functions with raw pointers, never through internals.

use std::ffi::CString;
use std::ptr;

use super::*;

const HELLO: &str = "---\nmeco: 1.0\nmodule: hello\nentry: greeting\nsampler: weighted/1\nexports: [greeting]\n---\n\n# greeting\n- [3] Hello, @person!\n- [1] Welcome back, @person.\n\n# person\n- traveller\n- neighbour\n- old friend\n";

const NPC_ROOT: &str = "---\nmeco: 1.0\nmodule: npc\nentry: line\nsampler: weighted/1\ninputs:\n  playerName: text\nimports:\n  common: \"./common.meco\"\nexports: [line]\n---\n\n# line\n- $playerName, @common.observation.\n";

const NPC_COMMON: &str = "---\nmeco: 1.0\nmodule: common\nexports: [observation]\n---\n\n# observation\n- the market is unusually quiet\n- the weather has changed\n";

fn view_str(view: &MecoStrView) -> &str {
    if view.data.is_null() {
        return "";
    }
    // The ABI guarantees UTF-8 plus a trailing NUL one byte past `len`.
    let bytes = unsafe { std::slice::from_raw_parts(view.data.cast::<u8>(), view.len + 1) };
    assert_eq!(bytes[view.len], 0, "views must stay NUL-terminated");
    std::str::from_utf8(&bytes[..view.len]).expect("views must stay UTF-8")
}

fn compile(name: &str, source: &str) -> *mut MecoGrammar {
    let name = CString::new(name).unwrap();
    let mut grammar = ptr::null_mut();
    let mut error = ptr::null_mut();
    let status = unsafe {
        meco_grammar_compile(
            name.as_ptr(),
            source.as_ptr(),
            source.len(),
            &raw mut grammar,
            &raw mut error,
        )
    };
    assert_eq!(status, MECO_STATUS_OK, "{}", unsafe {
        let message = view_str(&meco_error_message(error)).to_owned();
        meco_error_free(error);
        message
    });
    assert!(!grammar.is_null());
    grammar
}

fn generate(grammar: *const MecoGrammar, options: &MecoGenerateOptions) -> (String, String, u32) {
    let mut generated = ptr::null_mut();
    let mut error = ptr::null_mut();
    let status =
        unsafe { meco_grammar_generate(grammar, options, &raw mut generated, &raw mut error) };
    assert_eq!(status, MECO_STATUS_OK, "{}", unsafe {
        let message = view_str(&meco_error_message(error)).to_owned();
        meco_error_free(error);
        message
    });
    let (text, entry, expansions) = unsafe {
        (
            view_str(&meco_generated_text(generated)).to_owned(),
            view_str(&meco_generated_entry(generated)).to_owned(),
            meco_generated_expansions(generated),
        )
    };
    unsafe { meco_generated_free(generated) };
    (text, entry, expansions)
}

fn default_options(seed: u64) -> MecoGenerateOptions {
    MecoGenerateOptions {
        entry: ptr::null(),
        data: ptr::null(),
        data_count: 0,
        seed,
        max_depth: 0,
        max_expansions: 0,
        max_output_scalars: 0,
        max_output_bytes: 0,
        max_sampler_words: 0,
    }
}

#[test]
fn reports_versions() {
    assert_eq!(meco_ffi_abi_version(), FFI_ABI_VERSION);
    assert_eq!(meco_core_api_version(), mecojoni_core::API_VERSION);
    assert_eq!(
        view_str(&meco_bytecode_version()),
        mecojoni_core::BYTECODE_VERSION
    );
}

#[test]
fn compiles_and_generates_deterministically() {
    let grammar = compile("hello.meco", HELLO);
    unsafe {
        assert!(meco_grammar_entry_count(grammar) >= 1);
        assert!(meco_grammar_rule_count(grammar) >= 2);
        assert!(!meco_grammar_default_entry(grammar).data.is_null());
    }

    let (first, entry, expansions) = generate(grammar, &default_options(42));
    let (second, _, _) = generate(grammar, &default_options(42));
    assert_eq!(first, second, "same seed must reproduce the same text");
    assert!(
        first.starts_with("Hello, ") || first.starts_with("Welcome back, "),
        "unexpected text: {first}"
    );
    assert!(!entry.is_empty());
    assert!(expansions > 0);

    let mut texts = std::collections::BTreeSet::new();
    for seed in 0..32 {
        texts.insert(generate(grammar, &default_options(seed)).0);
    }
    assert!(texts.len() > 1, "different seeds should vary the output");

    unsafe { meco_grammar_free(grammar) };
}

#[test]
fn explicit_entry_matches_reported_entry_name() {
    let grammar = compile("hello.meco", HELLO);
    let entry_name = unsafe { view_str(&meco_grammar_entry(grammar, 0)).to_owned() };
    let entry_z = CString::new(entry_name.clone()).unwrap();
    let mut options = default_options(7);
    options.entry = entry_z.as_ptr();
    let (_, reported, _) = generate(grammar, &options);
    assert_eq!(reported, entry_name);
    unsafe { meco_grammar_free(grammar) };
}

#[test]
fn compiles_multi_module_package_with_data_bindings() {
    let root_id = CString::new("root").unwrap();
    let common_id = CString::new("common").unwrap();
    let root_name = CString::new("root.meco").unwrap();
    let common_name = CString::new("common.meco").unwrap();
    let authored = CString::new("./common.meco").unwrap();

    let builder = meco_package_builder_new();
    assert!(!builder.is_null());
    let mut error = ptr::null_mut();
    unsafe {
        assert_eq!(
            meco_package_builder_add_module(
                builder,
                root_id.as_ptr(),
                root_name.as_ptr(),
                NPC_ROOT.as_ptr(),
                NPC_ROOT.len(),
                &raw mut error,
            ),
            MECO_STATUS_OK
        );
        assert_eq!(
            meco_package_builder_add_module(
                builder,
                common_id.as_ptr(),
                common_name.as_ptr(),
                NPC_COMMON.as_ptr(),
                NPC_COMMON.len(),
                &raw mut error,
            ),
            MECO_STATUS_OK
        );
        assert_eq!(
            meco_package_builder_resolve_import(
                builder,
                root_id.as_ptr(),
                authored.as_ptr(),
                common_id.as_ptr(),
                &raw mut error,
            ),
            MECO_STATUS_OK
        );
    }

    let mut grammar = ptr::null_mut();
    // Null root selects the first added module, which is the intended root.
    let status = unsafe {
        meco_package_builder_compile(builder, ptr::null(), &raw mut grammar, &raw mut error)
    };
    assert_eq!(status, MECO_STATUS_OK, "{}", unsafe {
        let message = view_str(&meco_error_message(error)).to_owned();
        meco_error_free(error);
        message
    });
    unsafe { meco_package_builder_free(builder) };

    let name = CString::new("playerName").unwrap();
    let text = CString::new("Ripley").unwrap();
    let binding = MecoDataBinding {
        name: name.as_ptr(),
        text: text.as_ptr(),
        numerator: 0,
        denominator: 0,
        kind: MECO_VALUE_TEXT,
        boolean: 0,
    };
    let mut options = default_options(3);
    options.data = &raw const binding;
    options.data_count = 1;
    let (text, _, _) = generate(grammar, &options);
    assert!(text.starts_with("Ripley, "), "unexpected text: {text}");

    // The same request without the declared input must fail cleanly.
    let mut generated = ptr::null_mut();
    let mut error = ptr::null_mut();
    let status = unsafe {
        meco_grammar_generate(
            grammar,
            &default_options(3),
            &raw mut generated,
            &raw mut error,
        )
    };
    assert_eq!(status, MECO_STATUS_ERROR);
    assert!(generated.is_null());
    unsafe {
        assert!(!view_str(&meco_error_code(error)).is_empty());
        meco_error_free(error);
        meco_grammar_free(grammar);
    }
}

#[test]
fn artifact_round_trip_preserves_generation() {
    let grammar = compile("hello.meco", HELLO);
    let mut artifact = ptr::null_mut();
    let mut error = ptr::null_mut();
    let status = unsafe {
        meco_grammar_encode(
            grammar,
            MECO_PROFILE_FULL,
            &raw mut artifact,
            &raw mut error,
        )
    };
    assert_eq!(status, MECO_STATUS_OK);

    let (data, len) = unsafe { (meco_artifact_data(artifact), meco_artifact_len(artifact)) };
    assert!(!data.is_null());
    assert!(len > 0);

    let mut decoded = ptr::null_mut();
    let status = unsafe { meco_grammar_decode(data, len, &raw mut decoded, &raw mut error) };
    assert_eq!(status, MECO_STATUS_OK, "{}", unsafe {
        let message = view_str(&meco_error_message(error)).to_owned();
        meco_error_free(error);
        message
    });

    for seed in [0, 1, 42, u64::MAX] {
        let (source_text, ..) = generate(grammar, &default_options(seed));
        let (decoded_text, ..) = generate(decoded, &default_options(seed));
        assert_eq!(source_text, decoded_text);
    }

    unsafe {
        meco_artifact_free(artifact);
        meco_grammar_free(decoded);
        meco_grammar_free(grammar);
    }
}

#[test]
fn rejects_corrupt_artifacts_and_bad_source() {
    let mut grammar = ptr::null_mut();
    let mut error = ptr::null_mut();
    let garbage = [0xFFu8; 64];
    let status = unsafe {
        meco_grammar_decode(
            garbage.as_ptr(),
            garbage.len(),
            &raw mut grammar,
            &raw mut error,
        )
    };
    assert_eq!(status, MECO_STATUS_ERROR);
    assert!(grammar.is_null());
    unsafe {
        assert!(view_str(&meco_error_code(error)).starts_with("E_"));
        meco_error_free(error);
    }

    let name = CString::new("broken.meco").unwrap();
    let source = "# not a module: missing front matter";
    let status = unsafe {
        meco_grammar_compile(
            name.as_ptr(),
            source.as_ptr(),
            source.len(),
            &raw mut grammar,
            &raw mut error,
        )
    };
    assert_eq!(status, MECO_STATUS_ERROR);
    assert!(grammar.is_null());
    unsafe {
        assert!(!view_str(&meco_error_message(error)).is_empty());
        meco_error_free(error);
    }
}

#[test]
fn rejects_null_arguments_without_crashing() {
    unsafe {
        assert_eq!(
            meco_grammar_decode(ptr::null(), 4, ptr::null_mut(), ptr::null_mut()),
            MECO_STATUS_INVALID_ARGUMENT
        );
        assert_eq!(
            meco_grammar_generate(ptr::null(), ptr::null(), ptr::null_mut(), ptr::null_mut()),
            MECO_STATUS_INVALID_ARGUMENT
        );
        assert_eq!(meco_grammar_entry_count(ptr::null()), 0);
        assert!(meco_grammar_default_entry(ptr::null()).data.is_null());
        assert_eq!(meco_generated_expansions(ptr::null()), 0);
        // Free functions must accept null.
        meco_grammar_free(ptr::null_mut());
        meco_error_free(ptr::null_mut());
        meco_generated_free(ptr::null_mut());
        meco_artifact_free(ptr::null_mut());
        meco_package_builder_free(ptr::null_mut());
    }
}
