# JMP Binary Protocol Specification

**Version**: 5.3.0
**Status**: Working Draft

Binary encoding of the Jupyter Messaging Protocol (JMP) for bandwidth-constrained
transports (serial, TCP). All multi-byte integers are big-endian. All strings are
UTF-8, length-prefixed with a `uint16`.

See `jmp_bin.vectors.json` for canonical test vectors.

---

## 1. Envelope

Every JMP binary message has the following envelope:

```
header_len         : uint16
parent_header_len  : uint16
metadata_len       : uint16
content_len        : uint16
buffers_len        : uint16
header             : uint8[header_len]
parent_header      : uint8[parent_header_len]
metadata           : uint8[metadata_len]
content            : uint8[content_len]
buffers            : uint8[buffers_len]
```

Any section may be omitted by setting its length to zero. `metadata` and `buffers`
are currently unused (length always zero) but reserved for future use.

---

## 2. Header

```
msg_id_len     : uint16
msg_id         : uint8[]       # UUID string
session_id_len : uint16
session_id     : uint8[]       # UUID string
username_len   : uint16
username       : uint8[]
msg_type       : uint8         # see message type table
version        : uint8[3]      # [major, minor, patch]
```

`parent_header` uses the same format. When absent, its envelope length is zero.

---

## 3. Message Types

| msg_type | Opcode | Channel | Direction           |
| -------- | ------ | ------- | ------------------- |
| `kernel_info_request`  | `0x01` | shell   | frontend -> kernel |
| `kernel_info_reply`    | `0x02` | shell   | kernel -> frontend |
| `execute_request`      | `0x03` | shell   | frontend -> kernel |
| `execute_reply`        | `0x04` | shell   | kernel -> frontend |
| `stream`               | `0x05` | iopub   | kernel -> frontend |
| `error`                | `0x06` | iopub   | kernel -> frontend |
| `display_data`         | `0x07` | iopub   | kernel -> frontend |
| `status`               | `0x08` | iopub   | kernel -> frontend |
| `input_request`        | `0x09` | stdin   | kernel -> frontend |
| `input_reply`          | `0x0A` | stdin   | frontend -> kernel |
| `complete_request`     | `0x0B` | shell   | frontend -> kernel |
| `complete_reply`       | `0x0C` | shell   | kernel -> frontend |
| `inspect_request`      | `0x0D` | shell   | frontend -> kernel |
| `inspect_reply`        | `0x0E` | shell   | kernel -> frontend |
| `is_complete_request`  | `0x0F` | shell   | frontend -> kernel |
| `is_complete_reply`    | `0x10` | shell   | kernel -> frontend |
| `shutdown_request`     | `0x11` | control | frontend -> kernel |
| `shutdown_reply`       | `0x12` | control | kernel -> frontend |
| `interrupt_request`    | `0x13` | control | frontend -> kernel |
| `interrupt_reply`      | `0x14` | control | kernel -> frontend |
| `execute_result`       | `0x15` | iopub   | kernel -> frontend |
| `comm_open`            | `0x16` | shell   | bidirectional      |
| `comm_msg`             | `0x17` | shell   | bidirectional      |
| `comm_close`           | `0x18` | shell   | bidirectional      |
| `auth_request`         | `0x64` | control | client -> server   |
| `auth_reply`           | `0x65` | control | server -> client   |
| `target_not_found`     | `0x66` | control | server -> client   |

---

## 4. Common Patterns

### 4.1 Status field

Many reply messages begin with a `status` byte:

| Value | Meaning |
| ----- | ------- |
| `0`   | ok      |
| `1`   | error   |
| `2`   | abort   |

### 4.2 Error content

When `status = 1` (error), the content has this format regardless of message type:

```
status          : uint8        # 1
ename_len       : uint16
ename           : uint8[]
evalue_len      : uint16
evalue          : uint8[]
traceback_len   : uint16
traceback       : uint8[]      # newline-joined traceback lines
execution_count : uint16       # only meaningful in execute_reply errors
```

When `status = 2` (abort), content is just the single status byte with no
additional fields.

### 4.3 Execution state

| Value | Meaning  |
| ----- | -------- |
| `0`   | busy     |
| `1`   | idle     |
| `2`   | starting |

### 4.4 Stream name

| Value | Meaning |
| ----- | ------- |
| `0`   | stdout  |
| `1`   | stderr  |

### 4.5 Is-complete status

| Value | Meaning    |
| ----- | ---------- |
| `0`   | complete   |
| `1`   | incomplete |
| `2`   | invalid    |
| `3`   | unknown    |

---

## 5. Content Formats

Each section below defines the binary content for one message type. The JSON
equivalent is shown for reference.

---

### 5.1 `kernel_info_request` (0x01)

**Content**: empty (zero length)

---

### 5.2 `kernel_info_reply` (0x02)

```
status                   : uint8         # 0 = ok, else error format
protocol_version         : uint8[3]
implementation_len       : uint16
implementation           : uint8[]
implementation_version   : uint8[3]
language_name_len        : uint16
language_name            : uint8[]
language_version         : uint8[3]
mimetype_len             : uint16
mimetype                 : uint8[]
file_extension_len       : uint16
file_extension           : uint8[]
banner_len               : uint16
banner                   : uint8[]
debugger                 : uint8         # boolean
```

---

### 5.3 `execute_request` (0x03)

```
code_len     : uint16
code         : uint8[]
flags        : uint8
               bit 0 = silent
               bit 1 = store_history
               bit 2 = allow_stdin
               bit 3 = stop_on_error
```

`user_expressions` is not supported in binary encoding.

---

### 5.4 `execute_reply` (0x04)

When `status = ok`:

```
status          : uint8    # 0
execution_count : uint16
```

When `status = error`: uses the common error format (section 4.2).

---

### 5.5 `stream` (0x05)

```
name      : uint8      # 0 = stdout, 1 = stderr
text_len  : uint16
text      : uint8[]
```

---

### 5.6 `error` (0x06)

Uses the common error format (section 4.2). This is the iopub error broadcast,
distinct from error status in reply messages.

---

### 5.7 `display_data` (0x07)

Same encoding as `execute_result` (section 5.20) but without `execution_count`:

```
data_count       : uint16
data_key_1_len   : uint16
data_key_1       : uint8[]       # MIME type string
data_value_1_len : uint16
data_value_1     : uint8[]
...
data_key_n_len   : uint16
data_key_n       : uint8[]
data_value_n_len : uint16
data_value_n     : uint8[]
metadata_count   : uint16        # always 0 for now
```

---

### 5.8 `status` (0x08)

```
execution_state : uint8    # see section 4.3
```

---

### 5.9 `input_request` (0x09)

```
prompt_len : uint16
prompt     : uint8[]
password   : uint8         # boolean
```

---

### 5.10 `input_reply` (0x0A)

```
value_len : uint16
value     : uint8[]
```

---

### 5.11 `complete_request` (0x0B)

```
code_len   : uint16
code       : uint8[]
cursor_pos : uint16
```

---

### 5.12 `complete_reply` (0x0C)

```
matches_count    : uint16
match_1_len      : uint16
match_1          : uint8[]
...
match_n_len      : uint16
match_n          : uint8[]
cursor_start     : uint16
cursor_end       : uint16
metadata_len     : uint16       # always 0 for now
status           : uint8        # 0 = ok, 1 = error
```

---

### 5.13 `inspect_request` (0x0D)

```
code_len     : uint16
code         : uint8[]
cursor_pos   : uint16
detail_level : uint8        # 0 or 1
```

---

### 5.14 `inspect_reply` (0x0E)

When `status = ok`:

```
status           : uint8        # 0
found            : uint8        # boolean
data_count       : uint16
data_key_1_len   : uint16
data_key_1       : uint8[]      # MIME type
data_value_1_len : uint16
data_value_1     : uint8[]
...
metadata_count   : uint16       # always 0 for now
```

When `status = error`: uses the common error format (section 4.2).

---

### 5.15 `is_complete_request` (0x0F)

```
code_len : uint16
code     : uint8[]
```

---

### 5.16 `is_complete_reply` (0x10)

```
status      : uint8        # see section 4.5
indent_len  : uint16
indent      : uint8[]      # hint string (e.g. "  "), empty if not applicable
```

---

### 5.17 `shutdown_request` (0x11)

```
restart : uint8    # boolean
```

---

### 5.18 `shutdown_reply` (0x12)

```
status  : uint8    # 0 = ok, else error format
restart : uint8    # boolean
```

---

### 5.19 `interrupt_request` (0x13)

**Content**: empty (zero length)

---

### 5.20 `interrupt_reply` (0x14)

```
status : uint8    # 0 = ok, else error
```

---

### 5.21 `execute_result` (0x15)

```
execution_count  : uint16
data_count       : uint16
data_key_1_len   : uint16
data_key_1       : uint8[]       # MIME type string
data_value_1_len : uint16
data_value_1     : uint8[]
...
data_key_n_len   : uint16
data_key_n       : uint8[]
data_value_n_len : uint16
data_value_n     : uint8[]
metadata_count   : uint16        # always 0 for now
```

---

### 5.22 `comm_open` (0x16)

```
comm_id_len  : uint16
comm_id      : uint8[]
target_id    : uint16       # numeric ID mapped to target_name by the bridge
```

---

### 5.23 `comm_msg` (0x17)

```
comm_id_len : uint16
comm_id     : uint8[]
data        : uint32        # application-defined payload
```

---

### 5.24 `comm_close` (0x18)

```
comm_id_len : uint16
comm_id     : uint8[]
```

---

### 5.25 `auth_request` (0x64)

Non-standard. Used for client authentication with a relay server.

```
device_id_len : uint16
device_id     : uint8[]
timestamp     : uint32
hmac          : uint8[32]     # HMAC-SHA256
```

---

### 5.26 `auth_reply` (0x65)

```
status : uint8    # 0 = ok, else error
```

---

### 5.27 `target_not_found` (0x66)

**Content**: empty (zero length)

---

## 6. Protocol Flow

1. Frontend sends `<action>_request` on shell/control.
2. Kernel publishes `status: busy` on iopub.
3. Kernel processes the request; may emit `stream`, `error`, `display_data`,
   `execute_result`, or `input_request` during processing.
4. Kernel sends `<action>_reply` on the same channel as the request.
5. Kernel publishes `status: idle` on iopub.
