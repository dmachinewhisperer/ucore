// Jupyter Messaging Protocol Binary Library
// Supports conversion between JSON and binary formats

// Message type constants
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
  EXECUTE_RESULT: 0x14,
  COMM_OPEN: 0x15,
  COMM_MSG: 0x16,
  COMM_CLOSE: 0x17,

  //control messages
  AUTH_REQUEST: 0x64,
  AUTH_REPLY: 0x65,
  TARGET_NOT_FOUND: 0x66,
};

// Reverse mapping for binary to JSON conversion
const MSG_TYPE_NAMES = Object.fromEntries(
  Object.entries(MSG_TYPES).map(([name, value]) => [value, name.toLowerCase()])
);

// Status constants
const STATUS = {
  OK: 0,
  ERROR: 1,
  ABORT: 2
};

const STATUS_NAMES = ['ok', 'error', 'abort'];

// Execution state constants
const EXECUTION_STATE = {
  BUSY: 0,
  IDLE: 1,
  STARTING: 2,
  DEAD: 3
};

const EXECUTION_STATE_NAMES = ['busy', 'idle', 'starting', 'dead'];

// Utility functions for binary operations
class BinaryUtils {
  static writeUInt16BE(buffer, value, offset) {
    buffer.writeUInt16BE(value, offset);
    return offset + 2;
  }

  static writeUInt32BE(buffer, value, offset) {
    buffer.writeUInt32BE(value, offset);
    return offset + 4;
  }

  static writeUInt8(buffer, value, offset) {
    buffer.writeUInt8(value, offset);
    return offset + 1;
  }

  static writeString(buffer, str, offset) {
    const len = Buffer.byteLength(str, 'utf8');
    offset = this.writeUInt16BE(buffer, len, offset);
    buffer.write(str, offset, len, 'utf8');
    return offset + len;
  }

  static writeVersion(buffer, version, offset) {
    const parts = version.split('.').map(Number);
    buffer.writeUInt8(parts[0] || 0, offset);
    buffer.writeUInt8(parts[1] || 0, offset + 1);
    buffer.writeUInt8(parts[2] || 0, offset + 2);
    return offset + 3;
  }

  static readUInt16BE(buffer, offset) {
    return { value: buffer.readUInt16BE(offset), offset: offset + 2 };
  }

  static readUInt32BE(buffer, offset) {
    return { value: buffer.readUInt32BE(offset), offset: offset + 4 };
  }

  static readUInt8(buffer, offset) {
    return { value: buffer.readUInt8(offset), offset: offset + 1 };
  }

  static readString(buffer, offset) {
    const { value: len, offset: newOffset } = this.readUInt16BE(buffer, offset);
    const str = buffer.toString('utf8', newOffset, newOffset + len);
    return { value: str, offset: newOffset + len };
  }

  static readVersion(buffer, offset) {
    const major = buffer.readUInt8(offset);
    const minor = buffer.readUInt8(offset + 1);
    const patch = buffer.readUInt8(offset + 2);
    return { value: `${major}.${minor}.${patch}`, offset: offset + 3 };
  }
}

// Header conversion functions
//header type 2 is the default, see jmp_bin.specs
class HeaderConverter {
  static jsonToBinary(header) {
    const msgId = header.msg_id || '';
    const sessionId = header.session || '';
    const username = header.username || '';
    const msgTypeStr = header.msg_type || '';
    const msgType = MSG_TYPES[msgTypeStr.toUpperCase()] || 0;
    const version = header.version || '5.0.0';

    const msgIdLen = Buffer.byteLength(msgId, 'utf8');
    const sessionIdLen = Buffer.byteLength(sessionId, 'utf8');
    const usernameLen = Buffer.byteLength(username, 'utf8');

    const bufferSize =
      2 + msgIdLen +
      2 + sessionIdLen +
      2 + usernameLen +
      1 + 3;  // msg_type + version

    const buffer = Buffer.alloc(bufferSize);
    let offset = 0;

    offset = BinaryUtils.writeString(buffer, msgId, offset);
    offset = BinaryUtils.writeString(buffer, sessionId, offset);
    offset = BinaryUtils.writeString(buffer, username, offset);
    offset = BinaryUtils.writeUInt8(buffer, msgType, offset);
    BinaryUtils.writeVersion(buffer, version, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;

    const { value: msgId, offset: o1 } = BinaryUtils.readString(buffer, offset);
    const { value: sessionId, offset: o2 } = BinaryUtils.readString(buffer, o1);
    const { value: username, offset: o3 } = BinaryUtils.readString(buffer, o2);
    const { value: msgType, offset: o4 } = BinaryUtils.readUInt8(buffer, o3);
    const { value: version } = BinaryUtils.readVersion(buffer, o4);

    return {
      msg_id: msgId,
      session: sessionId,
      username,
      date: new Date().toISOString(),
      msg_type: MSG_TYPE_NAMES[msgType] || 'unknown',
      version
    };
  }
}

/*
//header type 1 
class HeaderConverter {
  static jsonToBinary(header) {
    const msgId = parseInt(header.msg_id) || 0;
    const sessionId = parseInt(header.session) || 0;
    const username = header.username || '';
    const msgTypeStr = header.msg_type || '';
    const msgType = MSG_TYPES[msgTypeStr.toUpperCase()] || 0;
    const version = header.version || '5.0.0';

    const usernameLen = Buffer.byteLength(username, 'utf8');
    const bufferSize = 4 + 4 + 2 + usernameLen + 1 + 3;
    const buffer = Buffer.alloc(bufferSize);

    let offset = 0;
    offset = BinaryUtils.writeUInt32BE(buffer, msgId, offset);
    offset = BinaryUtils.writeUInt32BE(buffer, sessionId, offset);
    offset = BinaryUtils.writeString(buffer, username, offset);
    offset = BinaryUtils.writeUInt8(buffer, msgType, offset);
    BinaryUtils.writeVersion(buffer, version, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;
    const { value: msgId, offset: o1 } = BinaryUtils.readUInt32BE(buffer, offset);
    const { value: sessionId, offset: o2 } = BinaryUtils.readUInt32BE(buffer, o1);
    const { value: username, offset: o3 } = BinaryUtils.readString(buffer, o2);
    const { value: msgType, offset: o4 } = BinaryUtils.readUInt8(buffer, o3);
    const { value: version } = BinaryUtils.readVersion(buffer, o4);

    return {
      msg_id: msgId.toString(),
      session: sessionId.toString(),
      username,
      date: new Date().toISOString(),
      msg_type: MSG_TYPE_NAMES[msgType] || 'unknown',
      version
    };
  }
}
*/
// Message type converters
class ExecuteRequestConverter {
  static jsonToBinary(content) {
    const code = content.code || '';
    const flags =
      (content.silent ? 1 : 0) |
      (content.store_history ? 2 : 0) |
      (content.allow_stdin ? 4 : 0) |
      (content.stop_on_error ? 8 : 0);

    const codeLen = Buffer.byteLength(code, 'utf8');
    const buffer = Buffer.alloc(2 + codeLen + 1);

    let offset = 0;
    offset = BinaryUtils.writeString(buffer, code, offset);
    BinaryUtils.writeUInt8(buffer, flags, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;
    const { value: code, offset: o1 } = BinaryUtils.readString(buffer, offset);
    const { value: flags } = BinaryUtils.readUInt8(buffer, o1);

    return {
      code,
      silent: !!(flags & 1),
      store_history: !!(flags & 2),
      allow_stdin: !!(flags & 4),
      stop_on_error: !!(flags & 8),
      user_expressions: {}
    };
  }
}

class ExecuteReplyConverter {
  static jsonToBinary(content) {
    if (content.status !== 'ok') {
      return ErrorConverter.jsonToBinary(content);
    }

    const buffer = Buffer.alloc(3);
    let offset = 0;
    offset = BinaryUtils.writeUInt8(buffer, STATUS.OK, offset);
    BinaryUtils.writeUInt16BE(buffer, content.execution_count || 0, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: status } = BinaryUtils.readUInt8(buffer, 0);

    if (status !== STATUS.OK) {
      return ErrorConverter.binaryToJson(buffer);
    }

    const { value: executionCount } = BinaryUtils.readUInt16BE(buffer, 1);

    return {
      status: 'ok',
      execution_count: executionCount
    };
  }
}

class KernelInfoRequestConverter {
  static jsonToBinary(content) {
    return Buffer.alloc(0);
  }

  static binaryToJson(buffer) {
    return {};
  }
}

class KernelInfoReplyConverter {
  static jsonToBinary(content) {
    if (content.status !== 'ok') {
      return ErrorConverter.jsonToBinary(content);
    }

    const impl = content.implementation || '';
    const langName = content.language_info?.name || '';
    const langMimetype = content.language_info?.mimetype || '';
    const langFileExt = content.language_info?.file_extension || '';
    const banner = content.banner || '';

    const bufferSize = 1 + 3 + 2 + Buffer.byteLength(impl, 'utf8') + 3 +
      2 + Buffer.byteLength(langName, 'utf8') + 3 +
      2 + Buffer.byteLength(langMimetype, 'utf8') +
      2 + Buffer.byteLength(langFileExt, 'utf8') +
      2 + Buffer.byteLength(banner, 'utf8') + 1;

    const buffer = Buffer.alloc(bufferSize);
    let offset = 0;

    offset = BinaryUtils.writeUInt8(buffer, STATUS.OK, offset);
    offset = BinaryUtils.writeVersion(buffer, content.protocol_version || '5.0.0', offset);
    offset = BinaryUtils.writeString(buffer, impl, offset);
    offset = BinaryUtils.writeVersion(buffer, content.implementation_version || '1.0.0', offset);
    offset = BinaryUtils.writeString(buffer, langName, offset);
    offset = BinaryUtils.writeVersion(buffer, content.language_info?.version || '1.0.0', offset);
    offset = BinaryUtils.writeString(buffer, langMimetype, offset);
    offset = BinaryUtils.writeString(buffer, langFileExt, offset);
    offset = BinaryUtils.writeString(buffer, banner, offset);
    BinaryUtils.writeUInt8(buffer, content.debugger ? 1 : 0, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: status } = BinaryUtils.readUInt8(buffer, 0);

    if (status !== STATUS.OK) {
      return ErrorConverter.binaryToJson(buffer);
    }

    let offset = 1;
    const { value: protocolVersion, offset: o1 } = BinaryUtils.readVersion(buffer, offset);
    const { value: implementation, offset: o2 } = BinaryUtils.readString(buffer, o1);
    const { value: implVersion, offset: o3 } = BinaryUtils.readVersion(buffer, o2);
    const { value: langName, offset: o4 } = BinaryUtils.readString(buffer, o3);
    const { value: langVersion, offset: o5 } = BinaryUtils.readVersion(buffer, o4);
    const { value: langMimetype, offset: o6 } = BinaryUtils.readString(buffer, o5);
    const { value: langFileExt, offset: o7 } = BinaryUtils.readString(buffer, o6);
    const { value: banner, offset: o8 } = BinaryUtils.readString(buffer, o7);
    const { value: _debugger } = BinaryUtils.readUInt8(buffer, o8);

    return {
      status: 'ok',
      protocol_version: protocolVersion,
      implementation,
      implementation_version: implVersion,
      language_info: {
        name: langName,
        version: langVersion,
        mimetype: langMimetype,
        file_extension: langFileExt
      },
      banner,
      _debugger: !!_debugger
    };
  }
}

class StreamConverter {
  static jsonToBinary(content) {
    const name = content.name === 'stderr' ? 1 : 0;  // default to stdout
    const text = content.text || '';

    const textByteLength = Buffer.byteLength(text, 'utf8');
    const bufferSize = 1 + 2 + textByteLength; // 1 byte for name, 2 for length, rest for text

    const buffer = Buffer.alloc(bufferSize);
    let offset = 0;

    buffer.writeUInt8(name, offset);  // Write the name code
    offset += 1;

    offset = BinaryUtils.writeString(buffer, text, offset); // Writes 2-byte length + UTF-8 bytes

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;

    const nameCode = buffer.readUInt8(offset);
    const name = nameCode === 1 ? 'stderr' : 'stdout';  // Default fallback is stdout
    offset += 1;

    const { value: text } = BinaryUtils.readString(buffer, offset);

    return { name, text };
  }
}

class StatusConverter {
  static jsonToBinary(content) {
    const stateMap = { busy: 0, idle: 1, starting: 2 };
    const state = stateMap[content.execution_state] || 0;

    const buffer = Buffer.alloc(1);
    BinaryUtils.writeUInt8(buffer, state, 0);
    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: state } = BinaryUtils.readUInt8(buffer, 0);
    return {
      execution_state: EXECUTION_STATE_NAMES[state] || 'busy'
    };
  }
}

class InputRequestConverter {
  static jsonToBinary(content) {
    const prompt = content.prompt || '';
    const password = content.password || false;

    const bufferSize = 2 + Buffer.byteLength(prompt, 'utf8') + 1;
    const buffer = Buffer.alloc(bufferSize);

    let offset = 0;
    offset = BinaryUtils.writeString(buffer, prompt, offset);
    BinaryUtils.writeUInt8(buffer, password ? 1 : 0, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;
    const { value: prompt, offset: o1 } = BinaryUtils.readString(buffer, offset);
    const { value: password } = BinaryUtils.readUInt8(buffer, o1);

    return { prompt, password: !!password };
  }
}

class InputReplyConverter {
  static jsonToBinary(content) {
    const value = content.value || '';
    const buffer = Buffer.alloc(2 + Buffer.byteLength(value, 'utf8'));
    BinaryUtils.writeString(buffer, value, 0);
    return buffer;
  }

  static binaryToJson(buffer) {
    const { value } = BinaryUtils.readString(buffer, 0);
    return { value };
  }
}

class ShutdownRequestConverter {
  static jsonToBinary(content) {
    const buffer = Buffer.alloc(1);
    BinaryUtils.writeUInt8(buffer, content.restart ? 1 : 0, 0);
    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: restart } = BinaryUtils.readUInt8(buffer, 0);
    return { restart: !!restart };
  }
}

class ShutdownReplyConverter {
  static jsonToBinary(content) {
    if (content.status !== 'ok') {
      return ErrorConverter.jsonToBinary(content);
    }

    const buffer = Buffer.alloc(2);
    let offset = 0;
    offset = BinaryUtils.writeUInt8(buffer, STATUS.OK, offset);
    BinaryUtils.writeUInt8(buffer, content.restart ? 1 : 0, offset);
    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: status } = BinaryUtils.readUInt8(buffer, 0);

    if (status !== STATUS.OK) {
      return ErrorConverter.binaryToJson(buffer);
    }

    const { value: restart } = BinaryUtils.readUInt8(buffer, 1);
    return { status: 'ok', restart: !!restart };
  }
}

class ErrorConverter {
  static jsonToBinary(content) {
    const ename = content.ename || '';
    const evalue = content.evalue || '';
    const traceback = Array.isArray(content.traceback) ? content.traceback.join('\n') : '';
    const execution_count = content.execution_count || 0;

    const bufferSize = 1 + 2 + Buffer.byteLength(ename, 'utf8') +
      2 + Buffer.byteLength(evalue, 'utf8') +
      2 + Buffer.byteLength(traceback, 'utf8') +
      2;
    const buffer = Buffer.alloc(bufferSize);

    let offset = 0;
    offset = BinaryUtils.writeUInt8(buffer, STATUS.ERROR, offset);
    offset = BinaryUtils.writeString(buffer, ename, offset);
    offset = BinaryUtils.writeString(buffer, evalue, offset);
    offset = BinaryUtils.writeString(buffer, traceback, offset);
    BinaryUtils.writeUInt16(buffer, execution_count, offset);

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;
    const { value: status, offset: o1 } = BinaryUtils.readUInt8(buffer, offset);

    if (status === STATUS.ABORT) {
      return { status: 'abort' };
    }

    const { value: ename, offset: o2 } = BinaryUtils.readString(buffer, o1);
    const { value: evalue, offset: o3 } = BinaryUtils.readString(buffer, o2);
    const { value: traceback, offset: o4 } = BinaryUtils.readString(buffer, o3);
    const { value: execution_count } = BinaryUtils.readUInt16BE(buffer, o4);

    // return {
    //   status: 'error',
    //   ename,
    //   evalue,
    //   traceback: traceback.split('\n').filter(line => line.trim()),
    //   execution_count
    // };
    return {
      status: 'error',
      ename,
      evalue,
      traceback: (() => {
        const lines = traceback
          .split('\n')
          .filter(line => line.trim());
        return lines.length > 0
          ? lines
          : ['No traceback available.'];
      })(),
      execution_count
    };
  }
}

//other custom converters (used for control) not implemented in standard JMP
class AuthRequestConverter {
  static jsonToBinary(content) {
    const deviceId = content.device_id || '';
    const timestamp = content.timestamp >>> 0; // force to uint32
    const hmac = content.hmac || [];

    if (!Array.isArray(hmac) || hmac.length !== 32) {
      throw new Error('auth_request.hmac must be an array of 32 bytes');
    }

    const deviceIdLen = Buffer.byteLength(deviceId, 'utf8');
    const bufferSize = 2 + deviceIdLen + 4 + 32;

    const buffer = Buffer.alloc(bufferSize);
    let offset = 0;

    offset = BinaryUtils.writeString(buffer, deviceId, offset);
    offset = BinaryUtils.writeUInt32BE(buffer, timestamp, offset);

    for (let i = 0; i < 32; i++) {
      buffer.writeUInt8(hmac[i] || 0, offset++);
    }

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;

    const { value: device_id, offset: o1 } = BinaryUtils.readString(buffer, offset);
    const { value: timestamp, offset: o2 } = BinaryUtils.readUInt32BE(buffer, o1);

    const hmac = [];
    for (let i = 0; i < 32; i++) {
      hmac.push(buffer.readUInt8(o2 + i));
    }

    return {
      device_id,
      timestamp,
      hmac
    };
  }
}
class AuthReplyConverter {
  static jsonToBinary(content) {
    const status = content.status >>> 0;
    const buffer = Buffer.alloc(1);
    BinaryUtils.writeUInt8(buffer, status, 0);
    return buffer;
  }

  static binaryToJson(buffer) {
    const { value: status } = BinaryUtils.readUInt8(buffer, 0);
    return { status };
  }
}

class ExecuteResultConverter {
  static jsonToBinary(content) {
    const executionCount = content.execution_count || 0;
    const data = content.data || {};
    const metadata = content.metadata || {};

    const dataEntries = Object.entries(data);
    const dataCount = dataEntries.length;

    let bufferSize = 2 + 2; // execution_count + data_count

    for (const [key, value] of dataEntries) {
      bufferSize += 2 + Buffer.byteLength(key, 'utf8');
      bufferSize += 2 + Buffer.byteLength(value, 'utf8');
    }

    bufferSize += 2; // metadata_count (always 0)

    const buffer = Buffer.alloc(bufferSize);
    let offset = 0;

    offset = BinaryUtils.writeUInt16BE(buffer, executionCount, offset);
    offset = BinaryUtils.writeUInt16BE(buffer, dataCount, offset);

    for (const [key, value] of dataEntries) {
      offset = BinaryUtils.writeString(buffer, key, offset);
      offset = BinaryUtils.writeString(buffer, value, offset);
    }

    BinaryUtils.writeUInt16BE(buffer, 0, offset); // metadata_count = 0

    return buffer;
  }

  static binaryToJson(buffer) {
    let offset = 0;

    const { value: executionCount, offset: o1 } = BinaryUtils.readUInt16BE(buffer, offset);
    const { value: dataCount, offset: o2 } = BinaryUtils.readUInt16BE(buffer, o1);

    offset = o2;
    const data = {};

    for (let i = 0; i < dataCount; i++) {
      const { value: key, offset: o3 } = BinaryUtils.readString(buffer, offset);
      const { value: value, offset: o4 } = BinaryUtils.readString(buffer, o3);
      data[key] = value;
      offset = o4;
    }

    // Read metadata_count (last 2 bytes)
    const { value: metadataCount } = BinaryUtils.readUInt16BE(buffer, offset);

    return {
      execution_count: executionCount,
      data,
      metadata: {} // metadata not supported yet
    };
  }
}

// Message type converter registry
const MESSAGE_CONVERTERS = {
  'execute_request': ExecuteRequestConverter,
  'execute_reply': ExecuteReplyConverter,
  'kernel_info_request': KernelInfoRequestConverter,
  'kernel_info_reply': KernelInfoReplyConverter,
  'stream': StreamConverter,
  'status': StatusConverter,
  'input_request': InputRequestConverter,
  'input_reply': InputReplyConverter,
  'shutdown_request': ShutdownRequestConverter,
  'shutdown_reply': ShutdownReplyConverter,
  'error': ErrorConverter,
  'auth_request': AuthRequestConverter,
  'auth_reply': AuthReplyConverter,
  'execute_result': ExecuteResultConverter
};

// High-level conversion functions
class JupyterMessageProtocol {
  /**
   * Convert a JSON message to binary format
   * @param {Object} jsonMessage - The JSON message object
   * @returns {Buffer} - The binary representation
   */
  static jsonToBinary(jsonMessage) {
    // Validate input
    if (!jsonMessage || typeof jsonMessage !== 'object') {
      throw new Error('Invalid JSON message: must be an object');
    }

    if (!jsonMessage.header || typeof jsonMessage.header !== 'object') {
      throw new Error('Invalid JSON message: header is required and must be an object');
    }

    if (!jsonMessage.header.msg_type) {
      throw new Error('Invalid JSON message: header.msg_type is required');
    }

    const header = HeaderConverter.jsonToBinary(jsonMessage.header);
    const parentHeader = jsonMessage.parent_header ?
      HeaderConverter.jsonToBinary(jsonMessage.parent_header) : Buffer.alloc(0);
    const metadata = Buffer.alloc(0); // unused

    const msgType = jsonMessage.header.msg_type;
    const converter = MESSAGE_CONVERTERS[msgType];

    if (!converter) {
      throw new Error(`Unsupported message type: ${msgType}`);
    }

    const content = converter.jsonToBinary(jsonMessage.content || {});
    const buffers = Buffer.alloc(0); // unused

    // Create the main buffer with length prefixes
    const totalSize = 2 + 2 + 2 + 2 + 2 +
      header.length + parentHeader.length +
      metadata.length + content.length + buffers.length;

    const buffer = Buffer.alloc(totalSize);
    let offset = 0;

    // Write length prefixes
    offset = BinaryUtils.writeUInt16BE(buffer, header.length, offset);
    offset = BinaryUtils.writeUInt16BE(buffer, parentHeader.length, offset);
    offset = BinaryUtils.writeUInt16BE(buffer, metadata.length, offset);
    offset = BinaryUtils.writeUInt16BE(buffer, content.length, offset);
    offset = BinaryUtils.writeUInt16BE(buffer, buffers.length, offset);

    // Write data sections
    header.copy(buffer, offset);
    offset += header.length;

    if (parentHeader.length > 0) {
      parentHeader.copy(buffer, offset);
      offset += parentHeader.length;
    }

    if (content.length > 0) {
      content.copy(buffer, offset);
      offset += content.length;
    }

    return buffer;
  }

  /**
   * Convert a binary message to JSON format
   * @param {Buffer} binaryMessage - The binary message buffer
   * @returns {Object} - The JSON representation
   */
  static binaryToJson(binaryMessage) {
    let offset = 0;

    // Read length prefixes
    const { value: headerLen, offset: o1 } = BinaryUtils.readUInt16BE(binaryMessage, offset);
    const { value: parentHeaderLen, offset: o2 } = BinaryUtils.readUInt16BE(binaryMessage, o1);
    const { value: metadataLen, offset: o3 } = BinaryUtils.readUInt16BE(binaryMessage, o2);
    const { value: contentLen, offset: o4 } = BinaryUtils.readUInt16BE(binaryMessage, o3);
    const { value: buffersLen, offset: o5 } = BinaryUtils.readUInt16BE(binaryMessage, o4);

    offset = o5;

    // Read header
    const headerBuffer = binaryMessage.slice(offset, offset + headerLen);
    const header = HeaderConverter.binaryToJson(headerBuffer);
    offset += headerLen;

    // Read parent header if present
    let parentHeader = {};
    if (parentHeaderLen > 0) {
      const parentHeaderBuffer = binaryMessage.slice(offset, offset + parentHeaderLen);
      parentHeader = HeaderConverter.binaryToJson(parentHeaderBuffer);
      offset += parentHeaderLen;
    }

    // Skip metadata (unused)
    offset += metadataLen;

    // Read content
    let content = {};
    if (contentLen > 0) {
      const contentBuffer = binaryMessage.slice(offset, offset + contentLen);
      const converter = MESSAGE_CONVERTERS[header.msg_type];

      if (!converter) {
        throw new Error(`Unsupported message type: ${header.msg_type}`);
      }

      content = converter.binaryToJson(contentBuffer);
    }

    return {
      header,
      parent_header: parentHeader,
      metadata: {},
      content,
      buffers: []
    };
  }
}



// Export the main class and utilities
// module.exports = {
//   JupyterMessageProtocol,
//   MSG_TYPES,
//   STATUS,
//   EXECUTION_STATE,
//   // Export individual converters for direct use if needed
//   converters: MESSAGE_CONVERTERS,
//   HeaderConverter,
//   BinaryUtils
// };

export {
  JupyterMessageProtocol,
  MSG_TYPES,
  STATUS,
  EXECUTION_STATE,
  MESSAGE_CONVERTERS as converters,
  HeaderConverter,
  BinaryUtils
};
