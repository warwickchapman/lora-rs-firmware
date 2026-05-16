pub mod commands;
pub mod services;

use serde::Serialize;
use std::time::Duration;
use tauri::Emitter;
#[cfg(target_os = "macos")]
use tauri_plugin_dialog::{DialogExt, MessageDialogButtons};

#[derive(Serialize, Clone)]
struct SerialPortsChangedEvent {
    ports: Vec<String>,
}

fn start_serial_port_watcher(app: tauri::AppHandle) {
    tauri::async_runtime::spawn(async move {
        let mut last_ports: Vec<String> = Vec::new();

        loop {
            let mut current_ports: Vec<String> = crate::services::serial::list_ports()
                .into_iter()
                .map(|p| p.port_name)
                .collect();
            current_ports.sort();

            if current_ports != last_ports {
                let _ = app.emit(
                    "serial-ports-changed",
                    SerialPortsChangedEvent {
                        ports: current_ports.clone(),
                    },
                );
                last_ports = current_ports;
            }

            tokio::time::sleep(Duration::from_millis(1500)).await;
        }
    });
}

#[cfg(target_os = "macos")]
fn app_bundle_from_exe_path(exe_path: &std::path::Path) -> Option<std::path::PathBuf> {
    exe_path
        .ancestors()
        .find(|path| path.extension().and_then(|ext| ext.to_str()) == Some("app"))
        .map(std::path::Path::to_path_buf)
}

#[cfg(target_os = "macos")]
fn is_installed_path(path: &std::path::Path) -> bool {
    if path.starts_with("/Applications") {
        return true;
    }

    std::env::var_os("HOME")
        .map(std::path::PathBuf::from)
        .map(|home| path.starts_with(home.join("Applications")))
        .unwrap_or(false)
}

#[cfg(target_os = "macos")]
fn maybe_offer_move_to_applications(app: &tauri::AppHandle) {
    if cfg!(debug_assertions) {
        return;
    }

    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(_) => return,
    };

    let source_app = match app_bundle_from_exe_path(&exe_path) {
        Some(path) => path,
        None => return,
    };

    if is_installed_path(&source_app) {
        return;
    }

    let destination_app = std::path::PathBuf::from("/Applications")
        .join(source_app.file_name().unwrap_or(std::ffi::OsStr::new("Thanda LoRa Flasher.app")));

    let app_handle = app.clone();
    std::thread::spawn(move || {
        let move_now = app_handle
            .dialog()
            .message("Move Thanda LoRa Flasher to Applications?")
            .title("Install Flasher")
            .buttons(MessageDialogButtons::OkCancelCustom(
                "Move to Applications".to_string(),
                "Run once".to_string(),
            ))
            .blocking_show();

        if !move_now {
            return;
        }

        let copy_status = std::process::Command::new("ditto")
            .arg(&source_app)
            .arg(&destination_app)
            .status();

        if !copy_status.map(|status| status.success()).unwrap_or(false) {
            return;
        }

        let _ = std::process::Command::new("open")
            .arg("-n")
            .arg(&destination_app)
            .spawn();

        app_handle.exit(0);
    });
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(crate::commands::monitor::MonitorState {
            status: std::sync::Arc::new(tokio::sync::Mutex::new(
                crate::commands::monitor::MonitorStatus::default(),
            )),
        })
        .manage(crate::services::serial_port_coordinator::SerialPortCoordinator::default())
        .manage(crate::commands::network::UdpMonitorState::default())
        .manage(crate::commands::network::FirmwareServerState::default())
        .setup(|app| {
            #[cfg(target_os = "macos")]
            maybe_offer_move_to_applications(&app.handle().clone());
            start_serial_port_watcher(app.handle().clone());
            Ok(())
        })
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_process::init())
        .invoke_handler(tauri::generate_handler![
            crate::commands::serial::list_serial_ports,
            crate::commands::flash::flash_firmware,
            crate::commands::github::get_firmware_list,
            crate::commands::status::get_system_status,
            crate::commands::status::get_app_version,
            crate::commands::device::get_device_info,
            crate::commands::monitor::toggle_serial_monitor,
            crate::commands::network::start_firmware_file_server,
            crate::commands::network::stop_firmware_file_server,
            crate::commands::network::start_network_udp_monitor,
            crate::commands::network::stop_network_udp_monitor,
            crate::commands::easy_pair::serial_admin_command,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
