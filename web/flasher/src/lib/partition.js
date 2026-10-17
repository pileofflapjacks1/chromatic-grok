/** Parse an ESP-IDF binary partition table (32-byte entries, magic AA 50). */

const TYPES = { 0: "app", 1: "data" };
const APP_SUBTYPES = { 0x00: "factory", 0x20: "test" };
const DATA_SUBTYPES = { 0x00: "ota", 0x01: "phy", 0x02: "nvs", 0x03: "coredump", 0x04: "nvs_keys", 0x05: "efuse", 0x80: "esphttpd", 0x81: "fat", 0x82: "spiffs", 0x83: "littlefs" };

function subtypeName(type, sub) {
  if (type === 0) {
    if (sub >= 0x10 && sub < 0x20) return `ota_${sub - 0x10}`;
    return APP_SUBTYPES[sub] ?? `0x${sub.toString(16)}`;
  }
  if (type === 1) return DATA_SUBTYPES[sub] ?? `0x${sub.toString(16)}`;
  return `0x${sub.toString(16)}`;
}

/**
 * @param {Uint8Array} bytes the table (0xC00 bytes from 0x8000, or more)
 * @returns {{label:string,type:string,subtype:string,offset:number,size:number,flags:number}[]}
 */
export function parsePartitionTable(bytes) {
  if (!bytes || bytes.length < 32) throw new Error("Partition table is too short.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = [];
  for (let off = 0; off + 32 <= bytes.length; off += 32) {
    const b0 = bytes[off], b1 = bytes[off + 1];
    if (b0 === 0xaa && b1 === 0x50) {
      const type = bytes[off + 2];
      const sub = bytes[off + 3];
      let label = "";
      for (let i = 0; i < 16; i++) {
        const c = bytes[off + 12 + i];
        if (c === 0) break;
        label += String.fromCharCode(c);
      }
      out.push({
        label,
        type: TYPES[type] ?? `0x${type.toString(16)}`,
        subtype: subtypeName(type, sub),
        offset: view.getUint32(off + 4, true),
        size: view.getUint32(off + 8, true),
        flags: view.getUint32(off + 28, true),
      });
      continue;
    }
    if (b0 === 0xeb && b1 === 0xeb) break; // MD5 entry follows
    if (b0 === 0xff && b1 === 0xff) break; // end of table
    throw new Error(`Bad partition entry at +0x${off.toString(16)}.`);
  }
  if (out.length === 0) throw new Error("No partition entries (magic 0xAA50 not found).");
  return out;
}

/** One line per partition, for the page. */
export function formatPartitionTable(entries) {
  const hx = (n) => "0x" + n.toString(16);
  return entries.map((p) => `${p.label.padEnd(10)} ${p.type.padEnd(5)} ${p.subtype.padEnd(8)} ${hx(p.offset).padStart(9)} ${hx(p.size).padStart(9)}`).join("\n");
}
