use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone, Default)]
pub struct SerialPortCoordinator {
    busy: Arc<Mutex<HashMap<String, String>>>,
}

pub struct SerialPortGuard {
    port: String,
    owner: String,
    busy: Arc<Mutex<HashMap<String, String>>>,
}

impl SerialPortCoordinator {
    pub async fn acquire(
        &self,
        port: &str,
        owner: &str,
        timeout: Duration,
    ) -> Result<SerialPortGuard, String> {
        let started = Instant::now();
        loop {
            {
                let mut busy = self
                    .busy
                    .lock()
                    .map_err(|_| "serial port coordinator lock poisoned".to_string())?;
                if !busy.contains_key(port) {
                    busy.insert(port.to_string(), owner.to_string());
                    return Ok(SerialPortGuard {
                        port: port.to_string(),
                        owner: owner.to_string(),
                        busy: self.busy.clone(),
                    });
                }
            }

            if started.elapsed() >= timeout {
                let current = self.owner(port).unwrap_or_else(|| "another operation".into());
                return Err(format!("USB port {} is busy: {}", port, current));
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }

    pub fn owner(&self, port: &str) -> Option<String> {
        self.busy
            .lock()
            .ok()
            .and_then(|busy| busy.get(port).cloned())
    }
}

impl Drop for SerialPortGuard {
    fn drop(&mut self) {
        if let Ok(mut busy) = self.busy.lock() {
            if busy.get(&self.port) == Some(&self.owner) {
                busy.remove(&self.port);
            }
        }
    }
}
