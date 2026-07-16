use mecojoni_benchmarks::{STARTUP_PROFILE_VERSION, harbor_startup_package};
use mecojoni_core::{
    DataBinding, GenerationRequest, Rational, Value, compile_package_with_manifest,
};

#[test]
fn filesystem_startup_package_compiles_and_generates_an_exported_ordinary_entry() {
    assert_eq!(STARTUP_PROFILE_VERSION, "startup/1");
    let package = harbor_startup_package().expect("load Harbor startup package");
    let grammar = compile_package_with_manifest(&package.input, &package.manifest)
        .expect("compile Harbor startup package");
    let data = [
        DataBinding::new("visitor".to_string(), Value::Text("Rin".to_string())),
        DataBinding::new("mood".to_string(), Value::Enum("tense".to_string())),
        DataBinding::new("urgency".to_string(), Value::Number(Rational::ONE)),
    ];
    let generated = grammar
        .generate_weighted(&GenerationRequest {
            entry: Some("harbor.scene"),
            data: &data,
            ..GenerationRequest::with_seed(0)
        })
        .expect("generate Harbor scene");

    assert_eq!(grammar.rule_count(), 11);
    assert_eq!(grammar.production_count(), 35);
    assert!(!generated.text().is_empty());
    assert!(generated.text().contains("Rin") || generated.text().contains("quay"));
}
