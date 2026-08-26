use serde_json::{json, Value};
use std::fs;
use std::io::{self, BufRead, BufReader, Write};
#[cfg(unix)]
use std::os::unix::net::UnixStream;
use std::path::PathBuf;

#[cfg(unix)]
fn endpoint() -> (PathBuf, String) {
    let dir = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join(".thanda-lora-flasher-diagnostics");
    let token = fs::read_to_string(dir.join("token"))
        .expect("Flasher is not running or diagnostics are unavailable");
    (dir.join("flasher.sock"), token)
}

#[cfg(unix)]
fn query(method: &str, args: &Value) -> Result<Value, String> {
    let (socket, token) = endpoint();
    let mut stream = UnixStream::connect(socket).map_err(|error| error.to_string())?;
    let mut request = json!({"token": token, "method": method, "id": "mcp"});
    if let (Some(request), Some(args)) = (request.as_object_mut(), args.as_object()) {
        for (key, value) in args {
            request.insert(key.clone(), value.clone());
        }
    }
    writeln!(stream, "{request}").map_err(|error| error.to_string())?;
    let mut line = String::new();
    BufReader::new(stream)
        .read_line(&mut line)
        .map_err(|error| error.to_string())?;
    let response: Value = serde_json::from_str(&line).map_err(|error| error.to_string())?;
    if response["ok"] == true {
        Ok(response["result"].clone())
    } else {
        Err(response["error"]
            .as_str()
            .unwrap_or("IPC request failed")
            .into())
    }
}

#[cfg(not(unix))]
fn query(_method: &str, _args: &Value) -> Result<Value, String> {
    Err("Flasher diagnostics MCP is only supported on Unix platforms".into())
}

fn cached_tool(name: &str, description: &str, properties: Value, required: Value) -> Value {
    json!({
        "name": name,
        "description": format!("{description} Cached Flasher state only; never refreshes devices or sends traffic."),
        "inputSchema": {"type": "object", "properties": properties, "required": required}
    })
}

fn tools() -> Value {
    json!([
        cached_tool("flasher_status", "Read Flasher's current cached support summary.", json!({}), json!([])),
        cached_tool("get_gateway_snapshot", "Read the selected gateway's cached session and operational fields.", json!({}), json!([])),
        cached_tool("list_gateways", "List gateways already discovered by Flasher over MQTT or selected over USB.", json!({}), json!([])),
        cached_tool("list_remote_devices", "List cached remote inventory rows from the Fleet pane.", json!({}), json!([])),
        cached_tool("get_remote_snapshot", "Read one cached remote inventory row by LoRa address.", json!({"address": {"type": "integer", "minimum": 1, "maximum": 255}}), json!(["address"])),
        cached_tool("list_operations", "Read cached Fleet/flash/OTA operation status.", json!({}), json!([])),
        cached_tool("get_log_sources", "List log sources represented in Flasher's retained log cache.", json!({}), json!([])),
        cached_tool("get_log_events", "Read up to 200 retained UI log records, optionally filtered by source.", json!({"source": {"type": "string"}}), json!([])),
        cached_tool("list_mqtt_config_buffers", "List cached MQTT configuration-buffer metadata for discovered gateways.", json!({}), json!([])),
        cached_tool("get_mqtt_config_snapshot", "Read one redacted cached MQTT configuration snapshot. Defaults to the selected MQTT gateway.", json!({"gateway_chip_id": {"type": "string"}}), json!([])),
        {"name":"list_captures","description":"List bounded Flasher OTA diagnostic captures.","inputSchema":{"type":"object","properties":{}}},
        {"name":"get_events","description":"Read bounded host diagnostic events after a sequence number.","inputSchema":{"type":"object","properties":{"after_sequence":{"type":"integer"},"limit":{"type":"integer"}}}},
        {"name":"get_timeline","description":"Read one OTA capture timeline, including passive evidence.","inputSchema":{"type":"object","properties":{"capture_id":{"type":"string"}},"required":["capture_id"]}},
        {"name":"get_anomalies","description":"Read deterministic anomaly summaries for an OTA capture.","inputSchema":{"type":"object","properties":{"capture_id":{"type":"string"}},"required":["capture_id"]}}
    ])
}

fn support_result(name: &str, args: &Value) -> Result<Value, String> {
    let snapshot = query("get_support_snapshot", &json!({}))?;
    match name {
        "flasher_status" => Ok(json!({
            "observed_at": snapshot["observed_at"],
            "selected_transport": snapshot["selected_transport"],
            "gateway": snapshot["gateway"],
            "operations": snapshot["operations"],
            "remote_count": snapshot["remotes"].as_array().map_or(0, Vec::len),
            "retained_log_count": snapshot["logs"].as_array().map_or(0, Vec::len),
        })),
        "get_gateway_snapshot" => Ok(snapshot["gateway"].clone()),
        "list_gateways" => Ok(snapshot["gateways"].clone()),
        "list_remote_devices" => Ok(snapshot["remotes"].clone()),
        "get_remote_snapshot" => {
            let address = args["address"].as_u64().ok_or("address required")?;
            snapshot["remotes"]
                .as_array()
                .and_then(|remotes| {
                    remotes
                        .iter()
                        .find(|remote| remote["address"].as_u64() == Some(address))
                        .cloned()
                })
                .ok_or_else(|| {
                    format!("remote {address} is not present in Flasher's cached inventory")
                })
        }
        "list_operations" => Ok(snapshot["operations"].clone()),
        "get_log_sources" => {
            let mut sources = Vec::new();
            for log in snapshot["logs"].as_array().into_iter().flatten() {
                if let Some(source) = log["source"].as_str() {
                    if !sources.iter().any(|known: &Value| known == source) {
                        sources.push(json!(source));
                    }
                }
            }
            Ok(json!(sources))
        }
        "get_log_events" => {
            let source = args["source"].as_str();
            let logs = snapshot["logs"]
                .as_array()
                .map(|logs| {
                    logs.iter()
                        .filter(|log| source.map_or(true, |source| log["source"] == source))
                        .cloned()
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();
            Ok(json!(logs))
        }
        "list_mqtt_config_buffers" => Ok(snapshot["mqtt_config_buffers"].clone()),
        "get_mqtt_config_snapshot" => {
            let raw_chip_id = args["gateway_chip_id"].as_str()
                .or_else(|| snapshot["gateway"]["target"].as_str())
                .ok_or("gateway_chip_id required when no MQTT gateway is selected")?;
            let chip_id = canonical_chip_id(raw_chip_id);
            snapshot["mqtt_config_buffers"][&chip_id]
                .as_object()
                .cloned()
                .map(Value::Object)
                .ok_or_else(|| format!("no cached MQTT configuration buffer for gateway {chip_id}"))
        }
        _ => Err("read-only tool not found".into()),
    }
}

fn call_tool(name: &str, args: &Value) -> Result<Value, String> {
    match name {
        "flasher_status"
        | "get_gateway_snapshot"
        | "list_gateways"
        | "list_remote_devices"
        | "get_remote_snapshot"
        | "list_operations"
        | "get_log_sources"
        | "get_log_events"
        | "list_mqtt_config_buffers"
        | "get_mqtt_config_snapshot" => support_result(name, args),
        "list_captures" => query("list_captures", args),
        "get_events" => query("get_events", args),
        "get_timeline" => query("get_timeline", args),
        "get_anomalies" => query("get_anomalies", args),
        _ => Err("read-only tool not found".into()),
    }
}

fn canonical_chip_id(raw: &str) -> String {
    raw.trim().trim_start_matches("lrs-").trim_start_matches("0x").to_ascii_lowercase()
}

fn main() {
    for line in io::stdin().lock().lines().flatten() {
        let request: Value = match serde_json::from_str(&line) {
            Ok(value) => value,
            Err(_) => continue,
        };
        let id = request["id"].clone();
        let result = match request["method"].as_str().unwrap_or("") {
            "initialize" => {
                json!({"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"flasher-diagnostics","version":"0.10.5"}})
            }
            "tools/list" => json!({"tools": tools()}),
            "tools/call" => match call_tool(
                request["params"]["name"].as_str().unwrap_or(""),
                &request["params"]["arguments"],
            ) {
                Ok(value) => json!({"content":[{"type":"text","text":value.to_string()}]}),
                Err(error) => json!({"content":[{"type":"text","text":error}],"isError":true}),
            },
            _ => json!(null),
        };
        println!("{}", json!({"jsonrpc":"2.0","id":id,"result":result}));
    }
}
