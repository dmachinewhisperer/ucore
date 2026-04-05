// JMP Binary Protocol - Converter Library
// See jmp_bin.spec.md for the wire format specification.

// --- Message type constants ---

const MSG_TYPES = {
  KERNEL_INFO_REQUEST: 0x01,
  KERNEL_INFO_REPLY: 0x02,
  EXECUTE_REQUEST: 0x03,
  EXECUTE_REPLY: 0x04,
  STREAM: 0x05,
  ERROR: 0x06,
  DISPLAY_DATA: 0x07,
  STATUS: 0x08,
  INPUT_REQUEST: 0x09,
  INPUT_REPLY: 0x0A,
  COMPLETE_REQUEST: 0x0B,
  COMPLETE_REPLY: 0x0C,
  INSPECT_REQUEST: 0x0D,
  INSPECT_REPLY: 0x0E,
  IS_COMPLETE_REQUEST: 0x0F,
  IS_COMPLETE_REPLY: 0x10,
  SHUTDOWN_REQUEST: 0x11,
  SHUTDOWN_REPLY: 0x12,
  INTERRUPT_REQUEST: 0x13,
  INTERRUPT_REPLY: 0x14,
  EXECUTE_RESULT: 0x15,
  COMM_OPEN: 0x16,
  COMM_MSG: 0x17,
  COMM_CLOSE: 0x18,

  AUTH_REQUEST: 0x64,
  AUTH_REPLY: 0x65,
  TARGET_NOT_FOUND: 0x66,
};

const MSG_TYPE_NAMES = Object.fromEntries(
  Object.entries(MSG_TYPES).map(([name, value]) => [value, name.toLowerCase()])
);

const STATUS = { OK: 0, ERROR: 1, ABORT: 2 };
const STATUS_NAMES = ['ok', 'error', 'abort'];

const EXECUTION_STATE = { BUSY: 0, IDLE: 1, STARTING: 2, DEAD: 3 };
const EXECUTION_STATE_NAMES = ['busy', 'idle', 'starting', 'dead'];

const IS_COMPLETE_STATUS = { COMPLETE: 0, INCOMPLETE: 1, INVALID: 2, UNKNOWN: 3 };
const IS_COMPLETE_STATUS_NAMES = ['complete', 'incomplete', 'invalid', 'unknown'];

// --- BinaryReader / BinaryWriter ---

class BinaryReader {
  constructor(buffer) {
    this.buf = buffer;
    this.pos = 0;
  }

  _check(n) {
    if (this.pos + n > this.buf.length) {
      throw new RangeError(
        `Read overflow: need ${n} bytes at offset ${this.pos}, buffer length ${this.buf.length}`
      );
    }
  }

  uint8() {
    this._check(1);
    const v = this.buf.readUInt8(this.pos);
    this.pos += 1;
    return v;
  }

  uint16() {
    this._check(2);
    const v = this.buf.readUInt16BE(this.pos);
    this.pos += 2;
    return v;
  }

  uint32() {
    this._check(4);
    const v = this.buf.readUInt32BE(this.pos);
    this.pos += 4;
    return v;
  }

  bytes(n) {
    this._check(n);
    const v = this.buf.slice(this.pos, this.pos + n);
    this.pos += n;
    return v;
  }

  string() {
    const len = this.uint16();
    this._check(len);
    const v = this.buf.toString('utf8', this.pos, this.pos + len);
    this.pos += len;
    return v;
  }

  version() {
    const major = this.uint8();
    const minor = this.uint8();
    const patch = this.uint8();
    return `${major}.${minor}.${patch}`;
  }

  bool() {
    return !!this.uint8();
  }

  remaining() {
    return this.buf.length - this.pos;
  }
}

class BinaryWriter {
  constructor(capacity = 256) {
    this.buf = Buffer.alloc(capacity);
    this.pos = 0;
  }

  _grow(needed) {
    while (this.pos + needed > this.buf.length) {
      const next = Buffer.alloc(this.buf.length * 2);
      this.buf.copy(next);
      this.buf = next;
    }
  }

  uint8(v) {
    this._grow(1);
    this.buf.writeUInt8(v, this.pos);
    this.pos += 1;
    return this;
  }

  uint16(v) {
    this._grow(2);
    this.buf.writeUInt16BE(v, this.pos);
    this.pos += 2;
    return this;
  }

  uint32(v) {
    this._grow(4);
    this.buf.writeUInt32BE(v, this.pos);
    this.pos += 4;
    return this;
  }

  bytes(buf) {
    this._grow(buf.length);
    buf.copy(this.buf, this.pos);
    this.pos += buf.length;
    return this;
  }

  string(s) {
    const len = Buffer.byteLength(s, 'utf8');
    this.uint16(len);
    this._grow(len);
    this.buf.write(s, this.pos, len, 'utf8');
    this.pos += len;
    return this;
  }

  version(v) {
    const parts = v.split('.').map(Number);
    this.uint8(parts[0] || 0);
    this.uint8(parts[1] || 0);
    this.uint8(parts[2] || 0);
    return this;
  }

  bool(v) {
    return this.uint8(v ? 1 : 0);
  }

  toBuffer() {
    return this.buf.slice(0, this.pos);
  }
}

// --- Header ---

class HeaderConverter {
  static toBinary(header) {
    const w = new BinaryWriter();
    w.string(header.msg_id || '');
    w.string(header.session || '');
    w.string(header.username || '');
    w.uint8(MSG_TYPES[(header.msg_type || '').toUpperCase()] || 0);
    w.version(header.version || '5.3.0');
    return w.toBuffer();
  }

  static fromBinary(buffer) {
    const r = new BinaryReader(buffer);
    return {
      msg_id: r.string(),
      session: r.string(),
      username: r.string(),
      date: new Date().toISOString(),
      msg_type: MSG_TYPE_NAMES[r.uint8()] || 'unknown',
      version: r.version(),
    };
  }
}

// --- Content converters ---
// Each has static toBinary(content) -> Buffer and fromBinary(buffer) -> object.

class KernelInfoRequestConverter {
  static toBinary() { return Buffer.alloc(0); }
  static fromBinary() { return {}; }
}

class KernelInfoReplyConverter {
  static toBinary(c) {
    if (c.status !== 'ok') return ErrorConverter.toBinary(c);
    const w = new BinaryWriter();
    w.uint8(STATUS.OK);
    w.version(c.protocol_version || '5.3.0');
    w.string(c.implementation || '');
    w.version(c.implementation_version || '1.0.0');
    w.string(c.language_info?.name || '');
    w.version(c.language_info?.version || '0.0.0');
    w.string(c.language_info?.mimetype || '');
    w.string(c.language_info?.file_extension || '');
    w.string(c.banner || '');
    w.bool(c.debugger);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = r.uint8();
    if (status !== STATUS.OK) return ErrorConverter.fromBinary(buf);
    return {
      status: 'ok',
      protocol_version: r.version(),
      implementation: r.string(),
      implementation_version: r.version(),
      language_info: {
        name: r.string(),
        version: r.version(),
        mimetype: r.string(),
        file_extension: r.string(),
      },
      banner: r.string(),
      debugger: r.bool(),
    };
  }
}

class ExecuteRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.code || '');
    w.uint8(
      (c.silent ? 1 : 0) |
      (c.store_history ? 2 : 0) |
      (c.allow_stdin ? 4 : 0) |
      (c.stop_on_error ? 8 : 0)
    );
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const code = r.string();
    const flags = r.uint8();
    return {
      code,
      silent: !!(flags & 1),
      store_history: !!(flags & 2),
      allow_stdin: !!(flags & 4),
      stop_on_error: !!(flags & 8),
      user_expressions: {},
    };
  }
}

class ExecuteReplyConverter {
  static toBinary(c) {
    if (c.status !== 'ok') return ErrorConverter.toBinary(c);
    const w = new BinaryWriter();
    w.uint8(STATUS.OK);
    w.uint16(c.execution_count || 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = r.uint8();
    if (status !== STATUS.OK) return ErrorConverter.fromBinary(buf);
    return { status: 'ok', execution_count: r.uint16() };
  }
}

class StreamConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.uint8(c.name === 'stderr' ? 1 : 0);
    w.string(c.text || '');
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const name = r.uint8() === 1 ? 'stderr' : 'stdout';
    return { name, text: r.string() };
  }
}

class ErrorConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.uint8(STATUS.ERROR);
    w.string(c.ename || '');
    w.string(c.evalue || '');
    w.string(Array.isArray(c.traceback) ? c.traceback.join('\n') : (c.traceback || ''));
    w.uint16(c.execution_count || 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = r.uint8();
    if (status === STATUS.ABORT) return { status: 'abort' };
    const ename = r.string();
    const evalue = r.string();
    const rawTraceback = r.string();
    const execution_count = r.uint16();
    const lines = rawTraceback.split('\n').filter(l => l.trim());
    return {
      status: 'error',
      ename,
      evalue,
      traceback: lines.length > 0 ? lines : [],
      execution_count,
    };
  }
}

class DisplayDataConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    const entries = Object.entries(c.data || {});
    w.uint16(entries.length);
    for (const [key, value] of entries) {
      w.string(key);
      w.string(String(value));
    }
    w.uint16(0); // metadata_count
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const dataCount = r.uint16();
    const data = {};
    for (let i = 0; i < dataCount; i++) {
      data[r.string()] = r.string();
    }
    r.uint16(); // metadata_count
    return { data, metadata: {} };
  }
}

class StatusConverter {
  static toBinary(c) {
    const w = new BinaryWriter(1);
    const map = { busy: 0, idle: 1, starting: 2 };
    w.uint8(map[c.execution_state] ?? 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { execution_state: EXECUTION_STATE_NAMES[r.uint8()] || 'busy' };
  }
}

class InputRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.prompt || '');
    w.bool(c.password);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { prompt: r.string(), password: r.bool() };
  }
}

class InputReplyConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.value || '');
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { value: r.string() };
  }
}

class CompleteRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.code || '');
    w.uint16(c.cursor_pos || 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { code: r.string(), cursor_pos: r.uint16() };
  }
}

class CompleteReplyConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    const matches = c.matches || [];
    w.uint16(matches.length);
    for (const m of matches) w.string(m);
    w.uint16(c.cursor_start || 0);
    w.uint16(c.cursor_end || 0);
    w.uint16(0); // metadata_len
    w.uint8(c.status === 'ok' ? STATUS.OK : STATUS.ERROR);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const count = r.uint16();
    const matches = [];
    for (let i = 0; i < count; i++) matches.push(r.string());
    const cursor_start = r.uint16();
    const cursor_end = r.uint16();
    r.uint16(); // metadata_len
    const status = r.uint8();
    return {
      matches,
      cursor_start,
      cursor_end,
      metadata: {},
      status: STATUS_NAMES[status] || 'ok',
    };
  }
}

class InspectRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.code || '');
    w.uint16(c.cursor_pos || 0);
    w.uint8(c.detail_level || 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return {
      code: r.string(),
      cursor_pos: r.uint16(),
      detail_level: r.uint8(),
    };
  }
}

class InspectReplyConverter {
  static toBinary(c) {
    if (c.status !== 'ok') return ErrorConverter.toBinary(c);
    const w = new BinaryWriter();
    w.uint8(STATUS.OK);
    w.bool(c.found);
    const entries = Object.entries(c.data || {});
    w.uint16(entries.length);
    for (const [key, value] of entries) {
      w.string(key);
      w.string(String(value));
    }
    w.uint16(0); // metadata_count
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = r.uint8();
    if (status !== STATUS.OK) return ErrorConverter.fromBinary(buf);
    const found = r.bool();
    const dataCount = r.uint16();
    const data = {};
    for (let i = 0; i < dataCount; i++) {
      data[r.string()] = r.string();
    }
    r.uint16(); // metadata_count
    return { status: 'ok', found, data, metadata: {} };
  }
}

class IsCompleteRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.code || '');
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { code: r.string() };
  }
}

class IsCompleteReplyConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    const map = { complete: 0, incomplete: 1, invalid: 2, unknown: 3 };
    w.uint8(map[c.status] ?? 3);
    w.string(c.indent || '');
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = IS_COMPLETE_STATUS_NAMES[r.uint8()] || 'unknown';
    const indent = r.string();
    return { status, indent };
  }
}

class ShutdownRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter(1);
    w.bool(c.restart);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { restart: r.bool() };
  }
}

class ShutdownReplyConverter {
  static toBinary(c) {
    if (c.status !== 'ok') return ErrorConverter.toBinary(c);
    const w = new BinaryWriter(2);
    w.uint8(STATUS.OK);
    w.bool(c.restart);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const status = r.uint8();
    if (status !== STATUS.OK) return ErrorConverter.fromBinary(buf);
    return { status: 'ok', restart: r.bool() };
  }
}

class InterruptRequestConverter {
  static toBinary() { return Buffer.alloc(0); }
  static fromBinary() { return {}; }
}

class InterruptReplyConverter {
  static toBinary(c) {
    const w = new BinaryWriter(1);
    w.uint8(c.status === 'ok' ? 0 : 1);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { status: STATUS_NAMES[r.uint8()] || 'ok' };
  }
}

class ExecuteResultConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.uint16(c.execution_count || 0);
    const entries = Object.entries(c.data || {});
    w.uint16(entries.length);
    for (const [key, value] of entries) {
      w.string(key);
      w.string(String(value));
    }
    w.uint16(0); // metadata_count
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const execution_count = r.uint16();
    const dataCount = r.uint16();
    const data = {};
    for (let i = 0; i < dataCount; i++) {
      data[r.string()] = r.string();
    }
    r.uint16(); // metadata_count
    return { execution_count, data, metadata: {} };
  }
}

class CommOpenConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.comm_id || '');
    w.uint16(c.target_id ?? 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { comm_id: r.string(), target_id: r.uint16() };
  }
}

class CommMsgConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.comm_id || '');
    w.uint32(c.data ?? 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { comm_id: r.string(), data: r.uint32() };
  }
}

class CommCloseConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.comm_id || '');
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { comm_id: r.string() };
  }
}

class AuthRequestConverter {
  static toBinary(c) {
    const w = new BinaryWriter();
    w.string(c.device_id || '');
    w.uint32(c.timestamp >>> 0);
    const hmac = c.hmac || [];
    if (!Array.isArray(hmac) || hmac.length !== 32) {
      throw new Error('auth_request.hmac must be an array of 32 bytes');
    }
    const hmacBuf = Buffer.from(hmac);
    w.bytes(hmacBuf);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    const device_id = r.string();
    const timestamp = r.uint32();
    const hmacBuf = r.bytes(32);
    return { device_id, timestamp, hmac: Array.from(hmacBuf) };
  }
}

class AuthReplyConverter {
  static toBinary(c) {
    const w = new BinaryWriter(1);
    w.uint8(c.status >>> 0);
    return w.toBuffer();
  }

  static fromBinary(buf) {
    const r = new BinaryReader(buf);
    return { status: r.uint8() };
  }
}

class TargetNotFoundConverter {
  static toBinary() { return Buffer.alloc(0); }
  static fromBinary() { return {}; }
}

// --- Converter registry ---

const MESSAGE_CONVERTERS = {
  'kernel_info_request': KernelInfoRequestConverter,
  'kernel_info_reply': KernelInfoReplyConverter,
  'execute_request': ExecuteRequestConverter,
  'execute_reply': ExecuteReplyConverter,
  'stream': StreamConverter,
  'error': ErrorConverter,
  'display_data': DisplayDataConverter,
  'status': StatusConverter,
  'input_request': InputRequestConverter,
  'input_reply': InputReplyConverter,
  'complete_request': CompleteRequestConverter,
  'complete_reply': CompleteReplyConverter,
  'inspect_request': InspectRequestConverter,
  'inspect_reply': InspectReplyConverter,
  'is_complete_request': IsCompleteRequestConverter,
  'is_complete_reply': IsCompleteReplyConverter,
  'shutdown_request': ShutdownRequestConverter,
  'shutdown_reply': ShutdownReplyConverter,
  'interrupt_request': InterruptRequestConverter,
  'interrupt_reply': InterruptReplyConverter,
  'execute_result': ExecuteResultConverter,
  'comm_open': CommOpenConverter,
  'comm_msg': CommMsgConverter,
  'comm_close': CommCloseConverter,
  'auth_request': AuthRequestConverter,
  'auth_reply': AuthReplyConverter,
  'target_not_found': TargetNotFoundConverter,
};

// --- Top-level protocol ---

class JupyterMessageProtocol {
  static jsonToBinary(jsonMessage) {
    if (!jsonMessage?.header?.msg_type) {
      throw new Error('Invalid message: header.msg_type is required');
    }

    const header = HeaderConverter.toBinary(jsonMessage.header);

    const ph = jsonMessage.parent_header;
    const parentHeader = (ph && ph.msg_type)
      ? HeaderConverter.toBinary(ph)
      : Buffer.alloc(0);

    const metadata = Buffer.alloc(0);

    const msgType = jsonMessage.header.msg_type;
    const converter = MESSAGE_CONVERTERS[msgType];
    if (!converter) {
      throw new Error(`Unsupported message type: ${msgType}`);
    }

    const content = converter.toBinary(jsonMessage.content || {});
    const buffers = Buffer.alloc(0);

    const w = new BinaryWriter(10 + header.length + parentHeader.length + content.length);
    w.uint16(header.length);
    w.uint16(parentHeader.length);
    w.uint16(metadata.length);
    w.uint16(content.length);
    w.uint16(buffers.length);
    w.bytes(header);
    w.bytes(parentHeader);
    w.bytes(content);

    return w.toBuffer();
  }

  static binaryToJson(binaryMessage) {
    const r = new BinaryReader(binaryMessage);

    const headerLen = r.uint16();
    const parentHeaderLen = r.uint16();
    const metadataLen = r.uint16();
    const contentLen = r.uint16();
    const buffersLen = r.uint16();

    const header = HeaderConverter.fromBinary(r.bytes(headerLen));

    let parent_header = {};
    if (parentHeaderLen > 0) {
      parent_header = HeaderConverter.fromBinary(r.bytes(parentHeaderLen));
    }

    // skip metadata
    if (metadataLen > 0) r.bytes(metadataLen);

    let content = {};
    if (contentLen > 0) {
      const contentBuf = r.bytes(contentLen);
      const converter = MESSAGE_CONVERTERS[header.msg_type];
      if (!converter) {
        throw new Error(`Unsupported message type: ${header.msg_type}`);
      }
      content = converter.fromBinary(contentBuf);
    }

    return { header, parent_header, metadata: {}, content, buffers: [] };
  }
}

// --- Exports ---

export {
  JupyterMessageProtocol,
  MSG_TYPES,
  MSG_TYPE_NAMES,
  STATUS,
  STATUS_NAMES,
  EXECUTION_STATE,
  EXECUTION_STATE_NAMES,
  IS_COMPLETE_STATUS,
  IS_COMPLETE_STATUS_NAMES,
  MESSAGE_CONVERTERS as converters,
  HeaderConverter,
  BinaryReader,
  BinaryWriter,
};
