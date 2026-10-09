// ZipReader.swift -- a zip's entries, read in order from their local headers as java.util.zip.ZipInputStream reads
// them (Android's FirmwareImport.kt takes zips that way, so the two apps see the same entries): stored and deflated
// entries, sizes in the local header or in a data descriptor after the data (macOS's Archive Utility and Java write
// those).  iOS has no zip API of its own; the deflate is Apple's Compression framework (COMPRESSION_ZLIB is raw
// DEFLATE, RFC 1951).  No ZIP64, no encryption: firmware, update disks and NVDA add-ons need neither.  MIT.

import Compression
import Foundation

enum ZipError: Error {
    case damaged(String)
}

struct ZipEntry {
    let name: String                    // as stored ("/"-separated, or "\" from some Windows tools)
    let isDirectory: Bool
}

struct ZipReader {
    private let data: Data
    private var at: Int

    /// The zip starting at byte `start` of `data` (an update program carries one behind its own code).
    init(_ data: Data, start: Int = 0) {
        self.data = data
        self.at = start
    }

    static func isZip(_ data: Data, at i: Int) -> Bool {
        i >= 0 && i + 4 <= data.count && data[data.startIndex + i] == 0x50 && data[data.startIndex + i + 1] == 0x4B
            && data[data.startIndex + i + 2] == 3 && data[data.startIndex + i + 3] == 4
    }

    /// Where the first zip entry starts in `data`, or nil.
    static func zipStart(_ data: Data) -> Int? {
        guard data.count >= 4 else { return nil }
        return data.withUnsafeBytes { (p: UnsafeRawBufferPointer) -> Int? in
            let b = p.bindMemory(to: UInt8.self)
            for i in 0...(b.count - 4) where b[i] == 0x50 && b[i + 1] == 0x4B && b[i + 2] == 3 && b[i + 3] == 4 {
                return i
            }
            return nil
        }
    }

    private func u16(_ i: Int) -> Int {
        Int(data[data.startIndex + i]) | Int(data[data.startIndex + i + 1]) << 8
    }

    private func u32(_ i: Int) -> Int {
        u16(i) | u16(i + 2) << 16
    }

    /// The next entry and its bytes (nil when the entry is larger than `cap`: skipped), or nil at the end of the
    /// entries (the central directory, or the end of the data).
    mutating func next(cap: Int) throws -> (ZipEntry, Data?)? {
        guard at + 30 <= data.count, ZipReader.isZip(data, at: at) else { return nil }
        let flags = u16(at + 6), method = u16(at + 8)
        var csize = u32(at + 18), usize = u32(at + 22)
        let nameLen = u16(at + 26), extraLen = u16(at + 28)
        let nameAt = at + 30, dataAt = nameAt + nameLen + extraLen
        guard dataAt <= data.count else { throw ZipError.damaged("an entry's header runs past the end") }
        let nameBytes = data.subdata(in: (data.startIndex + nameAt)..<(data.startIndex + nameAt + nameLen))
        let name = String(data: nameBytes, encoding: (flags & 0x800) != 0 ? .utf8 : .isoLatin1) ?? "?"
        let entry = ZipEntry(name: name, isDirectory: name.hasSuffix("/") || name.hasSuffix("\\"))
        if flags & 1 != 0 { throw ZipError.damaged("\(name) is encrypted") }
        let descriptor = flags & 8 != 0
        var bytes: Data?
        switch method {
        case 0:                         // stored
            if descriptor && csize == 0 { throw ZipError.damaged("\(name): a stored entry without its size") }
            guard dataAt + csize <= data.count else { throw ZipError.damaged("\(name) runs past the end") }
            if csize <= cap { bytes = data.subdata(in: (data.startIndex + dataAt)..<(data.startIndex + dataAt + csize)) }
            usize = csize
        case 8:                         // deflated
            let (out, produced) = try ZipReader.inflate(data, from: dataAt, cap: cap, name: name)
            bytes = out
            if descriptor {
                // The sizes follow the data, and the inflater does not say where its stream ended (it may read
                // ahead): the descriptor is where its own two sizes say this entry's data ends.
                guard let end = descriptorAt(from: dataAt, size: produced) else {
                    throw ZipError.damaged("\(name): its data descriptor is missing")
                }
                csize = end - dataAt
            } else if usize != produced {
                throw ZipError.damaged("\(name) is \(produced) bytes, its header says \(usize)")
            }
        default:
            throw ZipError.damaged("\(name) is compressed with method \(method)")
        }
        at = dataAt + csize
        if descriptor {                 // the descriptor: an optional signature, then CRC and the two sizes
            if at + 4 <= data.count && u32(at) == 0x08074B50 { at += 4 }
            at += 12
        }
        return (entry, bytes)
    }

    /// Where a deflated entry's data ends when its sizes come after it: the first offset from `from` holding a data
    /// descriptor -- with its signature or without -- whose compressed size is the distance from `from` and whose
    /// uncompressed size is `size`.
    private func descriptorAt(from: Int, size: Int) -> Int? {
        var p = from
        while p + 12 <= data.count {
            if p + 16 <= data.count && u32(p) == 0x08074B50 && u32(p + 8) == p - from && u32(p + 12) == size {
                return p
            }
            if u32(p + 4) == p - from && u32(p + 8) == size
                && (p + 12 == data.count || (p + 16 <= data.count && u32(p + 12) & 0xFFFF == 0x4B50)) {
                return p
            }
            p += 1
        }
        return nil
    }

    /// Raw DEFLATE from `from` to its end: the bytes (nil past `cap`) and how many there are.
    private static func inflate(_ data: Data, from: Int, cap: Int, name: String) throws -> (Data?, Int) {
        let stream = UnsafeMutablePointer<compression_stream>.allocate(capacity: 1)
        defer { stream.deallocate() }
        guard compression_stream_init(stream, COMPRESSION_STREAM_DECODE, COMPRESSION_ZLIB) == COMPRESSION_STATUS_OK
        else { throw ZipError.damaged("\(name): the inflater would not start") }
        defer { compression_stream_destroy(stream) }
        let chunk = 1 << 16
        let dst = UnsafeMutablePointer<UInt8>.allocate(capacity: chunk)
        defer { dst.deallocate() }
        var out = Data()
        var over = false
        var produced = 0
        let total = data.count - from
        return try data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) -> (Data?, Int) in
            let src = raw.bindMemory(to: UInt8.self).baseAddress! + from
            stream.pointee.src_ptr = UnsafePointer(src)
            stream.pointee.src_size = total
            while true {
                stream.pointee.dst_ptr = dst
                stream.pointee.dst_size = chunk
                let status = compression_stream_process(stream, 0)
                let got = chunk - stream.pointee.dst_size
                produced += got
                if !over {
                    out.append(dst, count: got)
                    if out.count > cap { over = true; out = Data() }
                }
                switch status {
                case COMPRESSION_STATUS_END:
                    return (over ? nil : out, produced)
                case COMPRESSION_STATUS_OK:
                    if stream.pointee.src_size == 0 && got == 0 {
                        throw ZipError.damaged("\(name) is cut short")
                    }
                default:
                    throw ZipError.damaged("\(name) could not be inflated")
                }
            }
        }
    }
}
