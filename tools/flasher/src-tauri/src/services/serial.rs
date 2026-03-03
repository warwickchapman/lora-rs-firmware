use serde::{Deserialize, Serialize};
use serialport::available_ports;

#[derive(Serialize, Deserialize, Clone)]
pub struct SerialPortInfo {
    pub port_name: String,
    pub description: Option<String>,
    pub product: Option<String>,
    pub manufacturer: Option<String>,
    pub score: i32,
}

pub fn list_ports() -> Vec<SerialPortInfo> {
    available_ports()
        .unwrap_or_default()
        .into_iter()
        .filter_map(|p| {
            let name = p.port_name.to_lowercase();
            
            // 1. Filter out redundant/noisy ports
            #[cfg(target_os = "macos")]
            if name.contains("cu.") || name.contains("bluetooth") || name.contains("debug-console") {
                return None;
            }

            #[cfg(target_os = "linux")]
            if name.contains("rfcomm") || (name.contains("ttys") && !name.contains("usb")) {
                return None;
            }

            // 2. Extract USB metadata and calculate priority score
            let mut score = 0;
            let (product, manufacturer) = match p.port_type {
                serialport::SerialPortType::UsbPort(info) => {
                    score += 50; // It's a USB device
                    
                    let prod = info.product.clone();
                    let manu = info.manufacturer.clone();
                    
                    let prod_lower = prod.as_deref().unwrap_or("").to_lowercase();
                    let manu_lower = manu.as_deref().unwrap_or("").to_lowercase();

                    // Known ESP-related chips/manufacturers
                    if prod_lower.contains("cp210") || prod_lower.contains("ch34") || prod_lower.contains("usb serial") {
                        score += 50;
                    }
                    if manu_lower.contains("espressif") || manu_lower.contains("silicon labs") || manu_lower.contains("qinheng") {
                        score += 50;
                    }
                    
                    (prod, manu)
                }
                _ => (None, None),
            };

            // 3. Platform-specific name scoring
            #[cfg(target_os = "macos")]
            if name.contains("usbserial") {
                score += 100;
            } else if name.contains("usb") {
                score += 50;
            }

            #[cfg(target_os = "linux")]
            if name.contains("ttyusb") {
                score += 100;
            } else if name.contains("ttyacm") {
                score += 50;
            }

            #[cfg(target_os = "windows")]
            if name.starts_with("com") {
                score += 10; // Basic COM port score
                // Add a small boost for higher port numbers as they are usually newer/external
                if let Ok(num) = name.replace("com", "").parse::<i32>() {
                    score += num;
                }
            }

            Some(SerialPortInfo {
                port_name: p.port_name,
                description: match (product.clone(), manufacturer.clone()) {
                    (Some(p), Some(m)) => Some(format!("{} ({})", p, m)),
                    (Some(p), None) => Some(p),
                    _ => None,
                },
                product,
                manufacturer,
                score,
            })
        })
        .collect();

    ports.sort_by(|a, b| b.score.cmp(&a.score));
    ports
}
