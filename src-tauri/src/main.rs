#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod error;
mod sidecar;
mod state;

use state::AppState;

fn main() {
    tauri::Builder::default()
        .manage(AppState::default())
        .invoke_handler(tauri::generate_handler![
            commands::start_search,
            commands::cancel_search,
            commands::load_preview,
            commands::list_worlds,
            commands::list_geysers,
            commands::get_search_catalog
        ])
        .run(tauri::generate_context!())
        .expect("failed to run tauri application");
}
