fn main() {
    ensure_sidecar_resource_placeholder();
    tauri_build::build()
}

fn ensure_sidecar_resource_placeholder() {
    let manifest_dir = std::path::PathBuf::from(
        std::env::var("CARGO_MANIFEST_DIR").unwrap_or_else(|_| ".".to_string()),
    );
    let binaries_dir = manifest_dir.join("binaries");
    let sidecar_path = binaries_dir.join("oni-sidecar.exe");

    let _ = std::fs::create_dir_all(&binaries_dir);
    if !sidecar_path.exists() {
        let _ = std::fs::write(&sidecar_path, []);
    }
}
