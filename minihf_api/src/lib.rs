use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};
use std::io::{Read, Write};
use serialport::SerialPort;
use std::sync::atomic::{AtomicU16, Ordering};

use cobs::{encode, decode_vec, max_encoding_length};
use crc::{Crc, CRC_16_KERMIT};

const RESP_ACK: u8 = 0xFF;
const RESP_NACK: u8 = 0xFE;
const HEADER_BYTE: u8 = 0xAA;
const HEADER_SIZE: usize = 5;

uniffi::include_scaffolding!("minihf_api");

#[derive(Debug, thiserror::Error, uniffi::Error)]
pub enum MiniHFError {
    #[error("Serial error: {0}")]
    Serial(String),
    #[error("Timeout waiting for response")]
    Timeout,
    #[error("Received NACK from device")]
    Nack,
    #[error("Invalid packet received")]
    InvalidPacket,
    #[error("I/O error: {0}")]
    Io(String),
    #[error("Invalid argument: {0}")]
    InvalidArgument(String),
    #[error("Port is closed")]
    PortClosed,
}

trait Transport: Read + Write + Send {
    fn set_timeout(&mut self, timeout: Duration) -> Result<(), MiniHFError>;
    fn bytes_available(&self) -> Result<usize, MiniHFError>;
}

struct SerialTransport(Box<dyn SerialPort>);

impl Transport for SerialTransport {
    fn set_timeout(&mut self, timeout: Duration) -> Result<(), MiniHFError> {
        self.0
            .set_timeout(timeout)
            .map_err(|e| MiniHFError::Io(e.to_string()))
    }
    fn bytes_available(&self) -> Result<usize, MiniHFError> {
        self.0
            .bytes_to_read()
            .map(|n| n as usize)
            .map_err(|e| MiniHFError::Io(e.to_string()))
    }
}

impl Read for SerialTransport {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.0.read(buf)
    }
}

impl Write for SerialTransport {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.write(buf)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.0.flush()
    }
}

struct FdTransport {
    file: std::fs::File,
    timeout: Duration,
}

impl FdTransport {
    #[cfg(unix)]
    fn new(fd: i32, timeout: Duration) -> Self {
        use std::os::unix::io::FromRawFd;
        // Safety: caller transfers ownership of the fd — it will be closed on drop.
        let file = unsafe { std::fs::File::from_raw_fd(fd) };
        Self { file, timeout }
    }
}

impl Transport for FdTransport {
    fn set_timeout(&mut self, timeout: Duration) -> Result<(), MiniHFError> {
        self.timeout = timeout;
        Ok(())
    }

    fn bytes_available(&self) -> Result<usize, MiniHFError> {
        #[cfg(unix)]
        {
            use std::os::unix::io::AsRawFd;
            let mut count: libc::c_int = 0;
            let ret = unsafe {
                libc::ioctl(self.file.as_raw_fd(), libc::FIONREAD, &mut count)
            };
            if ret != 0 {
                return Err(MiniHFError::Io(
                    std::io::Error::last_os_error().to_string(),
                ));
            }
            Ok(count.max(0) as usize)
        }
        #[cfg(not(unix))]
        {
            Ok(0)
        }
    }
}

impl Read for FdTransport {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        #[cfg(unix)]
        {
            use std::os::unix::io::AsRawFd;
            let mut pfd = libc::pollfd {
                fd: self.file.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            };
            let timeout_ms = self.timeout.as_millis().min(i32::MAX as u128) as i32;
            let ret = unsafe { libc::poll(&mut pfd, 1, timeout_ms) };
            if ret < 0 {
                return Err(std::io::Error::last_os_error());
            }
            if ret == 0 {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::TimedOut,
                    "timed out waiting for data",
                ));
            }
        }
        self.file.read(buf)
    }
}

impl Write for FdTransport {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.file.write(buf)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.file.flush()
    }
}

#[derive(uniffi::Record)]
pub struct RtcTime {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}

#[derive(uniffi::Object)]
pub struct MiniHF {
    port: Arc<Mutex<Option<Box<dyn Transport>>>>,
    next_id: AtomicU16,
    timeout: Duration,
    rx_buf: Mutex<Vec<u8>>,
}

#[uniffi::export]
impl MiniHF {
    #[uniffi::constructor]
    pub fn new(path: String, baud: u32) -> Result<Arc<Self>, MiniHFError> {
        Self::new_with_timeout(path, baud, 2000)
    }

    #[uniffi::constructor]
    pub fn new_with_timeout(path: String, baud: u32, timeout_ms: u64) -> Result<Arc<Self>, MiniHFError> {
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(timeout_ms))
            .open()
            .map_err(|e| MiniHFError::Io(e.to_string()))?;

        Ok(Arc::new(Self {
            port: Arc::new(Mutex::new(Some(Box::new(SerialTransport(port))))),
            next_id: AtomicU16::new(1),
            timeout: Duration::from_millis(timeout_ms),
            rx_buf: Mutex::new(Vec::new()),
        }))
    }

    #[uniffi::constructor]
    pub fn from_fd(fd: i32, timeout_ms: u64) -> Result<Arc<Self>, MiniHFError> {
        #[cfg(not(unix))]
        {
            let _ = (fd, timeout_ms);
            return Err(MiniHFError::Io("from_fd is only supported on Unix".into()));
        }

        #[cfg(unix)]
        {
            let transport = FdTransport::new(fd, Duration::from_millis(timeout_ms));
            Ok(Arc::new(Self {
                port: Arc::new(Mutex::new(Some(Box::new(transport)))),
                next_id: AtomicU16::new(1),
                timeout: Duration::from_millis(timeout_ms),
                rx_buf: Mutex::new(Vec::new()),
            }))
        }
    }

    pub fn set_rtc_time(&self, time: RtcTime) -> Result<(), MiniHFError> {
        let mut payload = Vec::new();
        payload.extend_from_slice(&time.year.to_le_bytes());
        payload.push(time.month);
        payload.push(time.day);
        payload.push(time.hour);
        payload.push(time.minute);
        payload.push(time.second);
        self.transact(0x01, payload)?;
        Ok(())
    }

    pub fn get_rtc_time(&self) -> Result<RtcTime, MiniHFError> {
        let resp = self.transact(0x02, vec![])?;
        if resp.len() < 7 { return Err(MiniHFError::InvalidPacket); }
        let year = u16::from_le_bytes([resp[0], resp[1]]);
        Ok(RtcTime {
            year,
            month: resp[2],
            day: resp[3],
            hour: resp[4],
            minute: resp[5],
            second: resp[6],
        })
    }

    pub fn set_base_freq(&self, freq_hz: f64) -> Result<(), MiniHFError> {
        if freq_hz.is_nan() || freq_hz.is_infinite() || freq_hz < 0.0 {
            return Err(MiniHFError::InvalidArgument(
                format!("frequency must be a finite non-negative number, got {}", freq_hz),
            ));
        }
        let freq_int = (freq_hz * 100.0).round() as u64;
        let payload = freq_int.to_le_bytes().to_vec();
        self.transact(0x03, payload)?;
        Ok(())
    }

    pub fn get_base_freq(&self) -> Result<f64, MiniHFError> {
        let resp = self.transact(0x04, vec![])?;
        if resp.len() < 8 { return Err(MiniHFError::InvalidPacket); }
        
        let mut bytes = [0u8; 8];
        bytes.copy_from_slice(&resp[0..8]);
        let freq_int = u64::from_le_bytes(bytes);
        Ok(freq_int as f64 / 100.0)
    }

    pub fn reset(&self) -> Result<(), MiniHFError> {
        self.send_only(0xFD, vec![])?;
        Ok(())
    }

    pub fn close(&self) -> Result<(), MiniHFError> {
        let mut port_opt = self.port.lock().unwrap();
        if let Some(ref mut port) = *port_opt {
            let _ = port.flush();
        }
        *port_opt = None;
        let mut rx_buf = self.rx_buf.lock().unwrap();
        rx_buf.clear();
        Ok(())
    }
}

impl MiniHF {
    fn send_only(&self, cmd_id: u8, payload: Vec<u8>) -> Result<(), MiniHFError> {
        if payload.len() > 255 {
            return Err(MiniHFError::InvalidArgument(
                format!("payload too large: {} bytes (max 255)", payload.len()),
            ));
        }
        let current_id = self.next_id.fetch_add(1, Ordering::SeqCst);
        let frame = frame_packet(cmd_id, current_id, &payload);
        let mut port_opt = self.port.lock().unwrap();
        let port = port_opt.as_mut().ok_or(MiniHFError::PortClosed)?;
        port.write_all(&frame).map_err(|e| MiniHFError::Io(e.to_string()))?;
        Ok(())
    }

    fn transact(&self, cmd_id: u8, payload: Vec<u8>) -> Result<Vec<u8>, MiniHFError> {
        if payload.len() > 255 {
            return Err(MiniHFError::InvalidArgument(
                format!("payload too large: {} bytes (max 255)", payload.len()),
            ));
        }
        let current_id = self.next_id.fetch_add(1, Ordering::SeqCst);
        let frame = frame_packet(cmd_id, current_id, &payload);

        let mut port_opt = self.port.lock().unwrap();
        let port = port_opt.as_mut().ok_or(MiniHFError::PortClosed)?;
        port.write_all(&frame).map_err(|e| MiniHFError::Io(e.to_string()))?;

        let deadline = Instant::now() + self.timeout;
        let mut rx_buf = self.rx_buf.lock().unwrap();

        loop {
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                rx_buf.clear();
                return Err(MiniHFError::Timeout);
            }

            port.set_timeout(remaining)?;

            let to_read = port.bytes_available()? ;
            let mut tmp = vec![0u8; to_read.max(1)];

            match port.read(&mut tmp) {
                Ok(n) => rx_buf.extend_from_slice(&tmp[..n]),
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
                Err(e) => return Err(MiniHFError::Io(e.to_string())),
            }

            while let Some(idx) = rx_buf.iter().position(|&b| b == 0x00) {
                let frame_data: Vec<u8> = rx_buf.drain(..=idx).collect();
                let frame_bytes = &frame_data[..frame_data.len() - 1];

                if frame_bytes.is_empty() {
                    continue;
                }

                let decoded = match decode_vec(frame_bytes) {
                    Ok(d) => d,
                    Err(_) => continue,
                };

                if let Some(pkt) = parse_packet(&decoded) {
                    if pkt.id != current_id {
                        continue;
                    }
                    if pkt.ptype == RESP_NACK {
                        return Err(MiniHFError::Nack);
                    }
                    return Ok(pkt.payload);
                }
            }
        }
    }
}

struct ParsedPacket {
    ptype: u8,
    id: u16,
    payload: Vec<u8>,
}

fn build_packet(cmd_id: u8, pkt_id: u16, payload: &[u8]) -> Vec<u8> {
    assert!(payload.len() <= 255, "payload too large for length field: {}", payload.len());
    let mut buf = Vec::new();
    buf.push(HEADER_BYTE);
    buf.push(cmd_id);
    buf.extend_from_slice(&pkt_id.to_le_bytes());
    buf.push(payload.len() as u8);
    buf.extend_from_slice(payload);

    let crc_obj = Crc::<u16>::new(&CRC_16_KERMIT);
    let checksum = crc_obj.checksum(&buf);
    buf.extend_from_slice(&checksum.to_le_bytes());
    buf
}

fn frame_packet(cmd_id: u8, pkt_id: u16, payload: &[u8]) -> Vec<u8> {
    let raw = build_packet(cmd_id, pkt_id, payload);
    let mut encoded = vec![0u8; max_encoding_length(raw.len())];
    let len = encode(&raw, &mut encoded);
    encoded.truncate(len);
    encoded.push(0x00);
    encoded
}

fn parse_packet(raw: &[u8]) -> Option<ParsedPacket> {
    if raw.len() < HEADER_SIZE + 2 {
        return None;
    }
    if raw[0] != HEADER_BYTE {
        return None;
    }
    let ptype = raw[1];
    let id = u16::from_le_bytes([raw[2], raw[3]]);
    let length = raw[4] as usize;
    if raw.len() < HEADER_SIZE + length + 2 {
        return None;
    }
    let payload = raw[HEADER_SIZE..HEADER_SIZE + length].to_vec();

    let crc_obj = Crc::<u16>::new(&CRC_16_KERMIT);
    let crc_calc = crc_obj.checksum(&raw[..HEADER_SIZE + length]);
    let crc_recv = u16::from_le_bytes([
        raw[HEADER_SIZE + length],
        raw[HEADER_SIZE + length + 1],
    ]);
    if crc_calc != crc_recv {
        return None;
    }
    Some(ParsedPacket { ptype, id, payload })
}
