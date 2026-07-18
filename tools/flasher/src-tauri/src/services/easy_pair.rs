use serde_json::{json, Value};
use std::io::{Read, Write};
use std::time::{Duration, Instant};

const SERIAL_ADMIN_BAUD: u32 = 115_200;
const SERIAL_ADMIN_PREFIX: &str = "LRS:";

pub fn send_serial_admin_command(
    port_name: &str,
    mut request: Value,
    timeout_ms: u64,
    mut on_log_line: impl FnMut(&str),
) -> Result<Value, String> {
    let id = ensure_request_id(&mut request);
    let cmd = request
        .get("cmd")
        .or_else(|| request.get("command"))
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string();
    if cmd.is_empty() {
        return Err("missing cmd".into());
    }

    let timeout = Duration::from_millis(timeout_ms.max(500));
    let mut port = serialport::new(port_name, SERIAL_ADMIN_BAUD)
        .timeout(Duration::from_millis(150))
        .open()
        .map_err(|e| format!("open serial port failed: {}", e))?;

    let line = format!(
        "{}{}\n",
        SERIAL_ADMIN_PREFIX,
        serde_json::to_string(&request).map_err(|e| e.to_string())?
    );
    port.write_all(line.as_bytes())
        .map_err(|e| format!("write serial command failed: {}", e))?;
    port.flush()
        .map_err(|e| format!("flush serial command failed: {}", e))?;

    let started = Instant::now();
    let mut line: Vec<u8> = Vec::with_capacity(256);
    let mut buf = [0_u8; 128];
    while started.elapsed() < timeout {
        match port.read(&mut buf) {
            Ok(0) => continue,
            Ok(n) => {
                for byte in &buf[..n] {
                    if *byte == b'\n' || *byte == b'\r' {
                        if let Some(parsed) = parse_serial_admin_line(&line) {
                            if !response_matches(&parsed, &cmd, &id) {
                                line.clear();
                                continue;
                            }
                            if parsed.get("ok").and_then(Value::as_bool) == Some(false) {
                                let error = parsed
                                    .get("error")
                                    .and_then(Value::as_str)
                                    .unwrap_or("serial_admin_error");
                                if error == "unknown_cmd" {
                                    return Err(format!(
                                        "{} failed: unknown_cmd - Flash this gateway with the latest firmware and try again.",
                                        human_command_label(&cmd)
                                    ));
                                }
                                if let Some(detail) = parsed.get("detail").and_then(Value::as_str) {
                                    return Err(format!("{}: {}", error, detail));
                                }
                                return Err(error.to_string());
                            }
                            return Ok(parsed);
                        } else {
                            emit_non_admin_line(&line, &mut on_log_line);
                        }
                        line.clear();
                    } else {
                        line.push(*byte);
                        if line.len() > 32768 {
                            line.clear();
                        }
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => return Err(format!("read serial response failed: {}", e)),
        }
    }

    Err(format!("timed out waiting for {} response", cmd))
}

fn emit_non_admin_line(line: &[u8], on_log_line: &mut impl FnMut(&str)) {
    if line.is_empty() {
        return;
    }
    if line
        .windows(SERIAL_ADMIN_PREFIX.len())
        .any(|w| w == SERIAL_ADMIN_PREFIX.as_bytes())
    {
        return;
    }
    let text = String::from_utf8_lossy(line);
    let trimmed = text.trim();
    if !trimmed.is_empty() {
        on_log_line(trimmed);
    }
}

fn parse_serial_admin_line(line: &[u8]) -> Option<Value> {
    let start = line
        .windows(SERIAL_ADMIN_PREFIX.len())
        .position(|w| w == SERIAL_ADMIN_PREFIX.as_bytes())?;
    let json_bytes = &line[start + SERIAL_ADMIN_PREFIX.len()..];
    serde_json::from_slice(json_bytes).ok()
}

fn human_command_label(cmd: &str) -> String {
    cmd.split('_')
        .filter(|part| !part.is_empty())
        .map(|part| {
            let mut chars = part.chars();
            match chars.next() {
                Some(first) => first.to_uppercase().collect::<String>() + chars.as_str(),
                None => String::new(),
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

fn ensure_request_id(request: &mut Value) -> String {
    if let Some(id) = request.get("id").and_then(Value::as_str) {
        if !id.is_empty() {
            return id.to_string();
        }
    }
    let generated = format!(
        "flasher-{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis())
            .unwrap_or(0)
    );
    if let Some(obj) = request.as_object_mut() {
        obj.insert("id".into(), json!(generated.clone()));
    }
    generated
}

fn response_matches(response: &Value, cmd: &str, id: &str) -> bool {
    let response_cmd = response.get("cmd").and_then(Value::as_str).unwrap_or("");
    let response_id = response.get("id").and_then(Value::as_str).unwrap_or("");
    response_cmd == cmd && response_id == id
}
