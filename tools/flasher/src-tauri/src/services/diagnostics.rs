use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::VecDeque;
use std::sync::Arc;
use std::time::{Instant, SystemTime, UNIX_EPOCH};
use tokio::sync::Mutex;

/// Host-owned diagnostic evidence.  It deliberately knows nothing about serial
/// ports, MQTT, UDP sockets, or device commands: those remain owned by Flasher.
const MAX_EVENTS: usize = 4_000;
const MAX_CAPTURES: usize = 64;
const MAX_RAW_BYTES: usize = 1_024;
const MAX_SNAPSHOT_ARRAY_ITEMS: usize = 500;
const MAX_SNAPSHOT_OBJECT_FIELDS: usize = 128;

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DiagnosticEvent {
    pub sequence: u64,
    pub received_monotonic_ms: u64,
    pub received_unix_ms: u64,
    pub source: String,
    pub transport: String,
    pub event: String,
    pub raw: String,
    pub operation_id: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DiagnosticSnapshot {
    pub next_sequence: u64,
    pub events: Vec<DiagnosticEvent>,
}

#[derive(Clone, Debug, Serialize)]
pub struct CaptureTimeline {
    pub capture: DiagnosticCapture,
    pub events: Vec<DiagnosticEvent>,
}

#[derive(Clone, Debug, Serialize)]
pub struct CaptureAnomalies {
    pub capture_id: String,
    pub anomalies: Vec<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DiagnosticCapture {
    pub id: String,
    pub started_sequence: u64,
    pub started_unix_ms: u64,
    pub gateway_chip_id: String,
    pub remote_address: Option<u8>,
    pub remote_chip_id: Option<String>,
    pub transfer_id: Option<u8>,
    pub target_version: String,
    pub firmware_sha256: String,
}

#[derive(Clone, Debug, Deserialize)]
pub struct DiagnosticInput {
    pub source: String,
    pub transport: String,
    pub event: String,
    pub raw: String,
    pub operation_id: Option<String>,
}

#[derive(Clone)]
pub struct DiagnosticStore {
    inner: Arc<Mutex<DiagnosticStoreInner>>,
}

struct DiagnosticStoreInner {
    started: Instant,
    next_sequence: u64,
    events: VecDeque<DiagnosticEvent>,
    captures: VecDeque<DiagnosticCapture>,
    support_snapshot: Value,
}

impl Default for DiagnosticStore {
    fn default() -> Self {
        Self {
            inner: Arc::new(Mutex::new(DiagnosticStoreInner {
                started: Instant::now(),
                next_sequence: 1,
                events: VecDeque::new(),
                captures: VecDeque::new(),
                support_snapshot: json!({"status":"Flasher has not published support state yet."}),
            })),
        }
    }
}

impl DiagnosticStore {
    pub async fn record(
        &self,
        source: impl Into<String>,
        transport: impl Into<String>,
        event: impl Into<String>,
        raw: impl AsRef<str>,
        operation_id: Option<String>,
    ) {
        let mut inner = self.inner.lock().await;
        let raw = redact_and_bound(raw.as_ref());
        let record = DiagnosticEvent {
            sequence: inner.next_sequence,
            received_monotonic_ms: inner.started.elapsed().as_millis() as u64,
            received_unix_ms: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis() as u64,
            source: source.into(),
            transport: transport.into(),
            event: event.into(),
            raw,
            operation_id,
        };
        inner.next_sequence += 1;
        inner.events.push_back(record);
        while inner.events.len() > MAX_EVENTS {
            inner.events.pop_front();
        }
    }

    pub async fn since(&self, after_sequence: Option<u64>, limit: usize) -> DiagnosticSnapshot {
        let inner = self.inner.lock().await;
        let after = after_sequence.unwrap_or(0);
        let events = inner
            .events
            .iter()
            .filter(|item| item.sequence > after)
            .take(limit.min(MAX_EVENTS))
            .cloned()
            .collect();
        DiagnosticSnapshot {
            next_sequence: inner.next_sequence,
            events,
        }
    }

    pub async fn begin_capture(&self, mut capture: DiagnosticCapture) -> DiagnosticCapture {
        let mut inner = self.inner.lock().await;
        capture.started_sequence = inner.next_sequence;
        capture.started_unix_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64;
        if capture.id.is_empty() {
            capture.id = format!("ota-{}", capture.started_sequence);
        }
        inner.captures.push_back(capture.clone());
        while inner.captures.len() > MAX_CAPTURES {
            inner.captures.pop_front();
        }
        capture
    }

    pub async fn captures(&self) -> Vec<DiagnosticCapture> {
        self.inner.lock().await.captures.iter().cloned().collect()
    }

    pub async fn update_capture_transfer(&self, id: &str, transfer_id: u8) -> Result<(), String> {
        let mut inner = self.inner.lock().await;
        let capture = inner
            .captures
            .iter_mut()
            .find(|capture| capture.id == id)
            .ok_or_else(|| "diagnostic capture not found".to_string())?;
        capture.transfer_id = Some(transfer_id);
        Ok(())
    }

    pub async fn timeline(&self, id: &str) -> Result<CaptureTimeline, String> {
        let inner = self.inner.lock().await;
        let capture = inner
            .captures
            .iter()
            .find(|capture| capture.id == id)
            .cloned()
            .ok_or_else(|| "diagnostic capture not found".to_string())?;
        // Events without an operation ID are passive network evidence. Preserve them
        // in the bounded time window rather than pretending they are authoritative.
        let events = inner
            .events
            .iter()
            .filter(|event| event.sequence >= capture.started_sequence)
            .cloned()
            .collect();
        Ok(CaptureTimeline { capture, events })
    }

    pub async fn anomalies(&self, id: &str) -> Result<CaptureAnomalies, String> {
        let timeline = self.timeline(id).await?;
        let own = timeline
            .events
            .iter()
            .filter(|event| event.operation_id.as_deref() == Some(id));
        let events: Vec<&str> = own.map(|event| event.event.as_str()).collect();
        let has = |name: &str| events.iter().any(|event| *event == name);
        let mut anomalies = Vec::new();
        if has("ota_command_accepted")
            && !has("ota_downloading")
            && !has("ota_failed")
            && !has("ota_unconfirmed")
        {
            anomalies.push("gateway accepted command but no manifest acceptance evidence".into());
        }
        if has("ota_downloading")
            && !has("ota_updated")
            && !has("ota_failed")
            && !has("ota_no_reboot")
        {
            anomalies.push("manifest accepted but no terminal update evidence yet".into());
        }
        if has("ota_updated") && !has("ota_downloading") {
            anomalies.push("version confirmation without recorded manifest acceptance".into());
        }
        Ok(CaptureAnomalies {
            capture_id: id.to_string(),
            anomalies,
        })
    }

    /// Accept only a bounded, redacted projection assembled by Flasher. This is
    /// deliberately data-only: publishing a snapshot must not cause I/O or a
    /// device refresh.
    pub async fn set_support_snapshot(&self, snapshot: Value) {
        self.inner.lock().await.support_snapshot = sanitize_support_value(snapshot, false);
    }
    pub async fn support_snapshot(&self) -> Value {
        self.inner.lock().await.support_snapshot.clone()
    }
}

fn redact_and_bound(raw: &str) -> String {
    let mut value = raw.to_string();
    for key in ["admin_password", "password", "fleet_key", "mqtt_password"] {
        if let Some(start) = value.find(&format!("{key}=")) {
            let end = value[start..]
                .find(char::is_whitespace)
                .map(|n| start + n)
                .unwrap_or(value.len());
            value.replace_range(start..end, &format!("{key}=<redacted>"));
        }
    }
    if value.len() > MAX_RAW_BYTES {
        value.truncate(MAX_RAW_BYTES);
        value.push_str("…<truncated>");
    }
    value
}

fn is_secret_key(key: &str) -> bool {
    let key = key.to_ascii_lowercase();
    [
        "password",
        "secret",
        "credential",
        "fleet_key",
        "mqtt_key",
        "auth_token",
    ]
    .iter()
    .any(|needle| key.contains(needle))
}

fn sanitize_support_value(value: Value, inherited_secret: bool) -> Value {
    if inherited_secret {
        return Value::String("<redacted>".into());
    }
    match value {
        Value::String(text) => Value::String(redact_and_bound(&text)),
        Value::Array(items) => Value::Array(
            items
                .into_iter()
                .take(MAX_SNAPSHOT_ARRAY_ITEMS)
                .map(|item| sanitize_support_value(item, false))
                .collect(),
        ),
        Value::Object(entries) => Value::Object(
            entries
                .into_iter()
                .take(MAX_SNAPSHOT_OBJECT_FIELDS)
                .map(|(key, value)| {
                    let secret = is_secret_key(&key);
                    (key, sanitize_support_value(value, secret))
                })
                .collect(),
        ),
        other => other,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn records_monotonic_events_and_redacts_passwords() {
        let store = DiagnosticStore::default();
        store
            .record(
                "remote:1",
                "udp",
                "boot",
                "event=boot password=secret",
                None,
            )
            .await;
        let events = store.since(None, 10).await.events;
        assert_eq!(events[0].sequence, 1);
        assert!(events[0].raw.contains("password=<redacted>"));
        assert!(!events[0].raw.contains("secret"));
    }

    #[tokio::test]
    async fn identifies_missing_manifest_after_command_acceptance() {
        let store = DiagnosticStore::default();
        let capture = store
            .begin_capture(DiagnosticCapture {
                id: "capture".into(),
                started_sequence: 0,
                started_unix_ms: 0,
                gateway_chip_id: "gw".into(),
                remote_address: Some(1),
                remote_chip_id: None,
                transfer_id: None,
                target_version: "0.10.5".into(),
                firmware_sha256: "hash".into(),
            })
            .await;
        store
            .record(
                "gateway",
                "usb",
                "ota_command_accepted",
                "",
                Some(capture.id.clone()),
            )
            .await;
        assert_eq!(
            store.anomalies(&capture.id).await.unwrap().anomalies.len(),
            1
        );
    }

    #[tokio::test]
    async fn support_snapshot_is_redacted_and_bounded_at_ingestion() {
        let store = DiagnosticStore::default();
        store
            .set_support_snapshot(json!({
                "mqtt_password": "do-not-export",
                "logs": [{ "raw": "password=also-hidden" }],
                "remotes": (0..600).collect::<Vec<_>>(),
            }))
            .await;
        let snapshot = store.support_snapshot().await;
        assert_eq!(snapshot["mqtt_password"], "<redacted>");
        assert_eq!(snapshot["logs"][0]["raw"], "password=<redacted>");
        assert_eq!(
            snapshot["remotes"].as_array().unwrap().len(),
            MAX_SNAPSHOT_ARRAY_ITEMS
        );
    }
}
