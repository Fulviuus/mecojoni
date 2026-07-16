// SPDX-License-Identifier: MPL-2.0
//! Handle-based C ABI over `mecojoni-core` for native hosts (Godot, C, C++).
//!
//! The contract is declared once in `include/mecojoni.h`; this file is its
//! only implementation. Every exported object is an opaque heap pointer that
//! the caller frees with the matching `*_free` function. No global state is
//! kept, so distinct objects may live on distinct threads; one object must
//! not be used from two threads at once.
//!
//! String views returned to the caller (`MecoStrView`) are UTF-8, carry an
//! explicit length, and are additionally NUL-terminated one byte past `len`.
//! They stay valid until the owning object is freed and are never invalidated
//! by read-only accessor calls.
//!
//! The workspace builds with `panic = "abort"`; `mecojoni-core` forbids
//! unsafe code and returns typed errors for hostile input, so a panic across
//! this boundary indicates a bug and deliberately aborts instead of unwinding
//! into C.

#![allow(unsafe_code)]
#![allow(clippy::missing_safety_doc)] // Safety contract lives in include/mecojoni.h.

use std::ffi::{CStr, c_char};
use std::ptr;

use mecojoni_core::{
    ArtifactDebugProfile, ArtifactLimits, ArtifactOptions, CompiledGrammar, DataBinding,
    Diagnostic, GenerationLimits, GenerationRequest, MecoError as CoreError, PackageInput,
    PackageSource, Rational, ResolvedImport, SourceFile, SourceId, Value, compile_package,
    decode_artifact, encode_artifact, validate_package_input,
};

/// Version of this C ABI, independent of `mecojoni_core::API_VERSION`.
pub const FFI_ABI_VERSION: u32 = 1;

pub const MECO_STATUS_OK: i32 = 0;
pub const MECO_STATUS_ERROR: i32 = 1;
pub const MECO_STATUS_INVALID_ARGUMENT: i32 = 2;

pub const MECO_VALUE_TEXT: u32 = 0;
pub const MECO_VALUE_NUMBER: u32 = 1;
pub const MECO_VALUE_BOOLEAN: u32 = 2;
pub const MECO_VALUE_ENUM: u32 = 3;

pub const MECO_PROFILE_FULL: u32 = 0;
pub const MECO_PROFILE_MAPPED: u32 = 1;
pub const MECO_PROFILE_STRIPPED: u32 = 2;

static BYTECODE_VERSION_Z: &[u8] = b"bytecode/1\0";

/// Owned UTF-8 buffer with a guaranteed trailing NUL, viewed across the ABI.
struct OwnedText(Vec<u8>);

impl OwnedText {
    fn new(text: &str) -> Self {
        let mut bytes = Vec::with_capacity(text.len() + 1);
        bytes.extend_from_slice(text.as_bytes());
        bytes.push(0);
        Self(bytes)
    }

    fn view(&self) -> MecoStrView {
        MecoStrView {
            data: self.0.as_ptr().cast(),
            len: self.0.len() - 1,
        }
    }
}

/// Borrowed UTF-8 view; `data[len]` is always a NUL byte when `data` is
/// non-null. Matches `MecoStrView` in `include/mecojoni.h`.
#[repr(C)]
pub struct MecoStrView {
    pub data: *const c_char,
    pub len: usize,
}

impl MecoStrView {
    const fn empty() -> Self {
        Self {
            data: ptr::null(),
            len: 0,
        }
    }
}

/// One typed host input. Matches `MecoDataBinding` in `include/mecojoni.h`.
#[repr(C)]
pub struct MecoDataBinding {
    pub name: *const c_char,
    pub text: *const c_char,
    pub numerator: i64,
    pub denominator: u64,
    pub kind: u32,
    pub boolean: u8,
}

/// One generation request. Matches `MecoGenerateOptions` in
/// `include/mecojoni.h`. Zero-valued limits select the interactive defaults.
#[repr(C)]
pub struct MecoGenerateOptions {
    pub entry: *const c_char,
    pub data: *const MecoDataBinding,
    pub data_count: usize,
    pub seed: u64,
    pub max_depth: u32,
    pub max_expansions: u32,
    pub max_output_scalars: u32,
    pub max_output_bytes: u32,
    pub max_sampler_words: u32,
}

/// Opaque compiled grammar with pre-rendered accessor strings.
pub struct MecoGrammar {
    grammar: CompiledGrammar,
    entries: Vec<OwnedText>,
    default_entry: Option<OwnedText>,
    warnings: Vec<(OwnedText, OwnedText)>,
}

impl MecoGrammar {
    fn new(grammar: CompiledGrammar) -> Self {
        let entries = grammar.entries().map(OwnedText::new).collect();
        let default_entry = grammar.default_entry().map(OwnedText::new);
        let warnings = grammar
            .warnings()
            .iter()
            .map(|warning| {
                (
                    OwnedText::new(warning.code().as_str()),
                    OwnedText::new(warning.message()),
                )
            })
            .collect();
        Self {
            grammar,
            entries,
            default_entry,
            warnings,
        }
    }
}

/// Opaque incremental package description compiled in one call.
pub struct MecoPackageBuilder {
    modules: Vec<PackageSource>,
}

/// Opaque completed generation.
pub struct MecoGenerated {
    text: OwnedText,
    entry: OwnedText,
    expansions: u32,
    sampler_words: u32,
}

/// Opaque encoded `bytecode/1` artifact.
pub struct MecoArtifact {
    bytes: Vec<u8>,
}

/// Opaque structured failure: the first stable diagnostic code plus every
/// diagnostic rendered as one message per line.
pub struct MecoError {
    code: OwnedText,
    message: OwnedText,
}

impl MecoError {
    fn from_core(error: &CoreError) -> Self {
        let diagnostics = error.diagnostics();
        let code = diagnostics
            .first()
            .map_or("E_UNKNOWN", |diagnostic| diagnostic.code().as_str());
        let message = diagnostics
            .iter()
            .map(render_diagnostic)
            .collect::<Vec<_>>()
            .join("\n");
        Self {
            code: OwnedText::new(code),
            message: OwnedText::new(&message),
        }
    }

    fn new(code: &str, message: &str) -> Self {
        Self {
            code: OwnedText::new(code),
            message: OwnedText::new(message),
        }
    }
}

fn render_diagnostic(diagnostic: &Diagnostic) -> String {
    match diagnostic.span() {
        Some(span) => format!(
            "{} [module {} bytes {}..{}]: {}",
            diagnostic.code().as_str(),
            span.source().get(),
            span.start().byte(),
            span.end().byte(),
            diagnostic.message()
        ),
        None => format!("{}: {}", diagnostic.code().as_str(), diagnostic.message()),
    }
}

/// Writes `error` through `out_error` when the caller asked for it.
///
/// # Safety
///
/// `out_error` must be null or valid for writes.
unsafe fn deliver_error(out_error: *mut *mut MecoError, error: MecoError) -> i32 {
    if !out_error.is_null() {
        // SAFETY: checked non-null; the header requires a writable pointer.
        unsafe { out_error.write(Box::into_raw(Box::new(error))) };
    }
    MECO_STATUS_ERROR
}

/// Clears both out parameters so failed calls leave deterministic nulls.
///
/// # Safety
///
/// Each pointer must be null or valid for writes.
unsafe fn clear_outputs<T>(out_value: *mut *mut T, out_error: *mut *mut MecoError) {
    if !out_value.is_null() {
        // SAFETY: checked non-null; the header requires a writable pointer.
        unsafe { out_value.write(ptr::null_mut()) };
    }
    if !out_error.is_null() {
        // SAFETY: checked non-null; the header requires a writable pointer.
        unsafe { out_error.write(ptr::null_mut()) };
    }
}

/// Reads a required NUL-terminated UTF-8 argument.
///
/// # Safety
///
/// `pointer` must be null (rejected) or a valid NUL-terminated string.
unsafe fn required_str<'a>(pointer: *const c_char) -> Result<&'a str, i32> {
    if pointer.is_null() {
        return Err(MECO_STATUS_INVALID_ARGUMENT);
    }
    // SAFETY: checked non-null; the header requires NUL termination.
    unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .map_err(|_| MECO_STATUS_INVALID_ARGUMENT)
}

/// Reads a byte-slice argument; `data` may be null only when `len` is zero.
///
/// # Safety
///
/// `data` must be valid for reads of `len` bytes when non-null.
unsafe fn required_bytes<'a>(data: *const u8, len: usize) -> Result<&'a [u8], i32> {
    if data.is_null() {
        if len == 0 {
            return Ok(&[]);
        }
        return Err(MECO_STATUS_INVALID_ARGUMENT);
    }
    // SAFETY: checked non-null; the header requires `len` readable bytes.
    Ok(unsafe { std::slice::from_raw_parts(data, len) })
}

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn meco_ffi_abi_version() -> u32 {
    FFI_ABI_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn meco_core_api_version() -> u32 {
    mecojoni_core::API_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn meco_bytecode_version() -> MecoStrView {
    MecoStrView {
        data: BYTECODE_VERSION_Z.as_ptr().cast(),
        len: BYTECODE_VERSION_Z.len() - 1,
    }
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_error_code(error: *const MecoError) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { error.as_ref() }.map_or_else(MecoStrView::empty, |error| error.code.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_error_message(error: *const MecoError) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { error.as_ref() }.map_or_else(MecoStrView::empty, |error| error.message.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_error_free(error: *mut MecoError) {
    if !error.is_null() {
        // SAFETY: the header requires ownership of a pointer from this library.
        drop(unsafe { Box::from_raw(error) });
    }
}

// ---------------------------------------------------------------------------
// Package building and compilation
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn meco_package_builder_new() -> *mut MecoPackageBuilder {
    Box::into_raw(Box::new(MecoPackageBuilder {
        modules: Vec::new(),
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_package_builder_add_module(
    builder: *mut MecoPackageBuilder,
    canonical_id: *const c_char,
    display_name: *const c_char,
    source_utf8: *const u8,
    source_len: usize,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        if !out_error.is_null() {
            out_error.write(ptr::null_mut());
        }
        let Some(builder) = builder.as_mut() else {
            return MECO_STATUS_INVALID_ARGUMENT;
        };
        let canonical_id = match required_str(canonical_id) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let display_name = match required_str(display_name) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let bytes = match required_bytes(source_utf8, source_len) {
            Ok(value) => value,
            Err(status) => return status,
        };
        // Package compilation reassigns source IDs deterministically; the
        // index only needs to be unique inside this builder.
        let next_id = u32::try_from(builder.modules.len()).unwrap_or(u32::MAX);
        match SourceFile::from_utf8(SourceId::new(next_id), display_name, bytes) {
            Ok(source) => {
                builder.modules.push(PackageSource {
                    canonical_id: canonical_id.to_owned(),
                    source,
                    resolved_imports: Vec::new(),
                });
                MECO_STATUS_OK
            }
            Err(error) => deliver_error(
                out_error,
                MecoError::new(
                    "E_SOURCE_UTF8",
                    &format!(
                        "module `{canonical_id}` is not valid UTF-8 at byte {}",
                        error.valid_up_to()
                    ),
                ),
            ),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_package_builder_resolve_import(
    builder: *mut MecoPackageBuilder,
    module_canonical_id: *const c_char,
    authored_path: *const c_char,
    target_canonical_id: *const c_char,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        if !out_error.is_null() {
            out_error.write(ptr::null_mut());
        }
        let Some(builder) = builder.as_mut() else {
            return MECO_STATUS_INVALID_ARGUMENT;
        };
        let module_id = match required_str(module_canonical_id) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let authored_path = match required_str(authored_path) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let target_id = match required_str(target_canonical_id) {
            Ok(value) => value,
            Err(status) => return status,
        };
        let Some(module) = builder
            .modules
            .iter_mut()
            .find(|module| module.canonical_id == module_id)
        else {
            return deliver_error(
                out_error,
                MecoError::new(
                    "E_IMPORT_RESOLUTION",
                    &format!("module `{module_id}` was not added to this package"),
                ),
            );
        };
        module.resolved_imports.push(ResolvedImport {
            authored_path: authored_path.to_owned(),
            target_id: target_id.to_owned(),
        });
        MECO_STATUS_OK
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_package_builder_compile(
    builder: *const MecoPackageBuilder,
    root_canonical_id: *const c_char,
    out_grammar: *mut *mut MecoGrammar,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        clear_outputs(out_grammar, out_error);
        let Some(builder) = builder.as_ref() else {
            return MECO_STATUS_INVALID_ARGUMENT;
        };
        if out_grammar.is_null() {
            return MECO_STATUS_INVALID_ARGUMENT;
        }
        let root_id = if root_canonical_id.is_null() {
            ""
        } else {
            match required_str(root_canonical_id) {
                Ok(value) => value,
                Err(status) => return status,
            }
        };
        let root_id = if root_id.is_empty() {
            // Default to the first added module so trivial single-module
            // packages need no explicit root.
            match builder.modules.first() {
                Some(module) => module.canonical_id.clone(),
                None => {
                    return deliver_error(
                        out_error,
                        MecoError::new("E_PACKAGE_ROOT", "the package has no modules"),
                    );
                }
            }
        } else {
            root_id.to_owned()
        };
        let package = PackageInput {
            root_id,
            modules: builder.modules.clone(),
        };
        let compiled = validate_package_input(&package).and_then(|()| compile_package(&package));
        match compiled {
            Ok(grammar) => {
                out_grammar.write(Box::into_raw(Box::new(MecoGrammar::new(grammar))));
                MECO_STATUS_OK
            }
            Err(error) => deliver_error(out_error, MecoError::from_core(&error)),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_package_builder_free(builder: *mut MecoPackageBuilder) {
    if !builder.is_null() {
        // SAFETY: the header requires ownership of a pointer from this library.
        drop(unsafe { Box::from_raw(builder) });
    }
}

/// Single-module convenience over the package builder.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_compile(
    name: *const c_char,
    source_utf8: *const u8,
    source_len: usize,
    out_grammar: *mut *mut MecoGrammar,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        clear_outputs(out_grammar, out_error);
        let builder = meco_package_builder_new();
        let status = meco_package_builder_add_module(
            builder,
            name,
            name,
            source_utf8,
            source_len,
            out_error,
        );
        let status = if status == MECO_STATUS_OK {
            meco_package_builder_compile(builder, name, out_grammar, out_error)
        } else {
            status
        };
        meco_package_builder_free(builder);
        status
    }
}

// ---------------------------------------------------------------------------
// Artifacts
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_decode(
    bytes: *const u8,
    len: usize,
    out_grammar: *mut *mut MecoGrammar,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        clear_outputs(out_grammar, out_error);
        let bytes = match required_bytes(bytes, len) {
            Ok(value) => value,
            Err(status) => return status,
        };
        if out_grammar.is_null() {
            return MECO_STATUS_INVALID_ARGUMENT;
        }
        match decode_artifact(bytes, ArtifactLimits::standard()) {
            Ok(grammar) => {
                out_grammar.write(Box::into_raw(Box::new(MecoGrammar::new(grammar))));
                MECO_STATUS_OK
            }
            Err(error) => deliver_error(out_error, MecoError::from_core(&error)),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_encode(
    grammar: *const MecoGrammar,
    debug_profile: u32,
    out_artifact: *mut *mut MecoArtifact,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        clear_outputs(out_artifact, out_error);
        let Some(wrapper) = grammar.as_ref() else {
            return MECO_STATUS_INVALID_ARGUMENT;
        };
        if out_artifact.is_null() {
            return MECO_STATUS_INVALID_ARGUMENT;
        }
        let debug_profile = match debug_profile {
            MECO_PROFILE_FULL => ArtifactDebugProfile::Full,
            MECO_PROFILE_MAPPED => ArtifactDebugProfile::Mapped,
            MECO_PROFILE_STRIPPED => ArtifactDebugProfile::Stripped,
            _ => return MECO_STATUS_INVALID_ARGUMENT,
        };
        match encode_artifact(&wrapper.grammar, ArtifactOptions { debug_profile }) {
            Ok(bytes) => {
                out_artifact.write(Box::into_raw(Box::new(MecoArtifact { bytes })));
                MECO_STATUS_OK
            }
            Err(error) => deliver_error(out_error, MecoError::from_core(&error)),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_artifact_data(artifact: *const MecoArtifact) -> *const u8 {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { artifact.as_ref() }.map_or(ptr::null(), |artifact| artifact.bytes.as_ptr())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_artifact_len(artifact: *const MecoArtifact) -> usize {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { artifact.as_ref() }.map_or(0, |artifact| artifact.bytes.len())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_artifact_free(artifact: *mut MecoArtifact) {
    if !artifact.is_null() {
        // SAFETY: the header requires ownership of a pointer from this library.
        drop(unsafe { Box::from_raw(artifact) });
    }
}

// ---------------------------------------------------------------------------
// Grammar introspection
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_entry_count(grammar: *const MecoGrammar) -> usize {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }.map_or(0, |grammar| grammar.entries.len())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_entry(
    grammar: *const MecoGrammar,
    index: usize,
) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }
        .and_then(|grammar| grammar.entries.get(index))
        .map_or_else(MecoStrView::empty, OwnedText::view)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_default_entry(grammar: *const MecoGrammar) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }
        .and_then(|grammar| grammar.default_entry.as_ref())
        .map_or_else(MecoStrView::empty, OwnedText::view)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_rule_count(grammar: *const MecoGrammar) -> usize {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }.map_or(0, |grammar| grammar.grammar.rule_count())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_production_count(grammar: *const MecoGrammar) -> usize {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }.map_or(0, |grammar| grammar.grammar.production_count())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_warning_count(grammar: *const MecoGrammar) -> usize {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }.map_or(0, |grammar| grammar.warnings.len())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_warning_code(
    grammar: *const MecoGrammar,
    index: usize,
) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }
        .and_then(|grammar| grammar.warnings.get(index))
        .map_or_else(MecoStrView::empty, |(code, _)| code.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_warning_message(
    grammar: *const MecoGrammar,
    index: usize,
) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { grammar.as_ref() }
        .and_then(|grammar| grammar.warnings.get(index))
        .map_or_else(MecoStrView::empty, |(_, message)| message.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_free(grammar: *mut MecoGrammar) {
    if !grammar.is_null() {
        // SAFETY: the header requires ownership of a pointer from this library.
        drop(unsafe { Box::from_raw(grammar) });
    }
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

/// Converts one caller binding; forwarded caller contract for the pointers.
unsafe fn convert_binding(binding: &MecoDataBinding) -> Result<DataBinding, MecoError> {
    // SAFETY: forwarded caller contract.
    let name = unsafe { required_str(binding.name) }
        .map_err(|_| MecoError::new("E_INPUT", "a data binding name is null or not UTF-8"))?;
    let text = || {
        // SAFETY: forwarded caller contract.
        unsafe { required_str(binding.text) }.map_err(|_| {
            MecoError::new(
                "E_INPUT",
                &format!("data binding `{name}` has a null or non-UTF-8 text payload"),
            )
        })
    };
    let value = match binding.kind {
        MECO_VALUE_TEXT => Value::Text(text()?.to_owned()),
        MECO_VALUE_ENUM => Value::Enum(text()?.to_owned()),
        MECO_VALUE_BOOLEAN => Value::Boolean(binding.boolean != 0),
        MECO_VALUE_NUMBER => Rational::new(binding.numerator, binding.denominator)
            .map(Value::Number)
            .map_err(|_| {
                MecoError::new(
                    "E_INPUT",
                    &format!(
                        "data binding `{name}` is not a valid rational ({}/{})",
                        binding.numerator, binding.denominator
                    ),
                )
            })?,
        _ => {
            return Err(MecoError::new(
                "E_INPUT",
                &format!("data binding `{name}` has unknown kind {}", binding.kind),
            ));
        }
    };
    Ok(DataBinding::new(name.to_owned(), value))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_grammar_generate(
    grammar: *const MecoGrammar,
    options: *const MecoGenerateOptions,
    out_generated: *mut *mut MecoGenerated,
    out_error: *mut *mut MecoError,
) -> i32 {
    // SAFETY: forwarded caller contract for every pointer.
    unsafe {
        clear_outputs(out_generated, out_error);
        let (Some(wrapper), Some(options)) = (grammar.as_ref(), options.as_ref()) else {
            return MECO_STATUS_INVALID_ARGUMENT;
        };
        if out_generated.is_null() {
            return MECO_STATUS_INVALID_ARGUMENT;
        }

        let entry = if options.entry.is_null() {
            None
        } else {
            match required_str(options.entry) {
                Ok("") => None,
                Ok(name) => Some(name),
                Err(status) => return status,
            }
        };

        if options.data.is_null() && options.data_count != 0 {
            return MECO_STATUS_INVALID_ARGUMENT;
        }
        let raw_bindings = if options.data_count == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(options.data, options.data_count)
        };
        let mut data = Vec::with_capacity(raw_bindings.len());
        for binding in raw_bindings {
            match convert_binding(binding) {
                Ok(binding) => data.push(binding),
                Err(error) => return deliver_error(out_error, error),
            }
        }

        let defaults = GenerationLimits::INTERACTIVE_WEIGHTED;
        let pick = |value: u32, fallback: u32| if value == 0 { fallback } else { value };
        let request = GenerationRequest {
            entry,
            seed: options.seed,
            limits: GenerationLimits {
                max_depth: pick(options.max_depth, defaults.max_depth),
                max_expansions: pick(options.max_expansions, defaults.max_expansions),
                max_output_scalars: pick(options.max_output_scalars, defaults.max_output_scalars),
                max_output_bytes: pick(options.max_output_bytes, defaults.max_output_bytes),
                max_sampler_words: pick(options.max_sampler_words, defaults.max_sampler_words),
            },
            data: &data,
            trace_bindings: false,
            trace_selections: false,
            trace_provenance: false,
        };

        match wrapper.grammar.generate_weighted(&request) {
            Ok(result) => {
                let generated = MecoGenerated {
                    text: OwnedText::new(result.text()),
                    entry: OwnedText::new(result.entry()),
                    expansions: result.expansions(),
                    sampler_words: result.sampler_words(),
                };
                out_generated.write(Box::into_raw(Box::new(generated)));
                MECO_STATUS_OK
            }
            Err(error) => deliver_error(out_error, MecoError::from_core(&error)),
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_generated_text(generated: *const MecoGenerated) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { generated.as_ref() }.map_or_else(MecoStrView::empty, |generated| generated.text.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_generated_entry(generated: *const MecoGenerated) -> MecoStrView {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { generated.as_ref() }
        .map_or_else(MecoStrView::empty, |generated| generated.entry.view())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_generated_expansions(generated: *const MecoGenerated) -> u32 {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { generated.as_ref() }.map_or(0, |generated| generated.expansions)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_generated_sampler_words(generated: *const MecoGenerated) -> u32 {
    // SAFETY: the header requires a live pointer from this library or null.
    unsafe { generated.as_ref() }.map_or(0, |generated| generated.sampler_words)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn meco_generated_free(generated: *mut MecoGenerated) {
    if !generated.is_null() {
        // SAFETY: the header requires ownership of a pointer from this library.
        drop(unsafe { Box::from_raw(generated) });
    }
}

#[cfg(test)]
mod tests;
