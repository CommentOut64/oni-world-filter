use std::env;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{ChildStderr, ChildStdout, Command, Stdio};
use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};

use crate::error::HostError;
use crate::state::{JobRegistry, JobStatus, RunningJobHandles};

pub const SIDECAR_EVENT_CHANNEL: &str = "sidecar://event";
pub const SIDECAR_STDERR_CHANNEL: &str = "sidecar://stderr";

const WORLD_CODES: [&str; 38] = [
    "SNDST-A-",
    "OCAN-A-",
    "S-FRZ-",
    "LUSH-A-",
    "FRST-A-",
    "VOLCA-",
    "BAD-A-",
    "HTFST-A-",
    "OASIS-A-",
    "CER-A-",
    "CERS-A-",
    "PRE-A-",
    "PRES-A-",
    "V-SNDST-C-",
    "V-OCAN-C-",
    "V-SWMP-C-",
    "V-SFRZ-C-",
    "V-LUSH-C-",
    "V-FRST-C-",
    "V-VOLCA-C-",
    "V-BAD-C-",
    "V-HTFST-C-",
    "V-OASIS-C-",
    "V-CER-C-",
    "V-CERS-C-",
    "V-PRE-C-",
    "V-PRES-C-",
    "SNDST-C-",
    "PRE-C-",
    "CER-C-",
    "FRST-C-",
    "SWMP-C-",
    "M-SWMP-C-",
    "M-BAD-C-",
    "M-FRZ-C-",
    "M-FLIP-C-",
    "M-RAD-C-",
    "M-CERS-C-",
];

const GEYSER_IDS: [&str; 32] = [
    "steam",
    "hot_steam",
    "hot_water",
    "slush_water",
    "filthy_water",
    "slush_salt_water",
    "salt_water",
    "small_volcano",
    "big_volcano",
    "liquid_co2",
    "hot_co2",
    "hot_hydrogen",
    "hot_po2",
    "slimy_po2",
    "chlorine_gas",
    "methane",
    "molten_copper",
    "molten_iron",
    "molten_gold",
    "molten_aluminum",
    "molten_cobalt",
    "oil_drip",
    "liquid_sulfur",
    "chlorine_gas_cool",
    "molten_tungsten",
    "molten_niobium",
    "printing_pod",
    "oil_reservoir",
    "warp_sender",
    "warp_receiver",
    "warp_portal",
    "cryo_tank",
];

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchRequestPayload {
    pub job_id: String,
    pub world_type: i32,
    pub seed_start: i32,
    pub seed_end: i32,
    #[serde(default)]
    pub mixing: i32,
    #[serde(default)]
    pub threads: i32,
    #[serde(default)]
    pub constraints: SearchConstraints,
    #[serde(default)]
    pub cpu: Option<SearchCpuConfig>,
}

#[derive(Debug, Clone, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SearchConstraints {
    #[serde(default)]
    pub required: Vec<String>,
    #[serde(default)]
    pub forbidden: Vec<String>,
    #[serde(default)]
    pub distance: Vec<DistanceRule>,
}

#[derive(Debug, Clone, Deserialize, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SearchCpuConfig {
    #[serde(default = "default_cpu_mode")]
    pub mode: String,
    #[serde(default)]
    pub workers: i32,
    #[serde(default = "default_true")]
    pub allow_smt: bool,
    #[serde(default)]
    pub allow_low_perf: bool,
    #[serde(default = "default_placement")]
    pub placement: String,
    #[serde(default = "default_true")]
    pub enable_warmup: bool,
    #[serde(default = "default_true")]
    pub enable_adaptive_down: bool,
    #[serde(default = "default_chunk_size")]
    pub chunk_size: i32,
    #[serde(default = "default_progress_interval")]
    pub progress_interval: i32,
    #[serde(default = "default_sample_window")]
    pub sample_window_ms: i32,
    #[serde(default = "default_adaptive_min_workers")]
    pub adaptive_min_workers: i32,
    #[serde(default = "default_adaptive_drop_threshold")]
    pub adaptive_drop_threshold: f64,
    #[serde(default = "default_adaptive_drop_windows")]
    pub adaptive_drop_windows: i32,
    #[serde(default = "default_adaptive_cooldown")]
    pub adaptive_cooldown_ms: i32,
}

fn default_true() -> bool {
    true
}

fn default_cpu_mode() -> String {
    "balanced".to_string()
}

fn default_placement() -> String {
    "preferred".to_string()
}

fn default_chunk_size() -> i32 {
    64
}

fn default_progress_interval() -> i32 {
    1000
}

fn default_sample_window() -> i32 {
    2000
}

fn default_adaptive_min_workers() -> i32 {
    1
}

fn default_adaptive_drop_threshold() -> f64 {
    0.12
}

fn default_adaptive_drop_windows() -> i32 {
    3
}

fn default_adaptive_cooldown() -> i32 {
    8000
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DistanceRule {
    pub geyser: String,
    pub min_dist: f64,
    pub max_dist: f64,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PreviewRequestPayload {
    pub job_id: String,
    pub world_type: i32,
    pub seed: i32,
    #[serde(default)]
    pub mixing: i32,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WorldOption {
    pub id: i32,
    pub code: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GeyserOption {
    pub id: i32,
    pub key: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TraitMeta {
    pub id: String,
    pub name: String,
    pub description: String,
    pub trait_tags: Vec<String>,
    pub exclusive_with: Vec<String>,
    pub exclusive_with_tags: Vec<String>,
    pub forbidden_dlc_ids: Vec<String>,
    pub effect_summary: Vec<String>,
    pub searchable: bool,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MixingSlotMeta {
    pub slot: i32,
    pub path: String,
    pub r#type: String,
    pub name: String,
    pub description: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ParameterSpec {
    pub id: String,
    pub value_type: String,
    pub meaning: String,
    pub static_range: String,
    pub supports_dynamic_range: bool,
    pub source: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchCatalogPayload {
    pub worlds: Vec<WorldOption>,
    pub geysers: Vec<GeyserOption>,
    pub traits: Vec<TraitMeta>,
    pub mixing_slots: Vec<MixingSlotMeta>,
    pub parameter_specs: Vec<ParameterSpec>,
}

pub fn list_world_options() -> Vec<WorldOption> {
    WORLD_CODES
        .iter()
        .enumerate()
        .map(|(index, code)| WorldOption {
            id: index as i32,
            code: (*code).to_string(),
        })
        .collect()
}

pub fn list_geyser_options() -> Vec<GeyserOption> {
    GEYSER_IDS
        .iter()
        .enumerate()
        .map(|(index, key)| GeyserOption {
            id: index as i32,
            key: (*key).to_string(),
        })
        .collect()
}

pub fn start_search_streaming(
    app: AppHandle,
    registry: Arc<JobRegistry>,
    request: SearchRequestPayload,
) -> Result<(), HostError> {
    if request.job_id.trim().is_empty() {
        return Err(HostError::InvalidRequest("jobId 不能为空".to_string()));
    }
    if request.seed_start > request.seed_end {
        return Err(HostError::InvalidRequest(
            "seedStart 必须 <= seedEnd".to_string(),
        ));
    }
    if request.world_type < 0 || request.world_type >= WORLD_CODES.len() as i32 {
        return Err(HostError::InvalidRequest(
            "worldType 超出有效范围".to_string(),
        ));
    }

    let sidecar_path = resolve_sidecar_path(Some(&app))?;
    let mut child = Command::new(&sidecar_path)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

    let mut stdin = child
        .stdin
        .take()
        .ok_or_else(|| HostError::InvalidRequest("sidecar stdin 不可用".to_string()))?;
    let stdout = child
        .stdout
        .take()
        .ok_or_else(|| HostError::InvalidRequest("sidecar stdout 不可用".to_string()))?;
    let stderr = child
        .stderr
        .take()
        .ok_or_else(|| HostError::InvalidRequest("sidecar stderr 不可用".to_string()))?;

    let payload = build_search_command(&request);
    write_json_line(&mut stdin, &payload)?;

    let child_handle = Arc::new(Mutex::new(Some(child)));
    let stdin_handle = Arc::new(Mutex::new(Some(stdin)));
    let cancel_token = Arc::new(std::sync::atomic::AtomicBool::new(false));

    registry.insert_running(
        request.job_id.clone(),
        RunningJobHandles {
            child_handle: child_handle.clone(),
            stdin_handle: stdin_handle.clone(),
            cancel_token,
        },
    )?;

    spawn_stderr_logger(app.clone(), request.job_id.clone(), stderr);
    spawn_stdout_forwarder(
        app.clone(),
        registry.clone(),
        request.job_id.clone(),
        stdin_handle.clone(),
        stdout,
    );
    spawn_waiter(app, registry, request.job_id, child_handle, stdin_handle);

    Ok(())
}

pub fn cancel_search(
    app: &AppHandle,
    registry: Arc<JobRegistry>,
    job_id: &str,
) -> Result<(), HostError> {
    let handles = registry
        .get_handles(job_id)
        .ok_or_else(|| HostError::JobNotFound(job_id.to_string()))?;
    if !registry.is_running(job_id) {
        return Ok(());
    }

    handles.cancel_token.store(true, Ordering::Relaxed);
    if let Ok(mut guard) = handles.stdin_handle.lock() {
        if let Some(stdin) = guard.as_mut() {
            let cancel = json!({
                "command": "cancel",
                "jobId": job_id
            });
            let _ = write_json_line(stdin, &cancel);
        }
    }

    // 优先等待 sidecar 自身完成取消，超时后再强制终止。
    let mut finished = false;
    for _ in 0..20 {
        thread::sleep(Duration::from_millis(50));
        match registry.get_status(job_id) {
            Some(JobStatus::Cancelled) | Some(JobStatus::Completed) | Some(JobStatus::Failed) => {
                finished = true;
                break;
            }
            _ => {}
        }
    }

    if !finished {
        if let Ok(mut guard) = handles.child_handle.lock() {
            if let Some(child) = guard.as_mut() {
                let _ = child.kill();
            }
        }
    }

    if registry.is_running(job_id) {
        registry.set_status(job_id, JobStatus::Cancelled)?;
        let _ = app.emit(
            SIDECAR_EVENT_CHANNEL,
            json!({
                "event": "cancelled",
                "jobId": job_id,
                "processedSeeds": 0,
                "totalSeeds": 0,
                "totalMatches": 0,
                "finalActiveWorkers": 0
            }),
        );
    }
    Ok(())
}

pub fn load_preview(
    app: Option<&AppHandle>,
    request: &PreviewRequestPayload,
) -> Result<Value, HostError> {
    if request.job_id.trim().is_empty() {
        return Err(HostError::InvalidRequest("jobId 不能为空".to_string()));
    }
    if request.world_type < 0 || request.world_type >= WORLD_CODES.len() as i32 {
        return Err(HostError::InvalidRequest(
            "worldType 超出有效范围".to_string(),
        ));
    }
    let sidecar_path = resolve_sidecar_path(app)?;
    let events = run_sidecar_request_collect(&sidecar_path, &build_preview_command(request))?;

    for event in events {
        if event.get("event").and_then(Value::as_str) == Some("failed") {
            let message = event
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("preview 请求失败");
            return Err(HostError::InvalidRequest(message.to_string()));
        }
        if event.get("event").and_then(Value::as_str) == Some("preview")
            && event.get("jobId").and_then(Value::as_str) == Some(request.job_id.as_str())
        {
            return Ok(event);
        }
    }

    Err(HostError::InvalidRequest("未收到 preview 事件".to_string()))
}

pub fn get_search_catalog(app: Option<&AppHandle>) -> Result<SearchCatalogPayload, HostError> {
    let sidecar_path = resolve_sidecar_path(app)?;
    let job_id = "search-catalog";
    let request = build_get_search_catalog_command(job_id);
    let events = run_sidecar_request_collect(&sidecar_path, &request)?;

    for event in events {
        if event.get("event").and_then(Value::as_str) == Some("failed") {
            let message = event
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("catalog 请求失败");
            return Err(HostError::InvalidRequest(message.to_string()));
        }
        if event.get("event").and_then(Value::as_str) == Some("search_catalog")
            && event.get("jobId").and_then(Value::as_str) == Some(job_id)
        {
            let catalog = event.get("catalog").cloned().ok_or_else(|| {
                HostError::InvalidRequest("search_catalog 缺少 catalog 字段".to_string())
            })?;
            return serde_json::from_value(catalog).map_err(HostError::from);
        }
    }

    Err(HostError::InvalidRequest(
        "未收到 search_catalog 事件".to_string(),
    ))
}

pub fn resolve_sidecar_path(app: Option<&AppHandle>) -> Result<PathBuf, HostError> {
    if let Some(path) = env::var_os("ONI_SIDECAR_PATH") {
        let candidate = PathBuf::from(path);
        if candidate.is_file() {
            return Ok(candidate);
        }
    }

    let mut candidates = Vec::new();
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let repo_root = manifest_dir
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| manifest_dir.clone());

    candidates.push(repo_root.join("out/build/x64-release/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-release/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-debug/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-debug/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-release/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-release/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-debug/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-debug/src/oni-sidecar.exe"));
    candidates.push(manifest_dir.join("binaries/oni-sidecar.exe"));

    if let Some(app_handle) = app {
        if let Ok(resource_dir) = app_handle.path().resource_dir() {
            candidates.push(resource_dir.join("oni-sidecar.exe"));
            candidates.push(resource_dir.join("oni-sidecar-x86_64-pc-windows-msvc.exe"));
            candidates.push(resource_dir.join("oni-sidecar-x86_64-pc-windows-gnu.exe"));
        }
    }

    if let Some(existing) = candidates.iter().find(|path| {
        path.is_file()
            && path
                .metadata()
                .map(|metadata| metadata.len() > 0)
                .unwrap_or(false)
    }) {
        return Ok(existing.to_path_buf());
    }

    Err(HostError::SidecarNotFound { candidates })
}

pub fn run_sidecar_request_collect(
    sidecar_path: &Path,
    request: &Value,
) -> Result<Vec<Value>, HostError> {
    let mut child = Command::new(sidecar_path)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

    {
        let mut stdin = child
            .stdin
            .take()
            .ok_or_else(|| HostError::InvalidRequest("sidecar stdin 不可用".to_string()))?;
        write_json_line(&mut stdin, request)?;
        drop(stdin);
    }

    let stdout = child
        .stdout
        .take()
        .ok_or_else(|| HostError::InvalidRequest("sidecar stdout 不可用".to_string()))?;
    let stderr = child
        .stderr
        .take()
        .ok_or_else(|| HostError::InvalidRequest("sidecar stderr 不可用".to_string()))?;

    let stderr_reader = thread::spawn(move || drain_stderr(stderr));
    let mut events = read_ndjson_events(stdout)?;

    let status = child.wait()?;
    let stderr_text = stderr_reader.join().unwrap_or_default();
    if !status.success() {
        return Err(HostError::SidecarExited {
            code: status.code(),
            message: stderr_text,
        });
    }

    if events.is_empty() && !stderr_text.is_empty() {
        events.push(json!({
            "event": "failed",
            "jobId": "unknown",
            "message": stderr_text
        }));
    }

    Ok(events)
}

fn build_search_command(request: &SearchRequestPayload) -> Value {
    let cpu = request.cpu.clone().map(|cpu| {
        json!({
            "mode": cpu.mode,
            "workers": cpu.workers,
            "allowSmt": cpu.allow_smt,
            "allowLowPerf": cpu.allow_low_perf,
            "placement": cpu.placement,
            "enableWarmup": cpu.enable_warmup,
            "enableAdaptiveDown": cpu.enable_adaptive_down,
            "chunkSize": cpu.chunk_size,
            "progressInterval": cpu.progress_interval,
            "sampleWindowMs": cpu.sample_window_ms,
            "adaptiveMinWorkers": cpu.adaptive_min_workers,
            "adaptiveDropThreshold": cpu.adaptive_drop_threshold,
            "adaptiveDropWindows": cpu.adaptive_drop_windows,
            "adaptiveCooldownMs": cpu.adaptive_cooldown_ms,
        })
    });
    json!({
        "command": "search",
        "jobId": request.job_id,
        "worldType": request.world_type,
        "seedStart": request.seed_start,
        "seedEnd": request.seed_end,
        "mixing": request.mixing,
        "threads": request.threads,
        "constraints": {
            "required": request.constraints.required,
            "forbidden": request.constraints.forbidden,
            "distance": request.constraints.distance,
        },
        "cpu": cpu,
    })
}

fn build_preview_command(request: &PreviewRequestPayload) -> Value {
    json!({
        "command": "preview",
        "jobId": request.job_id,
        "worldType": request.world_type,
        "seed": request.seed,
        "mixing": request.mixing
    })
}

fn build_get_search_catalog_command(job_id: &str) -> Value {
    json!({
        "command": "get_search_catalog",
        "jobId": job_id
    })
}

fn write_json_line(writer: &mut impl Write, value: &Value) -> Result<(), HostError> {
    let text = serde_json::to_string(value)?;
    writer.write_all(text.as_bytes())?;
    writer.write_all(b"\n")?;
    writer.flush()?;
    Ok(())
}

fn read_ndjson_events(stdout: ChildStdout) -> Result<Vec<Value>, HostError> {
    let mut events = Vec::new();
    let reader = BufReader::new(stdout);
    for line in reader.lines() {
        let line = line?;
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let value: Value = serde_json::from_str(trimmed)?;
        events.push(value);
    }
    Ok(events)
}

fn drain_stderr(stderr: ChildStderr) -> String {
    let reader = BufReader::new(stderr);
    let mut lines = Vec::new();
    for line in reader.lines().map_while(Result::ok) {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        lines.push(trimmed.to_string());
    }
    lines.join("\n")
}

fn spawn_stderr_logger(app: AppHandle, job_id: String, stderr: ChildStderr) {
    thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines().map_while(Result::ok) {
            if line.trim().is_empty() {
                continue;
            }
            eprintln!("[sidecar:{}] {}", job_id, line);
            let _ = app.emit(
                SIDECAR_STDERR_CHANNEL,
                json!({
                    "jobId": job_id,
                    "message": line
                }),
            );
        }
    });
}

fn spawn_stdout_forwarder(
    app: AppHandle,
    registry: Arc<JobRegistry>,
    fallback_job_id: String,
    stdin_handle: Arc<Mutex<Option<std::process::ChildStdin>>>,
    stdout: ChildStdout,
) {
    thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines().map_while(Result::ok) {
            if line.trim().is_empty() {
                continue;
            }

            let parsed = serde_json::from_str::<Value>(&line);
            let event_json = match parsed {
                Ok(value) => value,
                Err(error) => {
                    let failed = json!({
                        "event": "failed",
                        "jobId": fallback_job_id,
                        "message": format!("sidecar 输出非 JSON 行: {}", error),
                    });
                    let _ = app.emit(SIDECAR_EVENT_CHANNEL, failed.clone());
                    let _ = registry.set_status(&fallback_job_id, JobStatus::Failed);
                    continue;
                }
            };

            let job_id = event_json
                .get("jobId")
                .and_then(Value::as_str)
                .unwrap_or(fallback_job_id.as_str())
                .to_string();
            let event_name = event_json
                .get("event")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string();

            let _ = app.emit(SIDECAR_EVENT_CHANNEL, event_json);

            match event_name.as_str() {
                "completed" => {
                    let _ = registry.set_status(&job_id, JobStatus::Completed);
                    let _ = stdin_handle.lock().map(|mut guard| guard.take());
                }
                "failed" => {
                    let _ = registry.set_status(&job_id, JobStatus::Failed);
                    let _ = stdin_handle.lock().map(|mut guard| guard.take());
                }
                "cancelled" => {
                    let _ = registry.set_status(&job_id, JobStatus::Cancelled);
                    let _ = stdin_handle.lock().map(|mut guard| guard.take());
                }
                _ => {}
            }
        }
    });
}

fn spawn_waiter(
    app: AppHandle,
    registry: Arc<JobRegistry>,
    job_id: String,
    child_handle: Arc<Mutex<Option<std::process::Child>>>,
    stdin_handle: Arc<Mutex<Option<std::process::ChildStdin>>>,
) {
    thread::spawn(move || loop {
        let maybe_status = {
            let mut guard = match child_handle.lock() {
                Ok(guard) => guard,
                Err(_) => return,
            };
            match guard.as_mut() {
                Some(child) => child.try_wait(),
                None => return,
            }
        };

        match maybe_status {
            Ok(Some(status)) => {
                if !status.success() && registry.is_running(&job_id) {
                    let _ = registry.set_status(&job_id, JobStatus::Failed);
                    let _ = app.emit(
                        SIDECAR_EVENT_CHANNEL,
                        json!({
                            "event": "failed",
                            "jobId": job_id,
                            "message": format!("sidecar 进程退出码: {:?}", status.code())
                        }),
                    );
                }
                let _ = stdin_handle.lock().map(|mut guard| guard.take());
                return;
            }
            Ok(None) => {
                thread::sleep(Duration::from_millis(40));
            }
            Err(error) => {
                if registry.is_running(&job_id) {
                    let _ = registry.set_status(&job_id, JobStatus::Failed);
                    let _ = app.emit(
                        SIDECAR_EVENT_CHANNEL,
                        json!({
                            "event": "failed",
                            "jobId": job_id,
                            "message": format!("sidecar 进程状态查询失败: {}", error)
                        }),
                    );
                }
                return;
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::{
        list_geyser_options, list_world_options, resolve_sidecar_path, run_sidecar_request_collect,
    };

    #[test]
    fn world_and_geyser_lists_should_be_non_empty() {
        let worlds = list_world_options();
        let geysers = list_geyser_options();
        assert!(worlds.len() >= 30);
        assert!(geysers.len() >= 30);
    }

    #[test]
    #[ignore = "需要先构建 out/build/x64-release/src/oni-sidecar.exe"]
    fn sidecar_search_smoke() {
        let path = resolve_sidecar_path(None).expect("sidecar path should be resolvable");
        let request = json!({
            "command": "search",
            "jobId": "smoke-search-001",
            "worldType": 13,
            "seedStart": 100000,
            "seedEnd": 100000,
            "mixing": 625,
            "threads": 1,
            "constraints": {
                "required": [],
                "forbidden": [],
                "distance": []
            }
        });
        let events =
            run_sidecar_request_collect(&path, &request).expect("search smoke should succeed");
        assert!(events.iter().any(|event| event["event"] == "started"));
        assert!(events.iter().any(|event| event["event"] == "completed"));
    }

    #[test]
    #[ignore = "需要先构建 out/build/x64-release/src/oni-sidecar.exe"]
    fn sidecar_preview_smoke() {
        let path = resolve_sidecar_path(None).expect("sidecar path should be resolvable");
        let request = json!({
            "command": "preview",
            "jobId": "smoke-preview-001",
            "worldType": 13,
            "seed": 100123,
            "mixing": 625
        });
        let events =
            run_sidecar_request_collect(&path, &request).expect("preview smoke should succeed");
        assert!(events.iter().any(|event| event["event"] == "preview"));
    }

    #[test]
    #[ignore = "需要先构建 out/build/x64-release/src/oni-sidecar.exe"]
    fn sidecar_search_regression_seed_100030_should_not_crash() {
        let path = resolve_sidecar_path(None).expect("sidecar path should be resolvable");
        let request = json!({
            "command": "search",
            "jobId": "regression-search-100030",
            "worldType": 13,
            "seedStart": 100030,
            "seedEnd": 100030,
            "mixing": 625,
            "threads": 1,
            "constraints": {
                "required": [],
                "forbidden": [],
                "distance": []
            }
        });
        let events = run_sidecar_request_collect(&path, &request).expect("search should not crash");
        assert!(events.iter().any(|event| event["event"] == "started"));
        assert!(events
            .iter()
            .any(|event| { event["event"] == "completed" || event["event"] == "failed" }));
    }

    #[test]
    #[ignore = "需要先构建 out/build/x64-release/src/oni-sidecar.exe"]
    fn sidecar_preview_regression_seed_100030_should_not_crash() {
        let path = resolve_sidecar_path(None).expect("sidecar path should be resolvable");
        let request = json!({
            "command": "preview",
            "jobId": "regression-preview-100030",
            "worldType": 13,
            "seed": 100030,
            "mixing": 625
        });
        let events =
            run_sidecar_request_collect(&path, &request).expect("preview should not crash");
        assert!(events
            .iter()
            .any(|event| { event["event"] == "preview" || event["event"] == "failed" }));
    }
}
