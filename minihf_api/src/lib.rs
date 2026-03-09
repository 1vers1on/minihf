use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex, OnceLock};
use std::io::{Read, Write};
use serialport::SerialPort;
use std::sync::atomic::{AtomicU16, AtomicBool, Ordering};
use std::collections::HashMap;
use std::thread;

use cobs::{encode, decode_vec, max_encoding_length};
use crc::{Crc, CRC_16_KERMIT};

const RESP_ACK: u8 = 0xFF;
const RESP_NACK: u8 = 0xFE;
const DEBUG_MSG_CMD: u8 = 0xFC;
const HEADER_BYTE: u8 = 0xAA;
const HEADER_SIZE: usize = 5;

const BAND_30M_MIN_HZ: f64 = 10_100_000.0;
const BAND_30M_MAX_HZ: f64 = 10_150_000.0;

fn clamp_freq_hz(freq: f64) -> f64 {
    freq.clamp(BAND_30M_MIN_HZ, BAND_30M_MAX_HZ)
}

uniffi::include_scaffolding!("minihf_api");

#[uniffi::export(callback_interface)]
pub trait DebugLogger: Send + Sync {
    fn log(&self, message: String);
}

static LOGGER: OnceLock<Mutex<Option<Box<dyn DebugLogger>>>> = OnceLock::new();

fn logger_cell() -> &'static Mutex<Option<Box<dyn DebugLogger>>> {
    LOGGER.get_or_init(|| Mutex::new(None))
}

fn debug_log(msg: &str) {
    if let Ok(guard) = logger_cell().lock() {
        if let Some(ref logger) = *guard {
            logger.log(msg.to_string());
        }
    }
}

#[uniffi::export]
pub fn set_debug_logger(logger: Option<Box<dyn DebugLogger>>) {
    if let Ok(mut guard) = logger_cell().lock() {
        *guard = logger;
    }
}

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

#[derive(uniffi::Record)]
pub struct BuckRegulatorState {
    pub enabled: bool,
    pub voltage_level: u8,
}

#[derive(Clone)]
struct ParsedPacket {
    ptype: u8,
    id: u16,
    payload: Vec<u8>,
}

#[derive(uniffi::Object)]
pub struct MiniHF {
    port: Arc<Mutex<Option<Box<dyn Transport>>>>,
    next_id: AtomicU16,
    timeout: Duration,
    responses: Arc<Mutex<HashMap<u16, ParsedPacket>>>,
    is_running: Arc<AtomicBool>,
}

impl Drop for MiniHF {
    fn drop(&mut self) {
        self.is_running.store(false, Ordering::Relaxed);
    }
}

#[uniffi::export]
impl MiniHF {
    #[uniffi::constructor]
    pub fn new(path: String, baud: u32) -> Result<Arc<Self>, MiniHFError> {
        Self::new_with_timeout(path, baud, 2000)
    }

    #[uniffi::constructor]
    pub fn new_with_timeout(path: String, baud: u32, timeout_ms: u64) -> Result<Arc<Self>, MiniHFError> {
        debug_log(&format!("opening serial port {} @ {} baud (timeout {}ms)", path, baud, timeout_ms));
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(10)) // Short timeout for background non-blocking reads
            .open()
            .map_err(|e| MiniHFError::Io(e.to_string()))?;
        debug_log("serial port opened successfully");

        let hf = Arc::new(Self {
            port: Arc::new(Mutex::new(Some(Box::new(SerialTransport(port))))),
            next_id: AtomicU16::new(1),
            timeout: Duration::from_millis(timeout_ms),
            responses: Arc::new(Mutex::new(HashMap::new())),
            is_running: Arc::new(AtomicBool::new(true)),
        });

        hf.spawn_reader_thread();
        Ok(hf)
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
            debug_log(&format!("opening fd {} (timeout {}ms)", fd, timeout_ms));
            let transport = FdTransport::new(fd, Duration::from_millis(10));
            let hf = Arc::new(Self {
                port: Arc::new(Mutex::new(Some(Box::new(transport)))),
                next_id: AtomicU16::new(1),
                timeout: Duration::from_millis(timeout_ms),
                responses: Arc::new(Mutex::new(HashMap::new())),
                is_running: Arc::new(AtomicBool::new(true)),
            });

            hf.spawn_reader_thread();
            Ok(hf)
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
        let clamped = clamp_freq_hz(freq_hz);
        let freq_int = (clamped * 100.0).round() as u64;
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

    pub fn set_buck_boost_regulator(&self, enabled: bool, voltage_level: u8) -> Result<(), MiniHFError> {
        if voltage_level > 18 {
            return Err(MiniHFError::InvalidArgument(
                format!("voltage_level must be 0–18, got {}", voltage_level),
            ));
        }
        let state = if enabled { 0x80 } else { 0x00 } | (voltage_level & 0x1F);
        self.transact(0x05, vec![state])?;
        Ok(())
    }

    pub fn get_buck_boost_regulator(&self) -> Result<BuckRegulatorState, MiniHFError> {
        let resp = self.transact(0x06, vec![])?;
        if resp.is_empty() {
            return Err(MiniHFError::InvalidPacket);
        }
        let state = resp[0];
        Ok(BuckRegulatorState {
            enabled: (state & 0x80) != 0,
            voltage_level: state & 0x1F,
        })
    }

    pub fn set_tr_switch(&self, tx_mode: bool) -> Result<(), MiniHFError> {
        self.transact(0x08, vec![if tx_mode { 1 } else { 0 }])?;
        Ok(())
    }

    pub fn tx_test_signal(&self, duration_ms: u32) -> Result<(), MiniHFError> {
        if duration_ms == 0 {
            return Err(MiniHFError::InvalidArgument(
                "duration_ms must be greater than 0".to_string(),
            ));
        }
        let payload = duration_ms.to_le_bytes().to_vec();
        self.transact(0x07, payload)?;
        Ok(())
    }

    pub fn reset(&self) -> Result<(), MiniHFError> {
        self.send_only(0xFD, vec![])?;
        Ok(())
    }

    pub fn close(&self) -> Result<(), MiniHFError> {
        debug_log("closing connection");
        self.is_running.store(false, Ordering::Relaxed);
        let mut port_opt = self.port.lock().unwrap();
        if let Some(ref mut port) = *port_opt {
            let _ = port.flush();
        }
        *port_opt = None;
        self.responses.lock().unwrap().clear();
        Ok(())
    }
}

impl MiniHF {
    fn spawn_reader_thread(&self) {
        let port_arc = self.port.clone();
        let responses_arc = self.responses.clone();
        let is_running_arc = self.is_running.clone();

        thread::spawn(move || {
            let mut rx_buf = Vec::new();

            while is_running_arc.load(Ordering::Relaxed) {
                let mut bytes_read = 0;

                // Lock the port just long enough to read available data
                if let Ok(mut port_opt) = port_arc.lock() {
                    if let Some(ref mut port) = *port_opt {
                        let to_read = port.bytes_available().unwrap_or(0);
                        if to_read > 0 {
                            let mut tmp = vec![0u8; to_read.max(1)];
                            if let Ok(n) = port.read(&mut tmp) {
                                rx_buf.extend_from_slice(&tmp[..n]);
                                bytes_read = n;
                            }
                        }
                    }
                }

                if bytes_read > 0 {
                    while let Some(idx) = rx_buf.iter().position(|&b| b == 0x00) {
                        let frame_data: Vec<u8> = rx_buf.drain(..=idx).collect();
                        let frame_bytes = &frame_data[..frame_data.len() - 1];

                        if frame_bytes.is_empty() { continue; }

                        if let Ok(decoded) = decode_vec(frame_bytes) {
                            if let Some(pkt) = parse_packet(&decoded) {
                                if pkt.ptype == DEBUG_MSG_CMD {
                                    let msg = String::from_utf8_lossy(&pkt.payload);
                                    debug_log(&format!("[device] {}", msg));
                                } else {
                                    // It's a response packet, route it to `transact`
                                    if let Ok(mut map) = responses_arc.lock() {
                                        map.insert(pkt.id, pkt);
                                    }
                                }
                            }
                        }
                    }
                }

                // Sleep briefly to yield CPU and allow `transact` to grab the port lock to write
                thread::sleep(Duration::from_millis(5));
            }
        });
    }

    fn send_only(&self, cmd_id: u8, payload: Vec<u8>) -> Result<(), MiniHFError> {
        if payload.len() > 255 {
            return Err(MiniHFError::InvalidArgument(
                format!("payload too large: {} bytes (max 255)", payload.len()),
            ));
        }
        let current_id = self.next_id.fetch_add(1, Ordering::SeqCst);
        debug_log(&format!("TX cmd=0x{:02X} id={} len={} (fire-and-forget)", cmd_id, current_id, payload.len()));
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
        debug_log(&format!("TX cmd=0x{:02X} id={} len={}", cmd_id, current_id, payload.len()));
        let frame = frame_packet(cmd_id, current_id, &payload);

        // Scope the lock so the reader thread can continue
        {
            let mut port_opt = self.port.lock().unwrap();
            let port = port_opt.as_mut().ok_or(MiniHFError::PortClosed)?;
            port.write_all(&frame).map_err(|e| MiniHFError::Io(e.to_string()))?;
        }

        let deadline = Instant::now() + self.timeout;

        // Poll our synchronized map for the response
        loop {
            if Instant::now() > deadline {
                return Err(MiniHFError::Timeout);
            }

            if let Ok(mut map) = self.responses.lock() {
                if let Some(pkt) = map.remove(&current_id) {
                    if pkt.ptype == RESP_NACK {
                        debug_log(&format!("RX NACK for id={}", current_id));
                        return Err(MiniHFError::Nack);
                    }
                    debug_log(&format!("RX ACK cmd=0x{:02X} id={} payload_len={}", pkt.ptype, pkt.id, pkt.payload.len()));
                    return Ok(pkt.payload);
                }
            }

            thread::sleep(Duration::from_millis(5));
        }
    }
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
    if raw.len() < HEADER_SIZE + 2 { return None; }
    if raw[0] != HEADER_BYTE { return None; }
    
    let ptype = raw[1];
    let id = u16::from_le_bytes([raw[2], raw[3]]);
    let length = raw[4] as usize;
    
    if raw.len() < HEADER_SIZE + length + 2 { return None; }
    let payload = raw[HEADER_SIZE..HEADER_SIZE + length].to_vec();

    let crc_obj = Crc::<u16>::new(&CRC_16_KERMIT);
    let crc_calc = crc_obj.checksum(&raw[..HEADER_SIZE + length]);
    let crc_recv = u16::from_le_bytes([
        raw[HEADER_SIZE + length],
        raw[HEADER_SIZE + length + 1],
    ]);
    
    if crc_calc != crc_recv { return None; }
    Some(ParsedPacket { ptype, id, payload })
}
