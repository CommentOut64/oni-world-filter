use std::collections::HashMap;
use std::process::{Child, ChildStdin};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;

use crate::error::HostError;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum JobStatus {
    Running,
    Completed,
    Failed,
    Cancelled,
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
}

#[derive(Clone, Default)]
pub struct AppState {
    pub jobs: Arc<JobRegistry>,
}

fn now_epoch_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::{JobRegistry, JobStatus, RunningJobHandles};
    use std::sync::atomic::AtomicBool;
    use std::sync::{Arc, Mutex};

    #[test]
    fn registry_should_store_and_update_status() {
        let registry = JobRegistry::default();
        let handles = RunningJobHandles {
            child_handle: Arc::new(Mutex::new(None)),
            stdin_handle: Arc::new(Mutex::new(None)),
            cancel_token: Arc::new(AtomicBool::new(false)),
        };

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
        let handles = RunningJobHandles {
            child_handle: Arc::new(Mutex::new(None)),
            stdin_handle: Arc::new(Mutex::new(None)),
            cancel_token: Arc::new(AtomicBool::new(false)),
        };
        registry
            .insert_running("job-dup".to_string(), handles.clone())
            .expect("first insert should succeed");

        let duplicate = registry.insert_running("job-dup".to_string(), handles);
        assert!(duplicate.is_err());
    }
}
