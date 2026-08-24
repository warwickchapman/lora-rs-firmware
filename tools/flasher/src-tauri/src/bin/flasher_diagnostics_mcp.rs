use serde_json::{json, Value};
use std::fs;
use std::io::{self, BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;

fn endpoint() -> (PathBuf, String) {
    let dir = std::env::temp_dir().join("thanda-lora-flasher-diagnostics");
    let token = fs::read_to_string(dir.join("token")).expect("Flasher is not running or diagnostics are unavailable");
    (dir.join("flasher.sock"), token)
}
fn query(method: &str, args: &Value) -> Result<Value, String> {
    let (socket, token) = endpoint(); let mut stream = UnixStream::connect(socket).map_err(|e| e.to_string())?;
    let mut request = json!({"token":token,"method":method,"id":"mcp"});
    if let Some(map) = request.as_object_mut() { if let Some(args)=args.as_object(){ for (k,v) in args { map.insert(k.clone(),v.clone()); } } }
    writeln!(stream, "{}", request).map_err(|e| e.to_string())?;
    let mut line=String::new(); BufReader::new(stream).read_line(&mut line).map_err(|e| e.to_string())?;
    let response:Value=serde_json::from_str(&line).map_err(|e| e.to_string())?;
    if response["ok"]==true { Ok(response["result"].clone()) } else { Err(response["error"].as_str().unwrap_or("IPC request failed").into()) }
}
fn tools() -> Value { json!([{"name":"list_captures","description":"List bounded Flasher OTA diagnostic captures.","inputSchema":{"type":"object","properties":{}}},{"name":"get_events","description":"Read bounded host diagnostic events after a sequence number.","inputSchema":{"type":"object","properties":{"after_sequence":{"type":"integer"},"limit":{"type":"integer"}}}},{"name":"get_timeline","description":"Read one OTA capture timeline, including passive evidence.","inputSchema":{"type":"object","properties":{"capture_id":{"type":"string"}},"required":["capture_id"]}},{"name":"get_anomalies","description":"Read deterministic anomaly summaries for an OTA capture.","inputSchema":{"type":"object","properties":{"capture_id":{"type":"string"}},"required":["capture_id"]}}]) }
fn main() { for line in io::stdin().lock().lines().flatten() { let request:Value=match serde_json::from_str(&line){Ok(v)=>v,Err(_)=>continue}; let id=request["id"].clone(); let method=request["method"].as_str().unwrap_or(""); let result=match method { "initialize"=>json!({"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"flasher-diagnostics","version":"0.10.5"}}), "tools/list"=>json!({"tools":tools()}), "tools/call"=>{let name=request["params"]["name"].as_str().unwrap_or(""); let args=&request["params"]["arguments"]; let ipc=match name {"list_captures"=>"list_captures","get_events"=>"get_events","get_timeline"=>"get_timeline","get_anomalies"=>"get_anomalies",_=>""}; match query(ipc,args){Ok(v)=>json!({"content":[{"type":"text","text":v.to_string()}]}),Err(e)=>json!({"content":[{"type":"text","text":e}],"isError":true})}}, _=>json!(null)}; println!("{}",json!({"jsonrpc":"2.0","id":id,"result":result})); } }
