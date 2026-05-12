use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use tauri::webview::PageLoadEvent;
use tauri::{AppHandle, Url, WebviewUrl, WebviewWindow, WebviewWindowBuilder};

use crate::app_paths;
use crate::error::HostError;

const REPORT_PAGE_LOAD_TIMEOUT: Duration = Duration::from_secs(20);
const REPORT_DOM_READY_TIMEOUT: Duration = Duration::from_secs(20);
const REPORT_DOM_READY_POLL_INTERVAL: Duration = Duration::from_millis(100);

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ExportReportPdfRequest {
    pub html: String,
    pub output_path: String,
    pub title: String,
}

trait ReportPdfRuntime {
    fn print_html_file(
        &self,
        html_path: &Path,
        output_path: &Path,
        title: &str,
    ) -> Result<(), HostError>;
}

pub fn export_report_pdf(
    app: &AppHandle,
    request: &ExportReportPdfRequest,
) -> Result<(), HostError> {
    let temp_root = app_paths::resolve_app_local_data_dir(app)?.join("report-pdf");
    let runtime = WebviewReportPdfRuntime { app };
    export_report_pdf_with_runtime(&temp_root, request, &runtime)
}

fn export_report_pdf_with_runtime(
    temp_root: &Path,
    request: &ExportReportPdfRequest,
    runtime: &impl ReportPdfRuntime,
) -> Result<(), HostError> {
    validate_export_report_pdf_request(request)?;
    fs::create_dir_all(temp_root)?;

    let html_path = create_temp_html_file(temp_root, &request.html)?;
    let output_path = PathBuf::from(request.output_path.trim());
    let result = runtime.print_html_file(&html_path, &output_path, request.title.trim());
    let cleanup_result = fs::remove_file(&html_path);

    match (result, cleanup_result) {
        (Ok(()), Ok(())) => Ok(()),
        (Ok(()), Err(error)) => Err(HostError::Io(error)),
        (Err(error), Ok(())) => Err(error),
        (Err(error), Err(cleanup_error)) => Err(HostError::InvalidRequest(format!(
            "{}；临时 HTML 清理失败: {}",
            error, cleanup_error
        ))),
    }
}

fn validate_export_report_pdf_request(request: &ExportReportPdfRequest) -> Result<(), HostError> {
    if request.html.trim().is_empty() {
        return Err(HostError::InvalidRequest("html 不能为空".to_string()));
    }
    let output_path = request.output_path.trim();
    if output_path.is_empty() {
        return Err(HostError::InvalidRequest("outputPath 不能为空".to_string()));
    }
    let output = Path::new(output_path);
    let extension = output
        .extension()
        .and_then(|value| value.to_str())
        .map(|value| value.eq_ignore_ascii_case("pdf"))
        .unwrap_or(false);
    if !extension {
        return Err(HostError::InvalidRequest(
            "outputPath 必须是 .pdf 文件".to_string(),
        ));
    }
    Ok(())
}

fn create_temp_html_file(temp_root: &Path, html: &str) -> Result<PathBuf, HostError> {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| HostError::InvalidRequest(format!("系统时间无效: {}", error)))?
        .as_nanos();
    let path = temp_root.join(format!(
        "report-{}-{}.html",
        std::process::id(),
        nonce
    ));
    fs::write(&path, html)?;
    Ok(path)
}

struct WebviewReportPdfRuntime<'a> {
    app: &'a AppHandle,
}

impl ReportPdfRuntime for WebviewReportPdfRuntime<'_> {
    fn print_html_file(
        &self,
        html_path: &Path,
        output_path: &Path,
        title: &str,
    ) -> Result<(), HostError> {
        #[cfg(not(windows))]
        {
            let _ = (html_path, output_path, title);
            return Err(HostError::InvalidRequest(
                "export_report_pdf 当前仅支持 Windows".to_string(),
            ));
        }

        #[cfg(windows)]
        {
            let window = build_hidden_report_window(self.app, html_path, title)?;
            let result = wait_for_report_window_ready(&window)
                .and_then(|_| wait_for_report_dom_ready(&window))
                .and_then(|_| print_window_to_pdf(&window, output_path));
            close_report_window(&window);
            result
        }
    }
}

#[cfg(windows)]
fn build_hidden_report_window(
    app: &AppHandle,
    html_path: &Path,
    title: &str,
) -> Result<WebviewWindow, HostError> {
    let label = build_report_window_label();
    let url = Url::from_file_path(html_path).map_err(|_| {
        HostError::InvalidRequest(format!(
            "无法将临时 HTML 路径转换为 file URL: {}",
            html_path.display()
        ))
    })?;
    let (tx, page_ready_rx) = mpsc::sync_channel(1);
    let page_ready_tx = Arc::new(Mutex::new(Some(tx)));
    let page_ready_sender = Arc::clone(&page_ready_tx);

    let mut builder = WebviewWindowBuilder::new(app, &label, WebviewUrl::External(url))
        .title(title)
        .visible(false)
        .focused(false)
        .decorations(false)
        .skip_taskbar(true)
        .shadow(false)
        .on_page_load(move |_window, payload| {
            if matches!(payload.event(), PageLoadEvent::Finished) {
                if let Ok(mut guard) = page_ready_sender.lock() {
                    if let Some(sender) = guard.take() {
                        let _ = sender.send(());
                    }
                }
            }
        });

    if let Some(webview_data_dir) = app_paths::resolve_webview_data_dir(app)? {
        builder = builder.data_directory(webview_data_dir);
    }

    let window = builder.build().map_err(map_tauri_error)?;
    if page_ready_rx.recv_timeout(REPORT_PAGE_LOAD_TIMEOUT).is_err() {
        close_report_window(&window);
        return Err(HostError::InvalidRequest("页面加载失败或超时".to_string()));
    }
    Ok(window)
}

#[cfg(windows)]
fn wait_for_report_window_ready(window: &WebviewWindow) -> Result<(), HostError> {
    let script = "document.readyState === 'complete'";
    wait_for_script_truthy(window, script, REPORT_DOM_READY_TIMEOUT, "页面加载失败或超时")
}

#[cfg(windows)]
fn wait_for_report_dom_ready(window: &WebviewWindow) -> Result<(), HostError> {
    let script = r#"(function () {
  const fontsReady = !document.fonts || document.fonts.status === "loaded";
  const imagesReady = Array.from(document.images || []).every((image) => image.complete);
  return fontsReady && imagesReady;
})()"#;
    wait_for_script_truthy(window, script, REPORT_DOM_READY_TIMEOUT, "页面资源未就绪或超时")
}

#[cfg(windows)]
fn wait_for_script_truthy(
    window: &WebviewWindow,
    script: &str,
    timeout: Duration,
    timeout_message: &str,
) -> Result<(), HostError> {
    let started_at = std::time::Instant::now();
    loop {
        let result = execute_script(window, script)?;
        if normalize_script_result(&result) == Some(true) {
            return Ok(());
        }
        if started_at.elapsed() >= timeout {
            return Err(HostError::InvalidRequest(timeout_message.to_string()));
        }
        thread::sleep(REPORT_DOM_READY_POLL_INTERVAL);
    }
}

#[cfg(windows)]
fn execute_script(window: &WebviewWindow, script: &str) -> Result<String, HostError> {
    use webview2_com::{CoTaskMemPWSTR, ExecuteScriptCompletedHandler};
    use webview2_com::Microsoft::Web::WebView2::Win32::ICoreWebView2;
    let (tx, rx) = mpsc::sync_channel(1);
    let script_text = script.to_string();
    window.with_webview(move |webview| {
        let result = (|| unsafe {
            let controller = webview.controller();
            let core_webview: ICoreWebView2 = controller
                .CoreWebView2()
                .map_err(map_windows_error_to_host)?;
            let js = CoTaskMemPWSTR::from(script_text.as_str());
            let (callback_tx, callback_rx) = mpsc::channel();
            let handler = ExecuteScriptCompletedHandler::create(Box::new(move |error_code, result| {
                let callback_result = match error_code {
                    Ok(()) => Ok(result.to_string()),
                    Err(error) => Err(HostError::InvalidRequest(format!(
                        "脚本执行失败: {}",
                        error
                    ))),
                };
                let _ = callback_tx.send(callback_result);
                Ok(())
            }));
            core_webview
                .ExecuteScript(*js.as_ref().as_pcwstr(), &handler)
                .map_err(map_windows_error_to_host)?;
            webview2_com::wait_with_pump(callback_rx).map_err(map_webview2_error_to_host)?
        })();
        let _ = tx.send(result);
    })
    .map_err(map_tauri_error)?;
    rx.recv_timeout(REPORT_DOM_READY_TIMEOUT)
        .map_err(|_| HostError::InvalidRequest("脚本执行超时".to_string()))?
}

#[cfg(windows)]
fn print_window_to_pdf(window: &WebviewWindow, output_path: &Path) -> Result<(), HostError> {
    use webview2_com::{CoTaskMemPWSTR, PrintToPdfCompletedHandler};
    use webview2_com::Microsoft::Web::WebView2::Win32::{
        ICoreWebView2PrintSettings, ICoreWebView2_7,
    };
    use windows::core::Interface;

    let output_path_string = output_path.display().to_string();
    let (tx, rx) = mpsc::sync_channel(1);
    window.with_webview(move |webview| {
        let result = (|| unsafe {
            let controller = webview.controller();
            let core_webview: ICoreWebView2_7 = controller
                .CoreWebView2()
                .map_err(map_windows_error_to_host)?
                .cast()
                .map_err(map_windows_error_to_host)?;
            let pdf_path = CoTaskMemPWSTR::from(output_path_string.as_str());
            let (callback_tx, callback_rx) = mpsc::channel();
            let handler = PrintToPdfCompletedHandler::create(Box::new(move |error_code, success| {
                let callback_result = match error_code {
                    Ok(()) => {
                        if success {
                            Ok(())
                        } else {
                            Err(HostError::InvalidRequest(
                                "PrintToPdf 回调失败: WebView2 返回 false".to_string(),
                            ))
                        }
                    }
                    Err(error) => Err(HostError::InvalidRequest(format!(
                        "PrintToPdf 回调失败: {}",
                        error
                    ))),
                };
                let _ = callback_tx.send(callback_result);
                Ok(())
            }));
            core_webview
                .PrintToPdf(
                    *pdf_path.as_ref().as_pcwstr(),
                    Option::<&ICoreWebView2PrintSettings>::None,
                    &handler,
                )
                .map_err(map_windows_error_to_host)?;
            webview2_com::wait_with_pump(callback_rx).map_err(map_webview2_error_to_host)?
        })();
        let _ = tx.send(result);
    })
    .map_err(map_tauri_error)?;
    rx.recv_timeout(REPORT_PAGE_LOAD_TIMEOUT)
        .map_err(|_| HostError::InvalidRequest("PrintToPdf 回调失败: 超时".to_string()))?
}

#[cfg(windows)]
fn close_report_window(window: &WebviewWindow) {
    let _ = window.close();
}

fn build_report_window_label() -> String {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|value| value.as_nanos())
        .unwrap_or(0);
    format!("report-pdf-{}-{}", std::process::id(), nonce)
}

fn normalize_script_result(result: &str) -> Option<bool> {
    match result.trim() {
        "true" => Some(true),
        "false" => Some(false),
        _ => None,
    }
}

fn map_tauri_error(error: tauri::Error) -> HostError {
    HostError::InvalidRequest(format!("Tauri 窗口操作失败: {}", error))
}

#[cfg(windows)]
fn map_webview2_error_to_host(error: webview2_com::Error) -> HostError {
    HostError::InvalidRequest(format!("WebView2 操作失败: {}", error))
}

#[cfg(windows)]
fn map_windows_error_to_host(error: windows::core::Error) -> HostError {
    HostError::InvalidRequest(format!("Windows API 调用失败: {}", error))
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::{Path, PathBuf};
    use std::sync::{Arc, Mutex};
    use std::time::{SystemTime, UNIX_EPOCH};

    use super::{
        export_report_pdf_with_runtime, ExportReportPdfRequest, ReportPdfRuntime,
    };
    use crate::error::HostError;

    #[derive(Clone, Copy)]
    enum FakeRuntimeMode {
        Success,
        LoadFailed,
        PrintFailed,
    }

    #[derive(Clone)]
    struct FakeRuntime {
        mode: FakeRuntimeMode,
        observed_html_path: Arc<Mutex<Option<std::path::PathBuf>>>,
        observed_html_exists_during_call: Arc<Mutex<bool>>,
    }

    impl FakeRuntime {
        fn new(mode: FakeRuntimeMode) -> Self {
            Self {
                mode,
                observed_html_path: Arc::new(Mutex::new(None)),
                observed_html_exists_during_call: Arc::new(Mutex::new(false)),
            }
        }
    }

    impl ReportPdfRuntime for FakeRuntime {
        fn print_html_file(
            &self,
            html_path: &Path,
            _output_path: &Path,
            _title: &str,
        ) -> Result<(), HostError> {
            *self
                .observed_html_path
                .lock()
                .expect("observed html path lock poisoned") = Some(html_path.to_path_buf());
            *self
                .observed_html_exists_during_call
                .lock()
                .expect("observed html exists lock poisoned") = html_path.is_file();
            match self.mode {
                FakeRuntimeMode::Success => Ok(()),
                FakeRuntimeMode::LoadFailed => Err(HostError::InvalidRequest(
                    "页面加载失败或超时".to_string(),
                )),
                FakeRuntimeMode::PrintFailed => Err(HostError::InvalidRequest(
                    "PrintToPdf 回调失败".to_string(),
                )),
            }
        }
    }

    fn create_temp_root(name: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system time should be valid")
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "oni-report-pdf-tests-{}-{}-{}",
            name,
            std::process::id(),
            nonce
        ));
        fs::create_dir_all(&root).expect("temp root should be created");
        root
    }

    fn build_request(output_path: &str) -> ExportReportPdfRequest {
        ExportReportPdfRequest {
            html: "<!doctype html><html><body>report</body></html>".to_string(),
            output_path: output_path.to_string(),
            title: "世界报告".to_string(),
        }
    }

    #[test]
    fn report_pdf_should_reject_empty_html() {
        let root = create_temp_root("empty-html");
        let runtime = FakeRuntime::new(FakeRuntimeMode::Success);
        let mut request = build_request("C:\\temp\\world-report.pdf");
        request.html = "   ".to_string();

        let error = export_report_pdf_with_runtime(&root, &request, &runtime)
            .expect_err("empty html should be rejected");

        assert!(error.to_string().contains("html"));
    }

    #[test]
    fn report_pdf_should_reject_non_pdf_output_path() {
        let root = create_temp_root("non-pdf");
        let runtime = FakeRuntime::new(FakeRuntimeMode::Success);
        let request = build_request("C:\\temp\\world-report.txt");

        let error = export_report_pdf_with_runtime(&root, &request, &runtime)
            .expect_err("non pdf output should be rejected");

        assert!(error.to_string().contains(".pdf"));
    }

    #[test]
    fn report_pdf_should_create_and_cleanup_temp_html_on_success() {
        let root = create_temp_root("cleanup-success");
        let runtime = FakeRuntime::new(FakeRuntimeMode::Success);
        let request = build_request("C:\\temp\\world-report.pdf");

        export_report_pdf_with_runtime(&root, &request, &runtime)
            .expect("success path should succeed");

        let html_path = runtime
            .observed_html_path
            .lock()
            .expect("observed html path lock poisoned")
            .clone()
            .expect("runtime should observe html path");
        assert!(
            *runtime
                .observed_html_exists_during_call
                .lock()
                .expect("observed html exists lock poisoned"),
            "temp html should exist while runtime is printing"
        );
        assert!(
            !html_path.exists(),
            "temp html should be cleaned up after successful export"
        );
    }

    #[test]
    fn report_pdf_should_cleanup_temp_html_after_load_failure() {
        let root = create_temp_root("cleanup-load-failed");
        let runtime = FakeRuntime::new(FakeRuntimeMode::LoadFailed);
        let request = build_request("C:\\temp\\world-report.pdf");

        let error = export_report_pdf_with_runtime(&root, &request, &runtime)
            .expect_err("load failure should surface");

        let html_path = runtime
            .observed_html_path
            .lock()
            .expect("observed html path lock poisoned")
            .clone()
            .expect("runtime should observe html path");
        assert!(error.to_string().contains("页面加载失败"));
        assert!(
            !html_path.exists(),
            "temp html should be cleaned up after page load failure"
        );
    }

    #[test]
    fn report_pdf_should_cleanup_temp_html_after_print_failure() {
        let root = create_temp_root("cleanup-print-failed");
        let runtime = FakeRuntime::new(FakeRuntimeMode::PrintFailed);
        let request = build_request("C:\\temp\\world-report.pdf");

        let error = export_report_pdf_with_runtime(&root, &request, &runtime)
            .expect_err("print failure should surface");

        let html_path = runtime
            .observed_html_path
            .lock()
            .expect("observed html path lock poisoned")
            .clone()
            .expect("runtime should observe html path");
        assert!(error.to_string().contains("PrintToPdf 回调失败"));
        assert!(
            !html_path.exists(),
            "temp html should be cleaned up after print failure"
        );
    }
}
