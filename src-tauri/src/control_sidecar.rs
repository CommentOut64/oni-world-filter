use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;
use std::process::{Child, ChildStderr, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};

use serde_json::Value;
use tauri::AppHandle;

use crate::error::HostError;
use crate::sidecar::{
    self, PreviewRequestPayload, SearchAnalysisPayload, SearchCatalogPayload, SearchRequestPayload,
};

#[derive(Clone)]
pub struct ControlSidecarManager {
    enabled: bool,
    launcher_override: Option<SidecarLauncher>,
    active_process: Arc<Mutex<Option<ControlSidecarProcess>>>,
}

impl Default for ControlSidecarManager {
    fn default() -> Self {
        Self {
            enabled: control_sidecar_enabled_by_env(),
            launcher_override: None,
            active_process: Arc::new(Mutex::new(None)),
        }
    }
}

impl ControlSidecarManager {
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn reset(&self) {
        let mut guard = self
            .active_process
            .lock()
            .expect("control sidecar lock poisoned");
        if let Some(process) = guard.take() {
            process.shutdown();
        }
    }

    pub fn request_or_fallback(
        &self,
        app: Option<&AppHandle>,
        request: &Value,
    ) -> Result<Vec<Value>, HostError> {
        if !self.enabled {
            let launcher = self.resolve_launcher(app)?;
            return launcher.run_once(request);
        }
        self.request(app, request)
    }

    pub fn request(
        &self,
        app: Option<&AppHandle>,
        request: &Value,
    ) -> Result<Vec<Value>, HostError> {
        let launcher = self.resolve_launcher(app)?;
        let mut guard = self
            .active_process
            .lock()
            .expect("control sidecar lock poisoned");

        for attempt in 0..=1 {
            if guard.is_none() {
                *guard = Some(ControlSidecarProcess::spawn(&launcher)?);
            }

            let outcome = {
                let process = guard
                    .as_mut()
                    .expect("control sidecar process should exist after spawn");
                process.request(request)
            };

            match outcome {
                Ok(outcome) => {
                    if !outcome.keep_alive {
                        if let Some(process) = guard.take() {
                            process.shutdown();
                        }
                    }
                    return Ok(outcome.events);
                }
                Err(error) => {
                    if let Some(process) = guard.take() {
                        process.shutdown();
                    }
                    if attempt == 0 {
                        continue;
                    }
                    return Err(error);
                }
            }
        }

        unreachable!("control sidecar request retry loop should always return")
    }

    #[cfg(test)]
    fn new_for_tests(launcher: SidecarLauncher, enabled: bool) -> Self {
        Self {
            enabled,
            launcher_override: Some(launcher),
            active_process: Arc::new(Mutex::new(None)),
        }
    }

    fn resolve_launcher(&self, app: Option<&AppHandle>) -> Result<SidecarLauncher, HostError> {
        if let Some(launcher) = self.launcher_override.clone() {
            return Ok(launcher);
        }
        Ok(SidecarLauncher {
            program: sidecar::resolve_sidecar_path(app)?,
            args: Vec::new(),
        })
    }
}

pub fn load_preview(
    app: Option<&AppHandle>,
    manager: &ControlSidecarManager,
    request: &PreviewRequestPayload,
) -> Result<Value, HostError> {
    sidecar::validate_preview_request(request)?;
    let payload = sidecar::build_preview_command(request);
    let events = manager.request_or_fallback(app, &payload)?;

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

pub fn get_search_catalog(
    app: Option<&AppHandle>,
    manager: &ControlSidecarManager,
) -> Result<SearchCatalogPayload, HostError> {
    let job_id = "search-catalog";
    let payload = sidecar::build_get_search_catalog_command(job_id);
    let events = manager.request_or_fallback(app, &payload)?;

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
            return sidecar::deserialize_search_catalog_payload(catalog);
        }
    }

    Err(HostError::InvalidRequest(
        "未收到 search_catalog 事件".to_string(),
    ))
}

pub fn analyze_search_request(
    app: Option<&AppHandle>,
    manager: &ControlSidecarManager,
    request: &SearchRequestPayload,
) -> Result<SearchAnalysisPayload, HostError> {
    sidecar::validate_analyze_search_request(request)?;
    let payload = sidecar::build_analyze_search_command(request);
    let events = manager.request_or_fallback(app, &payload)?;

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

fn control_sidecar_enabled_by_env() -> bool {
    std::env::var("ONI_DESKTOP_CONTROL_SIDECAR")
        .map(|value| value != "0")
        .unwrap_or(true)
}

#[derive(Clone, Debug)]
struct SidecarLauncher {
    program: PathBuf,
    args: Vec<String>,
}

struct ControlSidecarProcess {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
    stderr_lines: Arc<Mutex<Vec<String>>>,
    stderr_reader: Option<JoinHandle<()>>,
}

struct ControlRequestOutcome {
    events: Vec<Value>,
    keep_alive: bool,
}

impl SidecarLauncher {
    fn run_once(&self, request: &Value) -> Result<Vec<Value>, HostError> {
        let mut command = Command::new(&self.program);
        command
            .args(&self.args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        let mut child = command.spawn()?;

        {
            let mut stdin = child
                .stdin
                .take()
                .ok_or_else(|| HostError::InvalidRequest("sidecar stdin 不可用".to_string()))?;
            write_json_line(&mut stdin, request)?;
        }

        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| HostError::InvalidRequest("sidecar stdout 不可用".to_string()))?;
        let stderr = child
            .stderr
            .take()
            .ok_or_else(|| HostError::InvalidRequest("sidecar stderr 不可用".to_string()))?;

        let stderr_reader = std::thread::spawn(move || drain_stderr(stderr));
        let events = read_ndjson_events(stdout)?;
        let status = child.wait()?;
        let stderr_text = stderr_reader.join().unwrap_or_default();
        if !status.success() {
            return Err(HostError::SidecarExited {
                code: status.code(),
                message: stderr_text,
            });
        }
        Ok(events)
    }
}

impl ControlSidecarProcess {
    fn spawn(launcher: &SidecarLauncher) -> Result<Self, HostError> {
        let mut command = Command::new(&launcher.program);
        command
            .args(&launcher.args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        let mut child = command.spawn()?;
        let stdin = child
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
        let stderr_lines = Arc::new(Mutex::new(Vec::new()));
        let stderr_lines_clone = Arc::clone(&stderr_lines);
        let stderr_reader = Some(thread::spawn(move || {
            drain_stderr_into(stderr, stderr_lines_clone);
        }));

        Ok(Self {
            child,
            stdin,
            stdout: BufReader::new(stdout),
            stderr_lines,
            stderr_reader,
        })
    }

    fn request(&mut self, request: &Value) -> Result<ControlRequestOutcome, HostError> {
        write_json_line(&mut self.stdin, request)?;
        let terminal_event = terminal_event_name(request)?;
        let request_job_id = request
            .get("jobId")
            .and_then(Value::as_str)
            .unwrap_or("unknown");
        let mut events = Vec::new();

        loop {
            let mut line = String::new();
            let bytes = self.stdout.read_line(&mut line)?;
            if bytes == 0 {
                return Err(self.build_exit_error()?);
            }

            let trimmed = line.trim();
            if trimmed.is_empty() {
                continue;
            }

            let event: Value = serde_json::from_str(trimmed)?;
            let event_name = event.get("event").and_then(Value::as_str);
            let event_job_id = event.get("jobId").and_then(Value::as_str);
            let is_terminal = event_name == Some("failed")
                || (event_name == Some(terminal_event) && event_job_id == Some(request_job_id));
            events.push(event);

            if is_terminal {
                let keep_alive = self.child.try_wait()?.is_none();
                return Ok(ControlRequestOutcome { events, keep_alive });
            }
        }
    }

    fn shutdown(mut self) {
        drop(self.stdin);
        match self.child.try_wait() {
            Ok(Some(_)) => {}
            Ok(None) | Err(_) => {
                let _ = self.child.kill();
                let _ = self.child.wait();
            }
        }

        if let Some(reader) = self.stderr_reader.take() {
            let _ = reader.join();
        }
    }

    fn build_exit_error(&mut self) -> Result<HostError, HostError> {
        let status = self.child.wait()?;
        Ok(HostError::SidecarExited {
            code: status.code(),
            message: self.stderr_text(),
        })
    }

    fn stderr_text(&self) -> String {
        let guard = self
            .stderr_lines
            .lock()
            .expect("control sidecar stderr lock poisoned");
        guard.join("\n")
    }
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
        events.push(serde_json::from_str(trimmed)?);
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

fn drain_stderr_into(stderr: ChildStderr, lines: Arc<Mutex<Vec<String>>>) {
    let reader = BufReader::new(stderr);
    for line in reader.lines().map_while(Result::ok) {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        if let Ok(mut guard) = lines.lock() {
            guard.push(trimmed.to_string());
        }
    }
}

fn terminal_event_name(request: &Value) -> Result<&'static str, HostError> {
    match request.get("command").and_then(Value::as_str) {
        Some("preview") => Ok("preview"),
        Some("get_search_catalog") => Ok("search_catalog"),
        Some("analyze_search_request") => Ok("search_analysis"),
        Some(other) => Err(HostError::InvalidRequest(format!(
            "control sidecar 不支持的命令: {}",
            other
        ))),
        None => Err(HostError::InvalidRequest(
            "control sidecar 缺少 command 字段".to_string(),
        )),
    }
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::sync::{Arc, Mutex, OnceLock};
    use std::sync::atomic::AtomicBool;
    use std::time::{SystemTime, UNIX_EPOCH};

    use serde_json::json;

    use crate::state::{JobRegistry, RunningJobHandles};

    use super::{ControlSidecarManager, SidecarLauncher};

    fn env_lock() -> &'static Mutex<()> {
        static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
        LOCK.get_or_init(|| Mutex::new(()))
    }

    fn create_temp_root(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time should be valid")
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "oni-control-sidecar-tests-{}-{}-{}",
            name,
            std::process::id(),
            nonce
        ));
        fs::create_dir_all(&root).expect("temp root should be created");
        root
    }

    fn write_fake_sidecar_script(script_path: &Path) {
        let script = r#"
param(
    [string]$CounterPath,
    [string]$Mode
)

$spawnCount = 1
if (Test-Path -LiteralPath $CounterPath) {
    $existing = Get-Content -LiteralPath $CounterPath -Raw
    if (-not [string]::IsNullOrWhiteSpace($existing)) {
        $spawnCount = [int]$existing + 1
    }
}
Set-Content -LiteralPath $CounterPath -Value $spawnCount -NoNewline

while (($line = [Console]::In.ReadLine()) -ne $null) {
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }

    $request = $line | ConvertFrom-Json
    switch ($request.command) {
        'get_search_catalog' {
            $event = @{
                event = 'search_catalog'
                jobId = $request.jobId
                pid = $PID
                spawnCount = $spawnCount
                catalog = @{
                    worlds = @()
                    geysers = @()
                    mixingSlots = @()
                    parameterSpecs = @()
                    traits = @()
                }
            }
        }
        'analyze_search_request' {
            $event = @{
                event = 'search_analysis'
                jobId = $request.jobId
                pid = $PID
                spawnCount = $spawnCount
                analysis = @{
                    worldProfile = @{
                        valid = $true
                        worldType = 13
                        worldCode = 'SNDST-A-'
                        width = 256
                        height = 384
                        diagonal = 461.52
                        activeMixingSlots = @()
                        disabledMixingSlots = @()
                        possibleGeyserTypes = @()
                        impossibleGeyserTypes = @()
                        possibleMaxCountByType = @{}
                        genericTypeUpperById = @{}
                        genericSlotUpper = 0
                        exactSourceSummary = @()
                        genericSourceSummary = @()
                        sourcePools = @()
                        spatialEnvelopes = @()
                    }
                    normalizedRequest = @{
                        worldType = 13
                        seedStart = 100000
                        seedEnd = 100001
                        mixing = 625
                        threads = 1
                        groups = @()
                    }
                    errors = @()
                    warnings = @()
                    bottlenecks = @()
                    predictedBottleneckProbability = 0.0
                }
            }
        }
        'preview' {
            $event = @{
                event = 'preview'
                jobId = $request.jobId
                pid = $PID
                spawnCount = $spawnCount
                preview = @{
                    seed = $request.seed
                }
            }
        }
        default {
            $event = @{
                event = 'failed'
                jobId = $request.jobId
                pid = $PID
                spawnCount = $spawnCount
                message = 'unknown command'
            }
        }
    }

    $event | ConvertTo-Json -Compress -Depth 20
    if ($Mode -eq 'exit-after-request') {
        break
    }
}
"#;
        fs::write(script_path, script).expect("fake sidecar script should be written");
    }

    fn create_test_manager(root: &Path, mode: &str, enabled: bool) -> ControlSidecarManager {
        let script_path = root.join("fake-sidecar.ps1");
        let counter_path = root.join("spawn-count.txt");
        write_fake_sidecar_script(&script_path);

        ControlSidecarManager::new_for_tests(
            SidecarLauncher {
                program: PathBuf::from("powershell.exe"),
                args: vec![
                    "-ExecutionPolicy".to_string(),
                    "Bypass".to_string(),
                    "-File".to_string(),
                    script_path.to_string_lossy().to_string(),
                    counter_path.to_string_lossy().to_string(),
                    mode.to_string(),
                ],
            },
            enabled,
        )
    }

    fn create_handles() -> RunningJobHandles {
        RunningJobHandles {
            child_handle: Arc::new(std::sync::Mutex::new(None)),
            stdin_handle: Arc::new(std::sync::Mutex::new(None)),
            cancel_token: Arc::new(AtomicBool::new(false)),
        }
    }

    #[test]
    fn control_sidecar_should_reuse_same_child_for_catalog_and_analyze() {
        let root = create_temp_root("reuse-child");
        let manager = create_test_manager(&root, "persistent", true);

        let catalog_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "get_search_catalog",
                    "jobId": "search-catalog"
                }),
            )
            .expect("catalog request should succeed");
        let analyze_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "analyze_search_request",
                    "jobId": "analysis-001"
                }),
            )
            .expect("analyze request should succeed");

        assert_eq!(catalog_events[0]["spawnCount"].as_i64(), Some(1));
        assert_eq!(
            analyze_events[0]["spawnCount"].as_i64(),
            Some(1),
            "enabled control sidecar should keep the same child alive across requests"
        );
    }

    #[test]
    fn control_sidecar_should_respawn_after_unexpected_exit() {
        let root = create_temp_root("respawn-after-exit");
        let manager = create_test_manager(&root, "exit-after-request", true);

        let first_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "get_search_catalog",
                    "jobId": "search-catalog"
                }),
            )
            .expect("first request should succeed");
        let second_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "analyze_search_request",
                    "jobId": "analysis-002"
                }),
            )
            .expect("second request should respawn and succeed");

        assert_eq!(first_events[0]["spawnCount"].as_i64(), Some(1));
        assert_eq!(second_events[0]["spawnCount"].as_i64(), Some(2));
    }

    #[test]
    fn control_sidecar_preview_should_not_touch_streaming_job_registry() {
        let root = create_temp_root("preview-registry");
        let manager = create_test_manager(&root, "persistent", true);
        let registry = JobRegistry::default();
        registry
            .insert_running("streaming-job".to_string(), create_handles())
            .expect("streaming job should be inserted");

        let preview_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "preview",
                    "jobId": "preview-001",
                    "seed": 100123
                }),
            )
            .expect("preview request should succeed");

        assert_eq!(preview_events[0]["event"].as_str(), Some("preview"));
        assert_eq!(registry.get_status("streaming-job"), Some(crate::state::JobStatus::Running));
        assert_eq!(registry.get_status("preview-001"), None);
    }

    #[test]
    fn control_sidecar_should_fallback_to_ephemeral_mode_when_disabled() {
        let root = create_temp_root("disabled-fallback");
        let manager = create_test_manager(&root, "persistent", false);

        let first_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "get_search_catalog",
                    "jobId": "search-catalog"
                }),
            )
            .expect("first request should succeed");
        let second_events = manager
            .request_or_fallback(
                None,
                &json!({
                    "command": "analyze_search_request",
                    "jobId": "analysis-003"
                }),
            )
            .expect("second request should also succeed");

        assert_eq!(first_events[0]["spawnCount"].as_i64(), Some(1));
        assert_eq!(
            second_events[0]["spawnCount"].as_i64(),
            Some(2),
            "disabled control sidecar should keep the old one-request-one-process model"
        );
    }

    #[test]
    fn control_sidecar_should_respect_env_disable_flag() {
        let _guard = env_lock()
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let previous = std::env::var_os("ONI_DESKTOP_CONTROL_SIDECAR");
        std::env::set_var("ONI_DESKTOP_CONTROL_SIDECAR", "0");

        let manager = ControlSidecarManager::default();

        match previous {
            Some(value) => std::env::set_var("ONI_DESKTOP_CONTROL_SIDECAR", value),
            None => std::env::remove_var("ONI_DESKTOP_CONTROL_SIDECAR"),
        }

        assert!(
            !manager.is_enabled(),
            "显式关闭开关时必须回到旧的一次请求一次 sidecar 路径"
        );
    }

    #[test]
    fn control_sidecar_should_enable_by_default_when_env_is_unset() {
        let _guard = env_lock()
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let previous = std::env::var_os("ONI_DESKTOP_CONTROL_SIDECAR");
        std::env::remove_var("ONI_DESKTOP_CONTROL_SIDECAR");

        let manager = ControlSidecarManager::default();

        match previous {
            Some(value) => std::env::set_var("ONI_DESKTOP_CONTROL_SIDECAR", value),
            None => std::env::remove_var("ONI_DESKTOP_CONTROL_SIDECAR"),
        }

        assert!(
            manager.is_enabled(),
            "未显式关闭时，desktop 应默认启用常驻 control sidecar"
        );
    }
}
