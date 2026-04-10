use std::fmt::{Display, Formatter};
use std::path::PathBuf;

#[derive(Debug)]
pub enum HostError {
    SidecarNotFound { candidates: Vec<PathBuf> },
    InvalidRequest(String),
    JobConflict(String),
    JobNotFound(String),
    Io(std::io::Error),
    Json(serde_json::Error),
    SidecarExited { code: Option<i32>, message: String },
}

impl Display for HostError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            HostError::SidecarNotFound { candidates } => {
                let joined = candidates
                    .iter()
                    .map(|path| path.to_string_lossy().to_string())
                    .collect::<Vec<_>>()
                    .join(", ");
                write!(f, "未找到 sidecar 可执行文件，已尝试路径: {}", joined)
            }
            HostError::InvalidRequest(message) => write!(f, "请求非法: {}", message),
            HostError::JobConflict(job_id) => write!(f, "任务已存在且仍在运行: {}", job_id),
            HostError::JobNotFound(job_id) => write!(f, "任务不存在: {}", job_id),
            HostError::Io(error) => write!(f, "I/O 错误: {}", error),
            HostError::Json(error) => write!(f, "JSON 错误: {}", error),
            HostError::SidecarExited { code, message } => {
                write!(f, "sidecar 进程异常退出(code={:?}): {}", code, message)
            }
        }
    }
}

impl std::error::Error for HostError {}

impl From<std::io::Error> for HostError {
    fn from(value: std::io::Error) -> Self {
        Self::Io(value)
    }
}

impl From<serde_json::Error> for HostError {
    fn from(value: serde_json::Error) -> Self {
        Self::Json(value)
    }
}
