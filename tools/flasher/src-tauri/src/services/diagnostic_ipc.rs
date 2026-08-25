use crate::services::diagnostics::DiagnosticStore;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::fs;
#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{UnixListener, UnixStream};

#[derive(Deserialize)]
struct Request {
    token: String,
    method: String,
    id: Option<String>,
    capture_id: Option<String>,
    after_sequence: Option<u64>,
    limit: Option<usize>,
}

#[derive(Serialize)]
struct Response {
    id: Option<String>,
    ok: bool,
    result: Option<Value>,
    error: Option<String>,
}

fn runtime_dir() -> PathBuf {
    std::env::temp_dir().join("thanda-lora-flasher-diagnostics")
}

fn token() -> String {
    let mut hash = Sha256::new();
    hash.update(format!(
        "{}:{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos()
    ));
    hex::encode(hash.finalize())
}

pub fn start(store: DiagnosticStore) {
    let dir = runtime_dir();
    let _ = fs::create_dir_all(&dir);
    #[cfg(unix)]
    let _ = fs::set_permissions(&dir, fs::Permissions::from_mode(0o700));
    let socket = dir.join("flasher.sock");
    let token_path = dir.join("token");
    let _ = fs::remove_file(&socket);
    let token = token();
    let _ = fs::write(&token_path, &token);
    #[cfg(unix)]
    let _ = fs::set_permissions(&token_path, fs::Permissions::from_mode(0o600));
    tauri::async_runtime::spawn(async move {
        let Ok(listener) = UnixListener::bind(socket) else {
            return;
        };
        loop {
            if let Ok((stream, _)) = listener.accept().await {
                let store = store.clone();
                let token = token.clone();
                tauri::async_runtime::spawn(async move { serve(stream, store, token).await });
            }
        }
    });
}

async fn serve(stream: UnixStream, store: DiagnosticStore, token: String) {
    let (read, mut write) = stream.into_split();
    let mut lines = BufReader::new(read).lines();
    while let Ok(Some(line)) = lines.next_line().await {
        let response = match serde_json::from_str::<Request>(&line) {
            Ok(request) if request.token == token => dispatch(request, &store).await,
            _ => Response {
                id: None,
                ok: false,
                result: None,
                error: Some("unauthorized".into()),
            },
        };
        if let Ok(text) = serde_json::to_string(&response) {
            let _ = write.write_all(format!("{text}\n").as_bytes()).await;
        }
    }
}

/// This narrow local IPC only exposes values already held by DiagnosticStore.
/// It never calls a Flasher command or owns a device-facing transport.
async fn dispatch(request: Request, store: &DiagnosticStore) -> Response {
    let id = request.id;
    match request.method.as_str() {
        "get_support_snapshot" => Response {
            id,
            ok: true,
            result: Some(store.support_snapshot().await),
            error: None,
        },
        "list_captures" => Response {
            id,
            ok: true,
            result: Some(json!(store.captures().await)),
            error: None,
        },
        "get_events" => Response {
            id,
            ok: true,
            result: Some(json!(
                store
                    .since(request.after_sequence, request.limit.unwrap_or(250))
                    .await
            )),
            error: None,
        },
        "get_timeline" => match request.capture_id {
            Some(capture_id) => match store.timeline(&capture_id).await {
                Ok(value) => Response {
                    id,
                    ok: true,
                    result: Some(json!(value)),
                    error: None,
                },
                Err(error) => Response {
                    id,
                    ok: false,
                    result: None,
                    error: Some(error),
                },
            },
            None => Response {
                id,
                ok: false,
                result: None,
                error: Some("capture_id required".into()),
            },
        },
        "get_anomalies" => match request.capture_id {
            Some(capture_id) => match store.anomalies(&capture_id).await {
                Ok(value) => Response {
                    id,
                    ok: true,
                    result: Some(json!(value)),
                    error: None,
                },
                Err(error) => Response {
                    id,
                    ok: false,
                    result: None,
                    error: Some(error),
                },
            },
            None => Response {
                id,
                ok: false,
                result: None,
                error: Some("capture_id required".into()),
            },
        },
        _ => Response {
            id,
            ok: false,
            result: None,
            error: Some("read-only method not found".into()),
        },
    }
}
