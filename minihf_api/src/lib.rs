// This macro generates the FFI glue code from the UDL file
uniffi::include_scaffolding!("minihf_api");

pub fn add(left: u64, right: u64) -> u64 {
    left + right
}
