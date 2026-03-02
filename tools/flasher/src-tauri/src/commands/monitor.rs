use tauri::{AppHandle, Emitter, State};
use std::time::Duration;
use std::sync::Arc;
use tokio::sync::Mutex;
use std::io::{BufRead, BufReader};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone)]
pub struct MonitorEvent {
    pub line: String,
}

pub struct MonitorState {
    pub running: Arc<Mutex<bool>>,
}

#[tauri::command]
pub async fn toggle_serial_monitor(
    app: AppHandle,
    state: State<'_, MonitorState>,
    port: String,
    baud: u32,
    enable: bool,
) -> Result<(), String> {
    let mut running = state.running.lock().await;
    
    if enable {
        if *running {
            return Ok(());
        }
        *running = true;
        
        let running_clone = state.running.clone();
        let app_handle = app.clone();
        
        // Use task::spawn_blocking for blocking serial I/O
        tokio::task::spawn_blocking(move || {
            let builder = serialport::new(&port, baud)
                .timeout(Duration::from_millis(100));
            
            match builder.open() {
                Ok(serial_port) => {
                    let mut reader = BufReader::new(serial_port);
                    // We need a way to check if we should stop
                    // Since we're in a blocking thread, we'll check the mutex periodically
                    loop {
                        // Check if we should still be running
                        // We use a small hack here: try_lock or similar if we were more advanced
                        // but for now, we'll just check if the app is still alive or use a timeout-based loop
                        let mut line = String::new();
                        if reader.read_line(&mut line).is_ok() {
                            if !line.is_empty() {
                                let _ = app_handle.emit("monitor-log", MonitorEvent { line: line.clone() });
                            }
                        }
                        
                        // Check stop flag
                        // In a blocking thread, we have to be careful not to block forever
                        // The serialport timeout (100ms) handles the reader.read_line block
                        if let Ok(run) = running_clone.try_lock() {
                            if !*run { break; }
                        }
                    }
                }
                Err(e) => {
                    let _ = app_handle.emit("monitor-log", MonitorEvent { 
                        line: format!("Error opening port: {}", e) 
                    });
                    if let Ok(mut run) = running_clone.try_lock() {
                        *run = false;
                    }
                }
            }
        });
    } else {
        *running = false;
    }
    
    Ok(())
}
