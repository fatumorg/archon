# RPF7 archives

Notes on the archive format used by GTA V on PC, written from what ARCHON's reader checks against real files. All integers are little-endian.

## Layout

| Part | Size | Notes |
|---|---|---|
| Header | 16 bytes | never encrypted |
| Entry table | entry count x 16 bytes | encrypted as one unit |
| Names block | names length bytes | encrypted as one unit, separately from the entry table |
| File data | rest of the file | each file starts on a 512-byte block boundary |

## Header

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | magic `0x52504637` (the bytes `7FPR` on disk) |
| 4 | u32 | entry count |
| 8 | u32 | names block length in bytes |
| 12 | u32 | encryption tag |

ARCHON refuses more than 1 000 000 entries, a names block over 16 MiB and directories nested more than 64 levels deep.

## Encryption tags

| Tag | Meaning |
|---|---|
| `0` | not encrypted |
| `0x4E45504F` (`OPEN`) | not encrypted; written by modding tools |
| `0x0FFFFFF9` | AES-256 in ECB mode with one fixed key |
| `0x0FEFFFFF` | NG: a table-driven block cipher whose key is chosen from the archive's file name and file size |
| anything else | treated as NG |

Both ciphers work on whole 16-byte blocks. A trailing partial block is left as it is.

For NG the key is selected from the lower-case file name of the archive (without its directory) and the archive's size in bytes truncated to 32 bits. A nested archive uses its own entry name and its own size.

## Entries

Every entry is 16 bytes. Read bytes 4 to 7 as a u32 called `hi` to tell the three kinds apart.

### Directory: `hi == 0x7FFFFF00`

| Bytes | Meaning |
|---|---|
| 0-3 | name offset |
| 4-7 | `0x7FFFFF00` |
| 8-11 | index of the first child entry |
| 12-15 | number of children |

### Binary file: top bit of `hi` clear

| Bytes | Meaning |
|---|---|
| 0-1 | name offset |
| 2-4 | stored size in bytes (24 bit). `0` means the file is stored uncompressed |
| 5-7 | data offset in 512-byte blocks (24 bit) |
| 8-11 | uncompressed size in bytes |
| 12-15 | `1` when the file data is encrypted |

A stored size other than `0` means the data is a raw deflate stream of that many bytes.

### Resource file: top bit of `hi` set

| Bytes | Meaning |
|---|---|
| 0-1 | name offset |
| 2-4 | stored size in bytes (24 bit) |
| 5-7 | data offset in 512-byte blocks, with the top bit set as the resource marker |
| 8-11 | system flags |
| 12-15 | graphics flags |

## Names

The names block is a run of NUL-terminated strings. An entry's name offset is a byte offset into it. Names are compared without regard to case.

## Tree

Entry 0 is the root directory and has an empty name. A directory's children are the consecutive entries starting at its first-child index. Every entry other than the root belongs to exactly one directory; ARCHON rejects a table in which an entry is claimed twice, is never reached, or a child range leaves the table.

## Nesting

An archive can hold other archives as binary files whose names end in `.rpf`. They are stored uncompressed and are read in place, at the entry's offset inside the parent.

## File data

A binary file with the encrypted flag set is decrypted with the archive's cipher before it is inflated. For NG the key is selected from the entry's lower-case name and its uncompressed size.

A resource file occupies its stored size at its offset. The first 16 bytes are a header and the rest is a raw deflate stream that inflates to the system pages followed by the graphics pages. A stored size of `0xFFFFFF` means the resource is 16 MiB or larger; its real size is then read from the header area as `(b[2] << 24) | (b[5] << 16) | (b[14] << 8) | b[7]`. Only resources whose names end in `.ysc` have an encrypted body.

A small number of resources in the shipped game were stored after a text-mode conversion that put a `0x0D` byte in front of every `0x0A` byte of the deflate stream; their stored size includes the extra bytes. Such a stream does not inflate. When inflating fails, ARCHON removes every `0x0D` that is directly followed by `0x0A` and tries once more; the result is accepted only if it inflates to exactly the size the flags declare.

A few resources of older versions (found in the AES-encrypted destruction archives inside `x64f.rpf`) carry eight extra bytes between the RSC7 header and the deflate stream. When inflating still fails, ARCHON skips those eight bytes and tries once more, with the same exact-size condition.

When a resource is exported as a standalone file, ARCHON writes a fresh RSC7 header built from the entry's flags, followed by the deflate stream.

## RSC7 header

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | magic `0x37435352` (the bytes `RSC7` on disk) |
| 4 | u32 | resource version |
| 8 | u32 | system flags |
| 12 | u32 | graphics flags |

The version is spread over the top four bits of both flag words: `((graphics >> 28) & 0xF) | (((system >> 28) & 0xF) << 4)`.

The size of the pages described by one flag word:

```
base  = 0x200 << (flags & 0xF)
count = ((flags >> 27) & 1)
      + (((flags >> 26) & 1) << 1)
      + (((flags >> 25) & 1) << 2)
      + (((flags >> 24) & 1) << 3)
      + (((flags >> 17) & 0x7F) << 4)
      + (((flags >> 11) & 0x3F) << 5)
      + (((flags >> 7) & 0xF) << 6)
      + (((flags >> 5) & 0x3) << 7)
      + (((flags >> 4) & 1) << 8)
size  = base * count
```

## Verified against

Every entry of every archive, nested archives included, was read and inflated without a failure on:

| Edition | `version.txt` | Archives | Entries | Bytes read |
| --- | --- | --- | --- | --- |
| Legacy (Steam) | 1.0.231.0 | 5378 (179 top level) | 403,642 | 278,208,038,312 |
