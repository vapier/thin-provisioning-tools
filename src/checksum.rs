use anyhow::{anyhow, Result};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use crc32c::crc32c;

use std::io::Cursor;

const BLOCK_SIZE: u64 = 4096;
#[allow(dead_code)]
const MAGIC: u64 = 0xa537a0aa6309ef77;
const SUPERBLOCK_CSUM_XOR: u32 = 160774;
const BITMAP_CSUM_XOR: u32 = 240779;
const INDEX_CSUM_XOR: u32 = 160478;
const BTREE_CSUM_XOR: u32 = 121107;

fn checksum(buf: &[u8]) -> u32 {
    crc32c(&buf[4..]) ^ 0xffffffff
}

#[derive(Debug, PartialEq)]
pub enum BT {
    SUPERBLOCK,
    NODE,
    INDEX,
    BITMAP,
    UNKNOWN,
}

pub fn metadata_block_type(buf: &[u8]) -> BT {
    if buf.len() != BLOCK_SIZE as usize {
        return BT::UNKNOWN;
    }

    // The checksum is always stored in the first u32 of the buffer.
    let mut rdr = Cursor::new(buf);
    let sum_on_disk = rdr.read_u32::<LittleEndian>().unwrap();
    let csum = checksum(buf);
    let btype = csum ^ sum_on_disk;

    match btype {
        SUPERBLOCK_CSUM_XOR => BT::SUPERBLOCK,
        BTREE_CSUM_XOR => BT::NODE,
        BITMAP_CSUM_XOR => BT::BITMAP,
        INDEX_CSUM_XOR => BT::INDEX,
        _ => BT::UNKNOWN,
    }
}

pub fn write_checksum(buf: &mut [u8], kind: BT) -> Result<()> {
    if buf.len() != BLOCK_SIZE as usize {
        return Err(anyhow!("block is wrong size"));
    }

    use BT::*;
    let salt = match kind {
        SUPERBLOCK => SUPERBLOCK_CSUM_XOR,
        NODE => BTREE_CSUM_XOR,
        BITMAP => BITMAP_CSUM_XOR,
        INDEX => INDEX_CSUM_XOR,
        UNKNOWN => {return Err(anyhow!("Invalid block type"));}
    };
    
    let csum = checksum(buf) ^ salt;
    let mut out = std::io::Cursor::new(buf);
    out.write_u32::<LittleEndian>(csum)?;
    Ok(())
}
