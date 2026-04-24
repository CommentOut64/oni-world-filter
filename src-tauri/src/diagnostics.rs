use std::fs::{self, OpenOptions};
use std::io::Write;
use std::time::{SystemTime, UNIX_EPOCH};

use tauri::AppHandle;

use crate::app_paths;

pub fn log(app: Option<&AppHandle>, scope: &str, message: impl AsRef<str>) {
    let Some(app) = app else {
        eprintln!("[diagnostic:{scope}] {}", message.as_ref());
        return;
    };
    let Ok(log_dir) = app_paths::resolve_app_log_dir(app) else {
        eprintln!("[diagnostic:{scope}] {}", message.as_ref());
        return;
    };
    if fs::create_dir_all(&log_dir).is_err() {
        eprintln!("[diagnostic:{scope}] {}", message.as_ref());
        return;
    }
    let log_path = log_dir.join("oni-world-filter.log");
    let timestamp_ms = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or_default();
    let line = format!("{timestamp_ms} [{scope}] {}\n", message.as_ref());
    if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(log_path) {
        let _ = file.write_all(line.as_bytes());
    }
}
