use std::collections::HashMap;
use std::process::{Child, ChildStdin};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;

use crate::control_sidecar::ControlSidecarManager;
use crate::error::HostError;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum JobStatus {
    Running,
    Cancelling,
    Completed,
    Failed,
    Cancelled,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct JobProgressSnapshot {
    pub processed_seeds: u64,
    pub total_seeds: u64,
    pub total_matches: u64,
    pub active_workers: u32,
}

#[derive(Clone)]
pub struct RunningJobHandles {
    pub child_handle: Arc<Mutex<Option<Child>>>,
    pub stdin_handle: Arc<Mutex<Option<ChildStdin>>>,
    pub cancel_token: Arc<AtomicBool>,
}

#[allow(dead_code)]
pub struct JobEntry {
    pub job_id: String,
    pub status: JobStatus,
    pub started_at_ms: u128,
    pub handles: RunningJobHandles,
    pub progress_snapshot: Option<JobProgressSnapshot>,
}

#[derive(Clone)]
pub struct ActiveJobSnapshot {
    pub job_id: String,
    pub status: JobStatus,
    pub handles: RunningJobHandles,
}

#[derive(Default)]
pub struct JobRegistry {
    jobs: Mutex<HashMap<String, JobEntry>>,
}

impl JobRegistry {
    pub fn insert_running(
        &self,
        job_id: String,
        handles: RunningJobHandles,
    ) -> Result<(), HostError> {
        let mut guard = self.jobs.lock().expect("job registry lock poisoned");
        if let Some(existing) = guard.get(&job_id) {
            if existing.status == JobStatus::Running {
                return Err(HostError::JobConflict(job_id));
            }
        }
        guard.insert(
            job_id.clone(),
            JobEntry {
                job_id,
                status: JobStatus::Running,
                started_at_ms: now_epoch_ms(),
                handles,
                progress_snapshot: None,
            },
        );
        Ok(())
    }

    pub fn set_status(&self, job_id: &str, status: JobStatus) -> Result<(), HostError> {
        let mut guard = self.jobs.lock().expect("job registry lock poisoned");
        let entry = guard
            .get_mut(job_id)
            .ok_or_else(|| HostError::JobNotFound(job_id.to_string()))?;
        entry.status = status;
        Ok(())
    }

    pub fn get_status(&self, job_id: &str) -> Option<JobStatus> {
        let guard = self.jobs.lock().expect("job registry lock poisoned");
        guard.get(job_id).map(|entry| entry.status)
    }

    pub fn is_running(&self, job_id: &str) -> bool {
        self.get_status(job_id) == Some(JobStatus::Running)
    }

    pub fn get_handles(&self, job_id: &str) -> Option<RunningJobHandles> {
        let guard = self.jobs.lock().expect("job registry lock poisoned");
        guard.get(job_id).map(|entry| entry.handles.clone())
    }

    pub fn update_progress_snapshot(
        &self,
        job_id: &str,
        snapshot: JobProgressSnapshot,
    ) -> Result<(), HostError> {
        let mut guard = self.jobs.lock().expect("job registry lock poisoned");
        let entry = guard
            .get_mut(job_id)
            .ok_or_else(|| HostError::JobNotFound(job_id.to_string()))?;
        entry.progress_snapshot = Some(snapshot);
        Ok(())
    }

    pub fn get_progress_snapshot(&self, job_id: &str) -> Option<JobProgressSnapshot> {
        let guard = self.jobs.lock().expect("job registry lock poisoned");
        guard
            .get(job_id)
            .and_then(|entry| entry.progress_snapshot.clone())
    }

    pub fn snapshot_active_jobs(&self) -> Vec<ActiveJobSnapshot> {
        let guard = self.jobs.lock().expect("job registry lock poisoned");
        guard
            .values()
            .filter(|entry| matches!(entry.status, JobStatus::Running | JobStatus::Cancelling))
            .map(|entry| ActiveJobSnapshot {
                job_id: entry.job_id.clone(),
                status: entry.status,
                handles: entry.handles.clone(),
            })
            .collect()
    }
}

#[derive(Clone)]
pub struct AppState {
    pub jobs: Arc<JobRegistry>,
    pub control_sidecar: ControlSidecarManager,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            jobs: Arc::new(JobRegistry::default()),
            control_sidecar: ControlSidecarManager::default(),
        }
    }
}

fn now_epoch_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::{AppState, JobProgressSnapshot, JobRegistry, JobStatus, RunningJobHandles};
    use std::sync::atomic::AtomicBool;
    use std::sync::{Arc, Mutex};

    fn create_handles() -> RunningJobHandles {
        RunningJobHandles {
            child_handle: Arc::new(Mutex::new(None)),
            stdin_handle: Arc::new(Mutex::new(None)),
            cancel_token: Arc::new(AtomicBool::new(false)),
        }
    }

    #[test]
    fn registry_should_store_and_update_status() {
        let registry = JobRegistry::default();
        let handles = create_handles();

        registry
            .insert_running("job-001".to_string(), handles)
            .expect("insert running job should succeed");
        assert!(registry.is_running("job-001"));
        assert_eq!(registry.get_status("job-001"), Some(JobStatus::Running));

        registry
            .set_status("job-001", JobStatus::Completed)
            .expect("set status should succeed");
        assert_eq!(registry.get_status("job-001"), Some(JobStatus::Completed));
    }

    #[test]
    fn registry_should_reject_running_duplicate_job() {
        let registry = JobRegistry::default();
        let handles = create_handles();
        registry
            .insert_running("job-dup".to_string(), handles.clone())
            .expect("first insert should succeed");

        let duplicate = registry.insert_running("job-dup".to_string(), handles);
        assert!(duplicate.is_err());
    }

    #[test]
    fn registry_should_support_cancelling_status_transitions() {
        let registry = JobRegistry::default();

        registry
            .insert_running("job-cancel".to_string(), create_handles())
            .expect("insert running job should succeed");
        assert_eq!(registry.get_status("job-cancel"), Some(JobStatus::Running));

        registry
            .set_status("job-cancel", JobStatus::Cancelling)
            .expect("set cancelling should succeed");
        assert_eq!(
            registry.get_status("job-cancel"),
            Some(JobStatus::Cancelling)
        );

        registry
            .set_status("job-cancel", JobStatus::Cancelled)
            .expect("set cancelled should succeed");
        assert_eq!(
            registry.get_status("job-cancel"),
            Some(JobStatus::Cancelled)
        );
    }

    #[test]
    fn registry_should_store_latest_progress_snapshot() {
        let registry = JobRegistry::default();

        registry
            .insert_running("job-progress".to_string(), create_handles())
            .expect("insert running job should succeed");

        let snapshot = JobProgressSnapshot {
            processed_seeds: 128,
            total_seeds: 1024,
            total_matches: 7,
            active_workers: 3,
        };

        registry
            .update_progress_snapshot("job-progress", snapshot.clone())
            .expect("update snapshot should succeed");

        assert_eq!(
            registry.get_progress_snapshot("job-progress"),
            Some(snapshot)
        );
    }

    #[test]
    fn registry_should_snapshot_only_active_jobs() {
        let registry = JobRegistry::default();
        registry
            .insert_running("job-running".to_string(), create_handles())
            .expect("insert running job should succeed");
        registry
            .insert_running("job-cancelling".to_string(), create_handles())
            .expect("insert cancelling job should succeed");
        registry
            .insert_running("job-completed".to_string(), create_handles())
            .expect("insert completed job should succeed");
        registry
            .set_status("job-cancelling", JobStatus::Cancelling)
            .expect("set cancelling should succeed");
        registry
            .set_status("job-completed", JobStatus::Completed)
            .expect("set completed should succeed");

        let snapshots = registry.snapshot_active_jobs();
        assert_eq!(snapshots.len(), 2);
        assert!(snapshots
            .iter()
            .any(|job| { job.job_id == "job-running" && job.status == JobStatus::Running }));
        assert!(snapshots
            .iter()
            .any(|job| { job.job_id == "job-cancelling" && job.status == JobStatus::Cancelling }));
        assert!(!snapshots.iter().any(|job| job.job_id == "job-completed"));
    }

    #[test]
    fn app_state_should_enable_control_sidecar_by_default() {
        let state = AppState::default();

        assert!(
            state.control_sidecar.is_enabled(),
            "Task 3 切默认值后，desktop 应默认走常驻 control sidecar"
        );
    }
}
