const DEVELOPMENT_PLACEHOLDER: &[u8] = b"development-placeholder";

fn main() {
    ensure_sidecar_resource();
    tauri_build::build()
}

fn is_real_sidecar(bytes: &[u8]) -> bool {
    !bytes.is_empty() && !bytes.starts_with(DEVELOPMENT_PLACEHOLDER)
}

fn ensure_sidecar_resource() {
    let manifest_dir = std::path::PathBuf::from(
        std::env::var("CARGO_MANIFEST_DIR").unwrap_or_else(|_| ".".to_string()),
    );
    let binaries_dir = manifest_dir.join("binaries");
    let sidecar_path = binaries_dir.join("oni-sidecar.exe");
    let sidecar_required = std::env::var("ONI_REQUIRE_SIDECAR")
        .map(|value| value != "0")
        .unwrap_or(false);

    println!("cargo:rerun-if-env-changed=ONI_REQUIRE_SIDECAR");
    println!("cargo:rerun-if-changed={}", sidecar_path.display());

    let _ = std::fs::create_dir_all(&binaries_dir);
    let file_bytes = std::fs::read(&sidecar_path).ok();
    let has_real_sidecar = file_bytes
        .as_ref()
        .map(|bytes| is_real_sidecar(bytes))
        .unwrap_or(false);

    if sidecar_required && !has_real_sidecar {
        panic!(
            "required sidecar resource is missing or empty: {}",
            sidecar_path.display()
        );
    }

    if !sidecar_required && !has_real_sidecar {
        let _ = std::fs::write(&sidecar_path, DEVELOPMENT_PLACEHOLDER);
    }
}
