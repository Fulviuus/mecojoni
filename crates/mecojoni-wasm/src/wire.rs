extern crate alloc;

use alloc::{string::String, vec::Vec};

pub const WIRE_VERSION: u32 = 1;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WireError {
    Truncated,
    Limit,
    InvalidUtf8,
    TrailingBytes,
    InvalidValue,
}

pub struct Decoder<'a> {
    bytes: &'a [u8],
    cursor: usize,
}

impl<'a> Decoder<'a> {
    pub const fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, cursor: 0 }
    }

    pub fn u8(&mut self) -> Result<u8, WireError> {
        let value = *self.bytes.get(self.cursor).ok_or(WireError::Truncated)?;
        self.cursor += 1;
        Ok(value)
    }

    pub fn u32(&mut self) -> Result<u32, WireError> {
        let bytes = self.take_array::<4>()?;
        Ok(u32::from_le_bytes(bytes))
    }

    pub fn u64(&mut self) -> Result<u64, WireError> {
        let bytes = self.take_array::<8>()?;
        Ok(u64::from_le_bytes(bytes))
    }

    pub fn bytes(&mut self, maximum: usize) -> Result<Vec<u8>, WireError> {
        let length = usize::try_from(self.u32()?).map_err(|_| WireError::Limit)?;
        if length > maximum {
            return Err(WireError::Limit);
        }
        let end = self.cursor.checked_add(length).ok_or(WireError::Limit)?;
        let value = self
            .bytes
            .get(self.cursor..end)
            .ok_or(WireError::Truncated)?
            .to_vec();
        self.cursor = end;
        Ok(value)
    }

    pub fn string(&mut self, maximum: usize) -> Result<String, WireError> {
        String::from_utf8(self.bytes(maximum)?).map_err(|_| WireError::InvalidUtf8)
    }

    pub fn optional_string(&mut self, maximum: usize) -> Result<Option<String>, WireError> {
        match self.u8()? {
            0 => Ok(None),
            1 => self.string(maximum).map(Some),
            _ => Err(WireError::InvalidValue),
        }
    }

    pub fn finish(self) -> Result<(), WireError> {
        if self.cursor == self.bytes.len() {
            Ok(())
        } else {
            Err(WireError::TrailingBytes)
        }
    }

    fn take_array<const N: usize>(&mut self) -> Result<[u8; N], WireError> {
        let end = self.cursor.checked_add(N).ok_or(WireError::Limit)?;
        let source = self
            .bytes
            .get(self.cursor..end)
            .ok_or(WireError::Truncated)?;
        let mut bytes = [0_u8; N];
        bytes.copy_from_slice(source);
        self.cursor = end;
        Ok(bytes)
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Encoder {
    bytes: Vec<u8>,
}

impl Encoder {
    pub const fn new() -> Self {
        Self { bytes: Vec::new() }
    }

    pub fn u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    pub fn u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn u64(&mut self, value: u64) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    pub fn bytes(&mut self, value: &[u8]) {
        self.u32(u32::try_from(value.len()).unwrap_or(u32::MAX));
        self.bytes.extend_from_slice(value);
    }

    pub fn string(&mut self, value: &str) {
        self.bytes(value.as_bytes());
    }

    pub fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

#[cfg(test)]
mod tests {
    use alloc::string::ToString;

    use super::{Decoder, Encoder, WireError};

    #[test]
    fn round_trips_fixed_width_values_and_utf8() {
        let mut encoder = Encoder::new();
        encoder.u8(7);
        encoder.u32(0x1234_5678);
        encoder.u64(0x0123_4567_89ab_cdef);
        encoder.string("Héllo 🦀");
        let bytes = encoder.into_bytes();
        let mut decoder = Decoder::new(&bytes);

        assert_eq!(decoder.u8(), Ok(7));
        assert_eq!(decoder.u32(), Ok(0x1234_5678));
        assert_eq!(decoder.u64(), Ok(0x0123_4567_89ab_cdef));
        assert_eq!(decoder.string(64), Ok("Héllo 🦀".to_string()));
        assert_eq!(decoder.finish(), Ok(()));
    }

    #[test]
    fn rejects_limits_invalid_utf8_and_trailing_bytes() {
        let mut encoder = Encoder::new();
        encoder.bytes(&[0xff]);
        let bytes = encoder.into_bytes();
        assert_eq!(Decoder::new(&bytes).string(0), Err(WireError::Limit));
        assert_eq!(Decoder::new(&bytes).string(1), Err(WireError::InvalidUtf8));
        assert_eq!(Decoder::new(&[1]).finish(), Err(WireError::TrailingBytes));
    }
}
