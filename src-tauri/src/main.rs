#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod app_paths;
mod commands;
mod control_sidecar;
mod diagnostics;
mod error;
mod sidecar;
mod state;

use state::AppState;
use tauri::{Manager, RunEvent};

fn shutdown_runtime_sidecars(app: &tauri::AppHandle) {
    let state = app.state::<AppState>();
    sidecar::abort_all_running_jobs_for_shutdown(&state.jobs);
    state.control_sidecar.reset();
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            commands::start_search,
            commands::cancel_search,
            commands::load_preview,
            commands::load_preview_by_coord,
            commands::list_worlds,
            commands::list_geysers,
            commands::get_host_paths,
            commands::get_search_catalog,
            commands::analyze_search_request
        ])
        .build(tauri::generate_context!())
        .expect("failed to build tauri application")
        .run(|app, event| {
            if matches!(event, RunEvent::ExitRequested { .. } | RunEvent::Exit) {
                shutdown_runtime_sidecars(app);
            }
        });
}

#[cfg(test)]
mod tests {
    use serde_json::Value;

    fn default_capability_permissions() -> Vec<String> {
        let capability: Value = serde_json::from_str(include_str!("../capabilities/default.json"))
            .expect("default capability should be valid json");
        capability["permissions"]
            .as_array()
            .expect("default capability should declare permissions")
            .iter()
            .map(|entry| {
                entry
                    .as_str()
                    .expect("permission entry should be a string")
                    .to_string()
            })
            .collect()
    }

    #[test]
    fn default_capability_allows_host_debug_window_lifecycle() {
        let permissions = default_capability_permissions();

        assert!(
            permissions.contains(&"core:webview:allow-create-webview-window".to_string()),
            "default capability must allow creating the host debug webview window"
        );
        assert!(
            permissions.contains(&"core:window:allow-show".to_string()),
            "default capability must allow showing an existing host debug window"
        );
        assert!(
            permissions.contains(&"core:window:allow-set-focus".to_string()),
            "default capability must allow focusing an existing host debug window"
        );
    }
}
