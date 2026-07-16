// SPDX-License-Identifier: MPL-2.0
use std::{fs, path::PathBuf};

use mecojoni_core::{
    ArtifactDebugProfile, ArtifactLimits, ArtifactOptions, DataBinding, DiverseGenerationRequest,
    GenerationRequest, LocaleRequest, MessageArgument, MessageDefinition, MessageManifest,
    PackageInput, PackageSource, Rational, RepetitionStore, ResolvedImport, SamplerSession,
    SchemaType, SourceFile, SourceId, Value, compile_package_with_manifest, decode_artifact,
    encode_artifact, inspect_artifact,
};

fn harbor_package() -> (PackageInput, MessageManifest) {
    let directory =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../benchmarks/packages/harbor");
    let module = |id: &str, source_id: u32, name: &str| PackageSource {
        canonical_id: id.to_string(),
        source: SourceFile::new(
            SourceId::new(source_id),
            name,
            fs::read_to_string(directory.join(name)).expect("read Harbor module"),
        ),
        resolved_imports: vec![],
    };
    let mut root = module("harbor", 0, "root.meco");
    root.resolved_imports = vec![
        ResolvedImport {
            authored_path: "./cast.meco".to_string(),
            target_id: "cast".to_string(),
        },
        ResolvedImport {
            authored_path: "./scenes.meco".to_string(),
            target_id: "scenes".to_string(),
        },
    ];
    (
        PackageInput {
            root_id: "harbor".to_string(),
            modules: vec![
                root,
                module("cast", 1, "cast.meco"),
                module("scenes", 2, "scenes.meco"),
            ],
        },
        MessageManifest {
            messages: vec![MessageDefinition {
                id: "harbor-welcome".to_string(),
                arguments: vec![MessageArgument {
                    name: "visitor".to_string(),
                    type_: SchemaType::Text,
                }],
            }],
        },
    )
}

fn milestone8_package() -> (PackageInput, MessageManifest) {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("tests/fixtures/packages/milestone8/root.meco");
    (
        PackageInput {
            root_id: "root".to_string(),
            modules: vec![PackageSource {
                canonical_id: "root".to_string(),
                source: SourceFile::new(
                    SourceId::new(0),
                    "root.meco",
                    fs::read_to_string(path).expect("read Milestone 8 fixture"),
                ),
                resolved_imports: vec![],
            }],
        },
        MessageManifest {
            messages: vec![MessageDefinition {
                id: "arrival".to_string(),
                arguments: vec![MessageArgument {
                    name: "name".to_string(),
                    type_: SchemaType::Text,
                }],
            }],
        },
    )
}

#[test]
fn every_profile_preserves_typed_dynamic_message_and_trace_semantics() {
    let (package, manifest) = harbor_package();
    let source = compile_package_with_manifest(&package, &manifest).expect("compile Harbor");
    let data = [
        DataBinding::new("visitor".to_string(), Value::Text("Rin".to_string())),
        DataBinding::new("mood".to_string(), Value::Enum("tense".to_string())),
        DataBinding::new("urgency".to_string(), Value::Number(Rational::ONE)),
    ];
    for profile in [
        ArtifactDebugProfile::Full,
        ArtifactDebugProfile::Mapped,
        ArtifactDebugProfile::Stripped,
    ] {
        let bytes = encode_artifact(
            &source,
            ArtifactOptions {
                debug_profile: profile,
            },
        )
        .expect("encode Harbor");
        let artifact = decode_artifact(&bytes, ArtifactLimits::default()).expect("decode Harbor");
        assert_eq!(source.manifest(), artifact.manifest());
        assert_eq!(source.warnings(), artifact.warnings());
        assert_eq!(source.audit_composition(), artifact.audit_composition());
        for entry in ["harbor.line", "harbor.scene"] {
            for seed in 0..32 {
                let request = GenerationRequest {
                    entry: Some(entry),
                    seed,
                    data: &data,
                    trace_bindings: true,
                    trace_selections: true,
                    trace_provenance: true,
                    ..GenerationRequest::with_seed(seed)
                };
                assert_eq!(
                    source.generate_weighted(&request).expect("source generate"),
                    artifact
                        .generate_weighted(&request)
                        .expect("artifact generate")
                );
            }
        }
        let message_request = GenerationRequest {
            entry: Some("harbor.welcome"),
            data: &data,
            trace_provenance: true,
            ..GenerationRequest::with_seed(3)
        };
        assert_eq!(
            source
                .generate_weighted_structural(
                    &message_request,
                    Some(LocaleRequest {
                        requested: "en",
                        fallbacks: &[],
                    }),
                )
                .expect("source message"),
            artifact
                .generate_weighted_structural(
                    &message_request,
                    Some(LocaleRequest {
                        requested: "en",
                        fallbacks: &[],
                    }),
                )
                .expect("artifact message")
        );
        let metadata = inspect_artifact(&bytes, ArtifactLimits::default()).expect("inspect Harbor");
        if profile == ArtifactDebugProfile::Full {
            metadata
                .require_full_debug()
                .expect("full debug is available");
        } else {
            assert!(metadata.require_full_debug().is_err());
        }
    }
}

#[test]
fn artifact_diverse_snapshots_replay_and_provenance_match_source() {
    let (package, manifest) = milestone8_package();
    let source = compile_package_with_manifest(&package, &manifest).expect("compile Milestone 8");
    let bytes = encode_artifact(&source, ArtifactOptions::default()).expect("encode Milestone 8");
    let artifact = decode_artifact(&bytes, ArtifactLimits::default()).expect("decode Milestone 8");
    let data = [DataBinding::new(
        "playerName".to_string(),
        Value::Text("Rin".to_string()),
    )];
    let request = DiverseGenerationRequest {
        data: &data,
        trace_bindings: true,
        trace_selections: true,
        trace_provenance: true,
        ..DiverseGenerationRequest::default()
    };
    let mut source_session = SamplerSession::new(19);
    let mut source_store = RepetitionStore::new_location();
    let first = source_session
        .generate(&source, &mut source_store, &request)
        .expect("source diverse generation");
    let session_snapshot = source_session.snapshot();
    let store_snapshot = source_store.snapshot().expect("snapshot history");
    let expected_next = source_session
        .generate(&source, &mut source_store, &request)
        .expect("source next generation");

    let mut artifact_session = SamplerSession::new(19);
    let mut artifact_store = RepetitionStore::new_location();
    assert_eq!(
        first,
        artifact_session
            .generate(&artifact, &mut artifact_store, &request)
            .expect("artifact diverse generation")
    );
    let mut restored_session = SamplerSession::restore(session_snapshot).expect("restore session");
    let mut restored_store = RepetitionStore::restore(&store_snapshot).expect("restore history");
    assert_eq!(
        expected_next,
        restored_session
            .generate(&artifact, &mut restored_store, &request)
            .expect("artifact continues source snapshot")
    );
}
