use tauri::{AppHandle, State};

use crate::sidecar::{
    self, GeyserOption, PreviewRequestPayload, SearchAnalysisPayload, SearchCatalogPayload,
    SearchRequestPayload, WorldOption,
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
    request: PreviewRequestPayload,
) -> Result<serde_json::Value, String> {
    let app_handle = app.clone();
    tauri::async_runtime::spawn_blocking(move || sidecar::load_preview(Some(&app_handle), &request))
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
pub async fn get_search_catalog(app: AppHandle) -> Result<SearchCatalogPayload, String> {
    let app_handle = app.clone();
    tauri::async_runtime::spawn_blocking(move || sidecar::get_search_catalog(Some(&app_handle)))
        .await
        .map_err(|error| error.to_string())?
        .map_err(|error| error.to_string())
}

#[tauri::command]
pub async fn analyze_search_request(
    app: AppHandle,
    request: SearchRequestPayload,
) -> Result<SearchAnalysisPayload, String> {
    let app_handle = app.clone();
    tauri::async_runtime::spawn_blocking(move || {
        sidecar::analyze_search_request(Some(&app_handle), &request)
    })
    .await
    .map_err(|error| error.to_string())?
    .map_err(|error| error.to_string())
}
