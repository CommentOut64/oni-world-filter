use std::path::{Path, PathBuf};

use serde::Serialize;
use tauri::{AppHandle, Manager};

use crate::error::HostError;

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct HostPathsPayload {
    pub install_resource_dir: String,
    pub app_data_dir: String,
    pub app_local_data_dir: String,
    pub app_log_dir: String,
    pub runtime_sidecar_dir: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ResolvedAppPaths {
    install_resource_dir: PathBuf,
    app_data_dir: PathBuf,
    app_local_data_dir: PathBuf,
    app_log_dir: PathBuf,
    runtime_sidecar_dir: PathBuf,
}

fn build_resolved_app_paths(
    install_resource_dir: PathBuf,
    app_data_dir: PathBuf,
    app_local_data_dir: PathBuf,
    app_log_dir: PathBuf,
) -> ResolvedAppPaths {
    ResolvedAppPaths {
        runtime_sidecar_dir: app_local_data_dir.join("sidecars"),
        install_resource_dir,
        app_data_dir,
        app_local_data_dir,
        app_log_dir,
    }
}

fn resolve_portable_root_dir(install_resource_dir: &Path) -> Result<PathBuf, HostError> {
    if install_resource_dir.join("portable.flag").is_file() {
        return Ok(install_resource_dir.to_path_buf());
    }

    if let Some(parent) = install_resource_dir.parent() {
        if parent.join("portable.flag").is_file() {
            return Ok(parent.to_path_buf());
        }
        if install_resource_dir.file_name() == Some(std::ffi::OsStr::new("resources")) {
            return Ok(parent.to_path_buf());
        }
    }

    Ok(install_resource_dir.to_path_buf())
}

fn build_portable_app_paths(install_resource_dir: PathBuf) -> Result<ResolvedAppPaths, HostError> {
    let portable_root = resolve_portable_root_dir(&install_resource_dir)?;
    let data_root = portable_root.join("data");
    let app_data_dir = data_root.join("app-data");
    let app_log_dir = data_root.join("logs");
    Ok(ResolvedAppPaths {
        install_resource_dir,
        app_data_dir: app_data_dir.clone(),
        app_local_data_dir: app_data_dir.clone(),
        app_log_dir,
        runtime_sidecar_dir: data_root.join("sidecars"),
    })
}

fn build_portable_webview_data_dir(install_resource_dir: PathBuf) -> Result<PathBuf, HostError> {
    Ok(resolve_portable_root_dir(&install_resource_dir)?
        .join("data")
        .join("webview"))
}

fn is_portable_install_resource_dir(install_resource_dir: &Path) -> bool {
    install_resource_dir.join("portable.flag").is_file()
        || install_resource_dir
            .parent()
            .map(|parent| parent.join("portable.flag").is_file())
            .unwrap_or(false)
}

fn to_invalid_request_path_error(prefix: &str, error: impl std::fmt::Display) -> HostError {
    HostError::InvalidRequest(format!("{prefix}: {error}"))
}

fn resolve_app_paths(app: &AppHandle) -> Result<ResolvedAppPaths, HostError> {
    let install_resource_dir = app
        .path()
        .resource_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析安装资源目录", error))?;
    if is_portable_install_resource_dir(&install_resource_dir) {
        return build_portable_app_paths(install_resource_dir);
    }
    let app_data_dir = app
        .path()
        .app_data_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析 AppData 目录", error))?;
    let app_local_data_dir = app
        .path()
        .app_local_data_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析 LocalAppData 目录", error))?;
    let app_log_dir = app
        .path()
        .app_log_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析日志目录", error))?;

    Ok(build_resolved_app_paths(
        install_resource_dir,
        app_data_dir,
        app_local_data_dir,
        app_log_dir,
    ))
}

pub fn resolve_install_resource_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    Ok(resolve_app_paths(app)?.install_resource_dir)
}

pub fn resolve_app_data_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    Ok(resolve_app_paths(app)?.app_data_dir)
}

pub fn resolve_app_local_data_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    Ok(resolve_app_paths(app)?.app_local_data_dir)
}

pub fn resolve_app_log_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    Ok(resolve_app_paths(app)?.app_log_dir)
}

pub fn resolve_runtime_sidecar_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    Ok(resolve_app_paths(app)?.runtime_sidecar_dir)
}

pub fn resolve_webview_data_dir(app: &AppHandle) -> Result<Option<PathBuf>, HostError> {
    let install_resource_dir = app
        .path()
        .resource_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析安装资源目录", error))?;
    if is_portable_install_resource_dir(&install_resource_dir) {
        return Ok(Some(build_portable_webview_data_dir(install_resource_dir)?));
    }
    Ok(None)
}

pub fn collect_host_paths(app: &AppHandle) -> Result<HostPathsPayload, HostError> {
    Ok(HostPathsPayload {
        install_resource_dir: resolve_install_resource_dir(app)?.display().to_string(),
        app_data_dir: resolve_app_data_dir(app)?.display().to_string(),
        app_local_data_dir: resolve_app_local_data_dir(app)?.display().to_string(),
        app_log_dir: resolve_app_log_dir(app)?.display().to_string(),
        runtime_sidecar_dir: resolve_runtime_sidecar_dir(app)?.display().to_string(),
    })
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    use super::{
        build_portable_app_paths, build_portable_webview_data_dir, build_resolved_app_paths,
        is_portable_install_resource_dir,
    };

    fn create_temp_root(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time should be valid")
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "oni-app-paths-tests-{}-{}-{}",
            name,
            std::process::id(),
            nonce
        ));
        fs::create_dir_all(&root).expect("temp root should be created");
        root
    }

    #[test]
    fn app_paths_should_keep_install_and_user_roots_separate() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\oni-world-filter\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter\logs"),
        );

        assert_eq!(
            paths.install_resource_dir,
            PathBuf::from(r"C:\Program Files\oni-world-filter\resources")
        );
        assert_eq!(
            paths.app_data_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.oni-world-filter")
        );
        assert_eq!(
            paths.app_local_data_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter")
        );
        assert_ne!(paths.install_resource_dir, paths.app_data_dir);
        assert_ne!(paths.install_resource_dir, paths.app_local_data_dir);
    }

    #[test]
    fn app_paths_should_place_runtime_sidecar_under_local_app_data() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\oni-world-filter\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter\logs"),
        );

        assert_eq!(
            paths.runtime_sidecar_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter\sidecars")
        );
        assert!(paths.runtime_sidecar_dir.starts_with(&paths.app_local_data_dir));
    }

    #[test]
    fn app_paths_should_keep_log_dir_outside_install_root() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\oni-world-filter\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter\logs"),
        );

        assert_eq!(
            paths.app_log_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.oni-world-filter\logs")
        );
        assert!(!paths.app_log_dir.starts_with(&paths.install_resource_dir));
    }

    #[test]
    fn app_paths_should_place_portable_runtime_state_under_exe_sibling_data_dir() {
        let paths = build_portable_app_paths(PathBuf::from(
            r"F:\portable\oni-world-filter\resources",
        ))
        .expect("portable paths should resolve from resource dir");

        assert_eq!(
            paths.install_resource_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\resources")
        );
        assert_eq!(
            paths.app_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\app-data")
        );
        assert_eq!(
            paths.app_local_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\app-data")
        );
        assert_eq!(
            paths.app_log_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\logs")
        );
        assert_eq!(
            paths.runtime_sidecar_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\sidecars")
        );
    }

    #[test]
    fn app_paths_should_detect_portable_when_resource_dir_is_bundle_root() {
        let root = create_temp_root("portable-root-detect");
        fs::write(root.join("portable.flag"), b"portable").expect("portable flag should be written");

        assert!(
            is_portable_install_resource_dir(&root),
            "portable bundle root should be detected when resource_dir already points at package root"
        );

        fs::remove_dir_all(root).expect("temp root should be removed");
    }

    #[test]
    fn app_paths_should_place_portable_runtime_state_under_bundle_root_data_dir() {
        let paths =
            build_portable_app_paths(PathBuf::from(r"F:\portable\oni-world-filter"))
                .expect("portable paths should resolve from bundle root");

        assert_eq!(
            paths.install_resource_dir,
            PathBuf::from(r"F:\portable\oni-world-filter")
        );
        assert_eq!(
            paths.app_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\app-data")
        );
        assert_eq!(
            paths.app_local_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\app-data")
        );
        assert_eq!(
            paths.app_log_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\logs")
        );
        assert_eq!(
            paths.runtime_sidecar_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\sidecars")
        );
    }

    #[test]
    fn app_paths_should_place_portable_webview_data_under_exe_sibling_data_dir() {
        let webview_data_dir =
            build_portable_webview_data_dir(PathBuf::from(r"F:\portable\oni-world-filter\resources"))
                .expect("portable webview data dir should resolve from resource dir");

        assert_eq!(
            webview_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\webview")
        );
    }

    #[test]
    fn app_paths_should_place_portable_webview_data_under_bundle_root_data_dir() {
        let webview_data_dir =
            build_portable_webview_data_dir(PathBuf::from(r"F:\portable\oni-world-filter"))
                .expect("portable webview data dir should resolve from bundle root");

        assert_eq!(
            webview_data_dir,
            PathBuf::from(r"F:\portable\oni-world-filter\data\webview")
        );
    }
}
