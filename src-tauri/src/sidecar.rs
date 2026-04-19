use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{ChildStderr, ChildStdout, Command, Stdio};
use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};

use crate::error::HostError;
use crate::state::{JobRegistry, JobStatus, RunningJobHandles};

#[cfg(windows)]
use std::mem::size_of;
#[cfg(windows)]
use std::os::windows::io::AsRawHandle;
#[cfg(windows)]
use windows_sys::Win32::Foundation::{GetLastError, ERROR_INSUFFICIENT_BUFFER};
#[cfg(windows)]
use windows_sys::Win32::System::SystemInformation::{
    CpuSetInformation, GetSystemCpuSetInformation, SYSTEM_CPU_SET_INFORMATION,
};
#[cfg(windows)]
use windows_sys::Win32::System::Threading::SetProcessAffinityMask;

pub const SIDECAR_EVENT_CHANNEL: &str = "sidecar://event";
pub const SIDECAR_STDERR_CHANNEL: &str = "sidecar://stderr";
const HOST_DEBUG_PREFIX: &str = "[host-debug]";

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
    #[serde(default)]
    pub count: Vec<CountRule>,
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

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CountRule {
    pub geyser: String,
    pub min_count: i32,
    pub max_count: i32,
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
    #[serde(default, alias = "forbiddenDLCIds")]
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

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidationIssue {
    pub layer: String,
    pub code: String,
    pub field: String,
    pub message: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NormalizedConstraintGroup {
    pub geyser_id: String,
    pub geyser_index: i32,
    pub min_count: i32,
    pub max_count: i32,
    pub has_required: bool,
    pub has_forbidden: bool,
    pub has_explicit_count: bool,
    pub distance: Vec<DistanceRule>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NormalizedSearchRequestPayload {
    pub world_type: i32,
    pub seed_start: i32,
    pub seed_end: i32,
    pub mixing: i32,
    pub threads: i32,
    pub groups: Vec<NormalizedConstraintGroup>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchAnalysisPayload {
    pub world_profile: WorldEnvelopeProfilePayload,
    pub normalized_request: NormalizedSearchRequestPayload,
    pub errors: Vec<ValidationIssue>,
    pub warnings: Vec<ValidationIssue>,
    pub bottlenecks: Vec<String>,
    pub predicted_bottleneck_probability: f64,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourceSummaryPayload {
    pub rule_id: String,
    pub template_name: String,
    pub geyser_id: String,
    pub upper_bound: i32,
    pub source_kind: String,
    pub pool_id: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SpatialEnvelopePayload {
    pub envelope_id: String,
    pub confidence: String,
    pub method: String,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WorldEnvelopeProfilePayload {
    pub valid: bool,
    pub world_type: i32,
    pub world_code: String,
    pub width: i32,
    pub height: i32,
    pub diagonal: f64,
    pub active_mixing_slots: Vec<i32>,
    pub disabled_mixing_slots: Vec<i32>,
    pub possible_geyser_types: Vec<String>,
    pub impossible_geyser_types: Vec<String>,
    pub possible_max_count_by_type: BTreeMap<String, i32>,
    pub generic_type_upper_by_id: BTreeMap<String, f64>,
    pub generic_slot_upper: i32,
    pub exact_source_summary: Vec<SourceSummaryPayload>,
    pub generic_source_summary: Vec<SourceSummaryPayload>,
    pub source_pools: Vec<SourcePoolPayload>,
    pub spatial_envelopes: Vec<SpatialEnvelopePayload>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SourcePoolPayload {
    pub pool_id: String,
    pub source_kind: String,
    pub capacity_upper: i32,
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
    emit_host_debug(
        &app,
        &request.job_id,
        format!(
            "{} resolved sidecar path: {}",
            HOST_DEBUG_PREFIX,
            sidecar_path.display()
        ),
    );
    emit_host_debug(
        &app,
        &request.job_id,
        format!("{} payload: {}", HOST_DEBUG_PREFIX, payload),
    );
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

    request_search_cancel(&handles, job_id);
    let forced = force_stop_child_process(&handles);

    if forced && registry.is_running(job_id) {
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

fn request_search_cancel(handles: &RunningJobHandles, job_id: &str) {
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
}

fn force_stop_child_process(handles: &RunningJobHandles) -> bool {
    let mut forced = false;
    if let Ok(mut guard) = handles.child_handle.lock() {
        if let Some(child) = guard.as_mut() {
            match child.try_wait() {
                Ok(Some(_)) => {}
                Ok(None) | Err(_) => {
                    let _ = child.kill();
                    forced = true;
                }
            }
        }
    }
    let _ = handles.stdin_handle.lock().map(|mut guard| guard.take());
    forced
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
    let payload = build_preview_command(request);
    let events =
        run_sidecar_request_collect(&sidecar_path, &payload, sidecar_request_priority(&payload))?;

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
    let events =
        run_sidecar_request_collect(&sidecar_path, &request, sidecar_request_priority(&request))?;

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
            return deserialize_search_catalog_payload(catalog);
        }
    }

    Err(HostError::InvalidRequest(
        "未收到 search_catalog 事件".to_string(),
    ))
}

fn deserialize_search_catalog_payload(catalog: Value) -> Result<SearchCatalogPayload, HostError> {
    serde_json::from_value(catalog).map_err(|error| {
        HostError::InvalidRequest(format!("search_catalog 反序列化失败: {}", error))
    })
}

pub fn analyze_search_request(
    app: Option<&AppHandle>,
    request: &SearchRequestPayload,
) -> Result<SearchAnalysisPayload, HostError> {
    if request.job_id.trim().is_empty() {
        return Err(HostError::InvalidRequest("jobId 不能为空".to_string()));
    }
    let sidecar_path = resolve_sidecar_path(app)?;
    let payload = build_analyze_search_command(request);
    let events =
        run_sidecar_request_collect(&sidecar_path, &payload, sidecar_request_priority(&payload))?;
    for event in events {
        if event.get("event").and_then(Value::as_str) == Some("failed") {
            let message = event
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("analyze 请求失败");
            return Err(HostError::InvalidRequest(message.to_string()));
        }
        if event.get("event").and_then(Value::as_str) == Some("search_analysis")
            && event.get("jobId").and_then(Value::as_str) == Some(request.job_id.as_str())
        {
            let analysis = event.get("analysis").cloned().ok_or_else(|| {
                HostError::InvalidRequest("search_analysis 缺少 analysis 字段".to_string())
            })?;
            return serde_json::from_value(analysis).map_err(HostError::from);
        }
    }
    Err(HostError::InvalidRequest(
        "未收到 search_analysis 事件".to_string(),
    ))
}

fn collect_sidecar_candidates(manifest_dir: &Path, resource_dir: Option<&Path>) -> Vec<PathBuf> {
    let repo_root = manifest_dir
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| manifest_dir.to_path_buf());
    let mut candidates = Vec::new();

    candidates.push(manifest_dir.join("binaries/oni-sidecar.exe"));

    if let Some(resource_dir) = resource_dir {
        candidates.push(resource_dir.join("oni-sidecar.exe"));
        candidates.push(resource_dir.join("oni-sidecar-x86_64-pc-windows-msvc.exe"));
        candidates.push(resource_dir.join("oni-sidecar-x86_64-pc-windows-gnu.exe"));
    }

    candidates.push(repo_root.join("out/build/mingw-release/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-release/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-debug/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/mingw-debug/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-release/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-release/src/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-debug/oni-sidecar.exe"));
    candidates.push(repo_root.join("out/build/x64-debug/src/oni-sidecar.exe"));

    candidates
}

#[derive(Debug)]
struct ExistingSidecarCandidate {
    path: PathBuf,
    modified_at: SystemTime,
    preference_index: usize,
}

fn collect_existing_sidecar_candidates(candidates: &[PathBuf]) -> Vec<ExistingSidecarCandidate> {
    candidates
        .iter()
        .enumerate()
        .filter_map(|(preference_index, path)| {
            let metadata = path.metadata().ok()?;
            if !metadata.is_file() || metadata.len() == 0 {
                return None;
            }
            let modified_at = metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH);
            Some(ExistingSidecarCandidate {
                path: path.to_path_buf(),
                modified_at,
                preference_index,
            })
        })
        .collect()
}

fn first_existing_sidecar_path(candidates: &[PathBuf]) -> Option<PathBuf> {
    collect_existing_sidecar_candidates(candidates)
        .into_iter()
        .max_by(|left, right| {
            left.modified_at
                .cmp(&right.modified_at)
                .then_with(|| right.preference_index.cmp(&left.preference_index))
        })
        .map(|candidate| candidate.path)
}

fn runtime_sidecar_dir(app: &AppHandle) -> Result<PathBuf, HostError> {
    let base_dir = app.path().app_local_data_dir().map_err(|error| {
        HostError::InvalidRequest(format!("无法解析 sidecar 运行目录: {}", error))
    })?;
    Ok(base_dir.join("sidecars"))
}

fn runtime_sidecar_file_name(source_path: &Path) -> Result<String, HostError> {
    let metadata = source_path.metadata()?;
    let modified = metadata
        .modified()
        .unwrap_or(SystemTime::UNIX_EPOCH)
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    Ok(format!("oni-sidecar-{}-{}.exe", modified, metadata.len()))
}

fn cleanup_runtime_sidecar_dir(runtime_dir: &Path, active_path: &Path) -> Result<(), HostError> {
    if !runtime_dir.is_dir() {
        return Ok(());
    }

    for entry in fs::read_dir(runtime_dir)? {
        let entry = entry?;
        let path = entry.path();
        if path == active_path {
            continue;
        }
        let Some(file_name) = path.file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        if !file_name.starts_with("oni-sidecar-") || !file_name.ends_with(".exe") {
            continue;
        }
        if let Err(error) = fs::remove_file(&path) {
            if error.kind() != std::io::ErrorKind::NotFound
                && error.kind() != std::io::ErrorKind::PermissionDenied
                && error.kind() != std::io::ErrorKind::Other
            {
                return Err(HostError::Io(error));
            }
        }
    }

    Ok(())
}

fn prepare_runtime_sidecar_copy(
    runtime_dir: &Path,
    source_path: &Path,
) -> Result<PathBuf, HostError> {
    fs::create_dir_all(runtime_dir)?;
    let runtime_path = runtime_dir.join(runtime_sidecar_file_name(source_path)?);
    if !runtime_path.is_file() {
        fs::copy(source_path, &runtime_path)?;
    }
    cleanup_runtime_sidecar_dir(runtime_dir, &runtime_path)?;
    Ok(runtime_path)
}

pub fn resolve_sidecar_path(app: Option<&AppHandle>) -> Result<PathBuf, HostError> {
    if let Some(path) = env::var_os("ONI_SIDECAR_PATH") {
        let candidate = PathBuf::from(path);
        if candidate.is_file() {
            return Ok(candidate);
        }
    }

    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let resource_dir = app.and_then(|app_handle| app_handle.path().resource_dir().ok());
    let candidates = collect_sidecar_candidates(&manifest_dir, resource_dir.as_deref());

    if let Some(existing) = first_existing_sidecar_path(&candidates) {
        if let Some(app_handle) = app {
            return prepare_runtime_sidecar_copy(&runtime_sidecar_dir(app_handle)?, &existing);
        }
        return Ok(existing);
    }

    Err(HostError::SidecarNotFound { candidates })
}

fn run_sidecar_request_collect(
    sidecar_path: &Path,
    request: &Value,
    priority: SidecarProcessPriority,
) -> Result<Vec<Value>, HostError> {
    let mut command = Command::new(sidecar_path);
    command
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let mut child = command.spawn()?;
    apply_process_priority(&child, priority);

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SidecarProcessPriority {
    Normal,
    LowPerfAffinity,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct PreviewCpuSetInfo {
    logical_index: u32,
    efficiency_class: u8,
}

fn preview_affinity_mask_for_cpu_sets(cpu_sets: &[PreviewCpuSetInfo]) -> Option<usize> {
    let min_efficiency_class = cpu_sets.iter().map(|cpu| cpu.efficiency_class).min()?;
    let mut has_low_perf = false;
    let mut mask = 0usize;

    for cpu in cpu_sets {
        if cpu.efficiency_class <= min_efficiency_class {
            continue;
        }
        has_low_perf = true;
        if cpu.logical_index < usize::BITS {
            mask |= 1usize << cpu.logical_index;
        }
    }

    if !has_low_perf || mask == 0 {
        return None;
    }
    Some(mask)
}

#[cfg(windows)]
fn detect_preview_affinity_mask() -> Option<usize> {
    let mut bytes_needed = 0u32;
    let probe_ok = unsafe {
        GetSystemCpuSetInformation(
            std::ptr::null_mut(),
            0,
            &mut bytes_needed,
            std::ptr::null_mut(),
            0,
        )
    };
    if probe_ok != 0 || bytes_needed == 0 {
        return None;
    }
    if unsafe { GetLastError() } != ERROR_INSUFFICIENT_BUFFER {
        return None;
    }

    let mut buffer = vec![0u8; bytes_needed as usize];
    let load_ok = unsafe {
        GetSystemCpuSetInformation(
            buffer.as_mut_ptr() as *mut SYSTEM_CPU_SET_INFORMATION,
            bytes_needed,
            &mut bytes_needed,
            std::ptr::null_mut(),
            0,
        )
    };
    if load_ok == 0 {
        return None;
    }

    let mut cpu_sets = Vec::new();
    let mut offset = 0usize;
    while offset + size_of::<SYSTEM_CPU_SET_INFORMATION>() <= bytes_needed as usize {
        let raw = unsafe { &*(buffer.as_ptr().add(offset) as *const SYSTEM_CPU_SET_INFORMATION) };
        if raw.Size == 0 {
            break;
        }
        if raw.Type == CpuSetInformation {
            let cpu = unsafe { raw.Anonymous.CpuSet };
            cpu_sets.push(PreviewCpuSetInfo {
                logical_index: cpu.LogicalProcessorIndex as u32,
                efficiency_class: cpu.EfficiencyClass,
            });
        }
        offset += raw.Size as usize;
    }

    preview_affinity_mask_for_cpu_sets(&cpu_sets)
}

#[cfg(windows)]
fn apply_process_priority(child: &std::process::Child, priority: SidecarProcessPriority) {
    if priority != SidecarProcessPriority::LowPerfAffinity {
        return;
    }
    let Some(mask) = detect_preview_affinity_mask() else {
        return;
    };
    unsafe {
        let _ = SetProcessAffinityMask(child.as_raw_handle() as _, mask);
    }
}

#[cfg(not(windows))]
fn apply_process_priority(_child: &std::process::Child, _priority: SidecarProcessPriority) {}

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
            "count": request.constraints.count,
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

fn build_analyze_search_command(request: &SearchRequestPayload) -> Value {
    json!({
        "command": "analyze_search_request",
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
            "count": request.constraints.count,
        },
        "cpu": request.cpu,
    })
}

fn sidecar_request_priority(request: &Value) -> SidecarProcessPriority {
    if request.get("command").and_then(Value::as_str) == Some("preview") {
        return SidecarProcessPriority::LowPerfAffinity;
    }
    SidecarProcessPriority::Normal
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

fn emit_host_debug(app: &AppHandle, job_id: &str, message: String) {
    eprintln!("[host:{}] {}", job_id, message);
    let _ = app.emit(
        SIDECAR_STDERR_CHANNEL,
        json!({
            "jobId": job_id,
            "message": message
        }),
    );
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
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::process::{Command, Stdio};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Mutex};
    use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

    use serde_json::json;

    use crate::state::RunningJobHandles;

    use super::{
        collect_sidecar_candidates, first_existing_sidecar_path, force_stop_child_process,
        list_geyser_options, list_world_options, prepare_runtime_sidecar_copy,
        preview_affinity_mask_for_cpu_sets, request_search_cancel, resolve_sidecar_path,
        run_sidecar_request_collect, PreviewCpuSetInfo, SearchCatalogPayload,
        SidecarProcessPriority,
    };

    fn create_temp_root(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time should be valid")
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "oni-sidecar-tests-{}-{}-{}",
            name,
            std::process::id(),
            nonce
        ));
        fs::create_dir_all(&root).expect("temp root should be created");
        root
    }

    fn write_dummy_sidecar(path: &Path) {
        let parent = path.parent().expect("sidecar path should have parent");
        fs::create_dir_all(parent).expect("sidecar parent should be created");
        fs::write(path, b"ok").expect("dummy sidecar should be written");
    }

    fn spawn_sleeping_process() -> std::process::Child {
        Command::new("cmd")
            .args(["/C", "ping -n 10 127.0.0.1 > nul"])
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .expect("sleeping child should spawn")
    }

    fn build_catalog_with_trait_field(field_name: &str) -> serde_json::Value {
        json!({
            "worlds": [],
            "geysers": [],
            "mixingSlots": [],
            "parameterSpecs": [],
            "traits": [
                {
                    "id": "traits/Volcanoes",
                    "name": "Volcanoes",
                    "description": "desc",
                    "traitTags": [],
                    "exclusiveWith": [],
                    "exclusiveWithTags": [],
                    field_name: ["EXPANSION1_ID"],
                    "effectSummary": [],
                    "searchable": false
                }
            ]
        })
    }

    #[test]
    fn world_and_geyser_lists_should_be_non_empty() {
        let worlds = list_world_options();
        let geysers = list_geyser_options();
        assert!(worlds.len() >= 30);
        assert!(geysers.len() >= 30);
    }

    #[test]
    fn resolve_candidates_should_choose_latest_modified_sidecar() {
        let root = create_temp_root("prefer-latest");
        let manifest_dir = root.join("src-tauri");
        let binaries = manifest_dir.join("binaries/oni-sidecar.exe");
        let x64 = root.join("out/build/x64-release/src/oni-sidecar.exe");

        write_dummy_sidecar(&binaries);
        std::thread::sleep(Duration::from_millis(20));
        write_dummy_sidecar(&x64);

        let candidates = collect_sidecar_candidates(&manifest_dir, None);
        let resolved = first_existing_sidecar_path(&candidates).expect("sidecar should resolve");
        assert_eq!(resolved, x64);

        fs::remove_dir_all(root).expect("temp root should be removed");
    }

    #[test]
    fn resolve_candidates_should_prefer_mingw_release_over_x64_release() {
        let root = create_temp_root("prefer-mingw");
        let manifest_dir = root.join("src-tauri");
        let mingw = root.join("out/build/mingw-release/src/oni-sidecar.exe");
        let x64 = root.join("out/build/x64-release/src/oni-sidecar.exe");

        write_dummy_sidecar(&x64);
        std::thread::sleep(Duration::from_millis(20));
        write_dummy_sidecar(&mingw);

        let candidates = collect_sidecar_candidates(&manifest_dir, None);
        let resolved = first_existing_sidecar_path(&candidates).expect("sidecar should resolve");
        assert_eq!(resolved, mingw);

        fs::remove_dir_all(root).expect("temp root should be removed");
    }

    #[test]
    fn prepare_runtime_sidecar_copy_should_keep_only_latest_runtime_copy() {
        let root = create_temp_root("runtime-sidecar-copy");
        let runtime_dir = root.join("runtime");
        let source_a = root.join("out/build/mingw-release/src/oni-sidecar.exe");
        let source_b = root.join("out/build/mingw-debug/src/oni-sidecar.exe");

        write_dummy_sidecar(&source_a);
        std::thread::sleep(Duration::from_millis(20));
        write_dummy_sidecar(&source_b);

        let first_copy =
            prepare_runtime_sidecar_copy(&runtime_dir, &source_a).expect("first runtime copy");
        assert!(first_copy.is_file(), "first runtime copy should exist");

        let second_copy =
            prepare_runtime_sidecar_copy(&runtime_dir, &source_b).expect("second runtime copy");
        assert!(second_copy.is_file(), "second runtime copy should exist");
        assert_ne!(
            first_copy, second_copy,
            "new sidecar should use a new runtime copy path"
        );
        assert!(
            !first_copy.exists(),
            "older runtime sidecar copy should be cleaned after switching"
        );

        fs::remove_dir_all(root).expect("temp root should be removed");
    }

    #[test]
    fn search_catalog_should_accept_trait_forbidden_dlc_ids_uppercase_alias() {
        let catalog = build_catalog_with_trait_field("forbiddenDLCIds");

        let payload = serde_json::from_value::<SearchCatalogPayload>(catalog)
            .expect("search catalog should deserialize forbiddenDLCIds");

        assert_eq!(payload.traits.len(), 1);
        assert_eq!(payload.traits[0].forbidden_dlc_ids, vec!["EXPANSION1_ID"]);
    }

    #[test]
    fn search_catalog_should_accept_trait_forbidden_dlc_ids_camel_case() {
        let catalog = build_catalog_with_trait_field("forbiddenDlcIds");

        let payload = serde_json::from_value::<SearchCatalogPayload>(catalog)
            .expect("search catalog should deserialize forbiddenDlcIds");

        assert_eq!(payload.traits.len(), 1);
        assert_eq!(payload.traits[0].forbidden_dlc_ids, vec!["EXPANSION1_ID"]);
    }

    #[test]
    fn preview_affinity_should_prefer_low_perf_logical_processors() {
        let cpu_sets = vec![
            PreviewCpuSetInfo {
                logical_index: 0,
                efficiency_class: 0,
            },
            PreviewCpuSetInfo {
                logical_index: 2,
                efficiency_class: 0,
            },
            PreviewCpuSetInfo {
                logical_index: 8,
                efficiency_class: 6,
            },
            PreviewCpuSetInfo {
                logical_index: 9,
                efficiency_class: 6,
            },
        ];

        assert_eq!(
            preview_affinity_mask_for_cpu_sets(&cpu_sets),
            Some((1usize << 8) | (1usize << 9))
        );
    }

    #[test]
    fn preview_affinity_should_skip_homogeneous_topology() {
        let cpu_sets = vec![
            PreviewCpuSetInfo {
                logical_index: 0,
                efficiency_class: 0,
            },
            PreviewCpuSetInfo {
                logical_index: 1,
                efficiency_class: 0,
            },
        ];

        assert_eq!(preview_affinity_mask_for_cpu_sets(&cpu_sets), None);
    }

    #[test]
    fn cancel_helpers_should_force_stop_running_child_promptly() {
        let mut child = spawn_sleeping_process();
        let stdin = child.stdin.take();
        let handles = RunningJobHandles {
            child_handle: Arc::new(Mutex::new(Some(child))),
            stdin_handle: Arc::new(Mutex::new(stdin)),
            cancel_token: Arc::new(AtomicBool::new(false)),
        };

        request_search_cancel(&handles, "job-cancel");
        let started_at = Instant::now();
        force_stop_child_process(&handles);

        let status = {
            let mut guard = handles
                .child_handle
                .lock()
                .expect("child handle lock should succeed");
            guard
                .as_mut()
                .expect("child should remain owned until wait")
                .wait()
                .expect("child wait should succeed")
        };

        assert!(
            started_at.elapsed() < Duration::from_secs(1),
            "forced stop should return before a long-running seed evaluation finishes"
        );
        assert!(
            handles.cancel_token.load(Ordering::Relaxed),
            "cancel token should be set before force stop"
        );
        assert!(
            !status.success(),
            "force-stopped search sidecar should not exit successfully"
        );
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
        let events = run_sidecar_request_collect(&path, &request, SidecarProcessPriority::Normal)
            .expect("search smoke should succeed");
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
            run_sidecar_request_collect(&path, &request, SidecarProcessPriority::LowPerfAffinity)
                .expect("preview smoke should succeed");
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
        let events = run_sidecar_request_collect(&path, &request, SidecarProcessPriority::Normal)
            .expect("search should not crash");
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
            run_sidecar_request_collect(&path, &request, SidecarProcessPriority::LowPerfAffinity)
                .expect("preview should not crash");
        assert!(events
            .iter()
            .any(|event| { event["event"] == "preview" || event["event"] == "failed" }));
    }
}
