use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
use std::net::Ipv4Addr;

const UDP_LOG_PORT: u16 = 5514;

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct LanSubnet {
    pub interface_name: String,
    pub address: String,
    pub netmask: String,
    pub cidr: u8,
    pub network: String,
    pub broadcast: String,
    pub host_count: u32,
}

pub fn udp_log_port() -> u16 {
    UDP_LOG_PORT
}

pub fn local_lan_subnets() -> Result<Vec<LanSubnet>, String> {
    let interfaces = get_if_addrs::get_if_addrs().map_err(|e| e.to_string())?;
    let mut seen = BTreeSet::new();
    let mut subnets = Vec::new();

    for iface in interfaces {
        let get_if_addrs::IfAddr::V4(v4) = iface.addr else {
            continue;
        };

        let ip = v4.ip;
        let mask = v4.netmask;
        if ip.is_loopback() || ip.is_link_local() || ip.octets()[0] == 0 {
            continue;
        }

        let mask_u32 = u32::from(mask);
        if mask_u32 == 0 || mask_u32 == u32::MAX {
            continue;
        }
        let network = u32::from(ip) & mask_u32;
        let broadcast = network | !mask_u32;
        if broadcast <= network + 1 {
            continue;
        }
        let key = (network, mask_u32);
        if !seen.insert(key) {
            continue;
        }
        let cidr = mask_u32.count_ones() as u8;
        subnets.push(LanSubnet {
            interface_name: iface.name,
            address: ip.to_string(),
            netmask: mask.to_string(),
            cidr,
            network: Ipv4Addr::from(network).to_string(),
            broadcast: Ipv4Addr::from(broadcast).to_string(),
            host_count: broadcast.saturating_sub(network).saturating_sub(1),
        });
    }

    if subnets.is_empty() {
        return Err("No usable IPv4 LAN subnet found".into());
    }
    Ok(subnets)
}
