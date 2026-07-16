use std::{fs, io, path::PathBuf};

use mecojoni_core::{
    MessageArgument, MessageDefinition, MessageManifest, PackageInput, PackageSource,
    ResolvedImport, SchemaType, SourceFile, SourceId,
};

pub const STARTUP_PROFILE_VERSION: &str = "startup/1";

/// One manually authored application-shaped package used for startup evidence.
pub struct StartupPackage {
    pub name: &'static str,
    pub input: PackageInput,
    pub manifest: MessageManifest,
    pub source_bytes: usize,
    pub manifest_bytes: usize,
}

/// Loads the checked-in Harbor package through the filesystem.
///
/// # Errors
///
/// Returns the underlying filesystem error if any committed source is absent.
pub fn harbor_startup_package() -> io::Result<StartupPackage> {
    let directory =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../benchmarks/packages/harbor");
    let root = fs::read_to_string(directory.join("root.meco"))?;
    let cast = fs::read_to_string(directory.join("cast.meco"))?;
    let scenes = fs::read_to_string(directory.join("scenes.meco"))?;
    let manifest_source = fs::read_to_string(directory.join("messages.manifest"))?;
    let source_bytes = root.len() + cast.len() + scenes.len();
    let manifest_bytes = manifest_source.len();
    if manifest_source != "harbor-welcome|visitor:text\n" {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Harbor message manifest drifted from startup/1",
        ));
    }
    Ok(StartupPackage {
        name: "harbor-dialogue",
        input: PackageInput {
            root_id: "harbor".to_string(),
            modules: vec![
                PackageSource {
                    canonical_id: "harbor".to_string(),
                    source: SourceFile::new(SourceId::new(0), "root.meco", root),
                    resolved_imports: vec![
                        ResolvedImport {
                            authored_path: "./cast.meco".to_string(),
                            target_id: "cast".to_string(),
                        },
                        ResolvedImport {
                            authored_path: "./scenes.meco".to_string(),
                            target_id: "scenes".to_string(),
                        },
                    ],
                },
                PackageSource {
                    canonical_id: "cast".to_string(),
                    source: SourceFile::new(SourceId::new(1), "cast.meco", cast),
                    resolved_imports: vec![],
                },
                PackageSource {
                    canonical_id: "scenes".to_string(),
                    source: SourceFile::new(SourceId::new(2), "scenes.meco", scenes),
                    resolved_imports: vec![],
                },
            ],
        },
        manifest: MessageManifest {
            messages: vec![MessageDefinition {
                id: "harbor-welcome".to_string(),
                arguments: vec![MessageArgument {
                    name: "visitor".to_string(),
                    type_: SchemaType::Text,
                }],
            }],
        },
        source_bytes,
        manifest_bytes,
    })
}
