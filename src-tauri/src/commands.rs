use tauri::{AppHandle, State};

use crate::app_paths::{self, HostPathsPayload};
use crate::control_sidecar;
use crate::sidecar::{
    self, CoordPreviewRequestPayload, GeyserOption, PreviewRequestPayload,
    SearchAnalysisPayload, SearchCatalogPayload, SearchRequestPayload, WorldOption,
};
use crate::state::AppState;

#[tauri::command]
pub async fn start_search(
    app: AppHandle,
    state: State<'_, AppState>,
    request: SearchRequestPayload,
) -> Result<(), String> {
    sidecar::start_search_streaming(app, state.jobs.clone(), request)
        .map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn cancel_search(
    app: AppHandle,
    state: State<'_, AppState>,
    job_id: String,
) -> Result<(), String> {
    sidecar::cancel_search(&app, state.jobs.clone(), &job_id).map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn load_preview(
    app: AppHandle,
    state: State<'_, AppState>,
    request: PreviewRequestPayload,
) -> Result<serde_json::Value, String> {
    let app_handle = app.clone();
    let control_sidecar = state.control_sidecar.clone();
    tauri::async_runtime::spawn_blocking(move || {
        control_sidecar::load_preview(Some(&app_handle), &control_sidecar, &request)
    })
    .await
    .map_err(|error| error.to_string())?
    .map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn load_preview_by_coord(
    app: AppHandle,
    state: State<'_, AppState>,
    request: CoordPreviewRequestPayload,
) -> Result<serde_json::Value, String> {
    let app_handle = app.clone();
    let control_sidecar = state.control_sidecar.clone();
    tauri::async_runtime::spawn_blocking(move || {
        control_sidecar::load_preview_by_coord(Some(&app_handle), &control_sidecar, &request)
    })
    .await
    .map_err(|error| error.to_string())?
    .map_err(|error| error.to_string())
}

#[tauri::command]
pub fn list_worlds() -> Vec<WorldOption> {
    sidecar::list_world_options()
}

#[tauri::command]
pub fn list_geysers() -> Vec<GeyserOption> {
    sidecar::list_geyser_options()
}

#[tauri::command]
pub fn get_host_paths(app: AppHandle) -> Result<HostPathsPayload, String> {
    app_paths::collect_host_paths(&app).map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn get_search_catalog(
    app: AppHandle,
    state: State<'_, AppState>,
) -> Result<SearchCatalogPayload, String> {
    let app_handle = app.clone();
    let control_sidecar = state.control_sidecar.clone();
    tauri::async_runtime::spawn_blocking(move || {
        control_sidecar::get_search_catalog(Some(&app_handle), &control_sidecar)
    })
    .await
    .map_err(|error| error.to_string())?
    .map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn analyze_search_request(
    app: AppHandle,
    state: State<'_, AppState>,
    request: SearchRequestPayload,
) -> Result<SearchAnalysisPayload, String> {
    let app_handle = app.clone();
    let control_sidecar = state.control_sidecar.clone();
    tauri::async_runtime::spawn_blocking(move || {
        control_sidecar::analyze_search_request(Some(&app_handle), &control_sidecar, &request)
    })
    .await
    .map_err(|error| error.to_string())?
    .map_err(|error| error.to_string())
}
