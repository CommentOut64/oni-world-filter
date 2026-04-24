use std::path::PathBuf;

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

fn to_invalid_request_path_error(prefix: &str, error: impl std::fmt::Display) -> HostError {
    HostError::InvalidRequest(format!("{prefix}: {error}"))
}

fn resolve_app_paths(app: &AppHandle) -> Result<ResolvedAppPaths, HostError> {
    let install_resource_dir = app
        .path()
        .resource_dir()
        .map_err(|error| to_invalid_request_path_error("无法解析安装资源目录", error))?;
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
    use std::path::PathBuf;

    use super::build_resolved_app_paths;

    #[test]
    fn app_paths_should_keep_install_and_user_roots_separate() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\ONI World Desktop\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop\logs"),
        );

        assert_eq!(
            paths.install_resource_dir,
            PathBuf::from(r"C:\Program Files\ONI World Desktop\resources")
        );
        assert_eq!(
            paths.app_data_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.wgh.oniworld.desktop")
        );
        assert_eq!(
            paths.app_local_data_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop")
        );
        assert_ne!(paths.install_resource_dir, paths.app_data_dir);
        assert_ne!(paths.install_resource_dir, paths.app_local_data_dir);
    }

    #[test]
    fn app_paths_should_place_runtime_sidecar_under_local_app_data() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\ONI World Desktop\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop\logs"),
        );

        assert_eq!(
            paths.runtime_sidecar_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop\sidecars")
        );
        assert!(paths.runtime_sidecar_dir.starts_with(&paths.app_local_data_dir));
    }

    #[test]
    fn app_paths_should_keep_log_dir_outside_install_root() {
        let paths = build_resolved_app_paths(
            PathBuf::from(r"C:\Program Files\ONI World Desktop\resources"),
            PathBuf::from(r"C:\Users\wgh\AppData\Roaming\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop"),
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop\logs"),
        );

        assert_eq!(
            paths.app_log_dir,
            PathBuf::from(r"C:\Users\wgh\AppData\Local\com.wgh.oniworld.desktop\logs")
        );
        assert!(!paths.app_log_dir.starts_with(&paths.install_resource_dir));
    }
}
