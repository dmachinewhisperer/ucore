// JMP Binary Converter Test Suite
// Uses canonical vectors from jmp_bin.vectors.json
// Run: node --test jmp_converter.test.js

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

import {
  JupyterMessageProtocol,
  MSG_TYPES,
  MSG_TYPE_NAMES,
  converters,
  HeaderConverter,
  BinaryReader,
  BinaryWriter,
} from './jmp_converter.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const vectors = JSON.parse(
  fs.readFileSync(path.join(__dirname, '..', 'jmp_bin.vectors.json'), 'utf8')
);

// --- Helper ---

function resolveConverter(name, vec) {
  let msgType = vec.msg_type || name;
  if (converters[msgType]) return msgType;
  const base = msgType.replace(
    /_(ok|error|found|not_found|stdout|stderr|busy|idle|starting|complete|incomplete)$/,
    ''
  );
  if (converters[base]) return base;
  return null;
}

// --- BinaryReader tests ---

describe('BinaryReader', () => {
  it('reads uint8, uint16, uint32', () => {
    const buf = Buffer.from([0x42, 0x00, 0x0A, 0x00, 0x00, 0x00, 0xFF]);
    const r = new BinaryReader(buf);
    assert.equal(r.uint8(), 0x42);
    assert.equal(r.uint16(), 10);
    assert.equal(r.uint32(), 255);
  });

  it('reads length-prefixed strings', () => {
    const w = new BinaryWriter();
    w.string('hello');
    const r = new BinaryReader(w.toBuffer());
    assert.equal(r.string(), 'hello');
  });

  it('reads version tuples', () => {
    const buf = Buffer.from([5, 3, 1]);
    const r = new BinaryReader(buf);
    assert.equal(r.version(), '5.3.1');
  });

  it('throws on overflow', () => {
    const r = new BinaryReader(Buffer.alloc(2));
    r.uint16(); // ok
    assert.throws(() => r.uint8(), RangeError);
  });

  it('reads booleans', () => {
    const r = new BinaryReader(Buffer.from([0x00, 0x01, 0x05]));
    assert.equal(r.bool(), false);
    assert.equal(r.bool(), true);
    assert.equal(r.bool(), true); // any non-zero is true
  });
});

// --- BinaryWriter tests ---

describe('BinaryWriter', () => {
  it('writes and grows automatically', () => {
    const w = new BinaryWriter(2); // tiny initial capacity
    w.uint8(1);
    w.uint16(1000);
    w.uint32(100000);
    w.string('test');
    const buf = w.toBuffer();
    assert.equal(buf.length, 1 + 2 + 4 + 2 + 4);
  });

  it('roundtrips through reader', () => {
    const w = new BinaryWriter();
    w.uint8(0xFF);
    w.uint16(12345);
    w.uint32(0xDEADBEEF);
    w.string('hello world');
    w.version('5.3.0');
    w.bool(true);
    w.bool(false);

    const r = new BinaryReader(w.toBuffer());
    assert.equal(r.uint8(), 0xFF);
    assert.equal(r.uint16(), 12345);
    assert.equal(r.uint32(), 0xDEADBEEF);
    assert.equal(r.string(), 'hello world');
    assert.equal(r.version(), '5.3.0');
    assert.equal(r.bool(), true);
    assert.equal(r.bool(), false);
    assert.equal(r.remaining(), 0);
  });
});

// --- Header converter tests ---

describe('HeaderConverter', () => {
  it('roundtrips a header', () => {
    const original = {
      msg_id: '550e8400-e29b-41d4-a716-446655440000',
      session: 'abcd-1234',
      username: 'testuser',
      msg_type: 'execute_request',
      version: '5.3.0',
    };

    const binary = HeaderConverter.toBinary(original);
    const decoded = HeaderConverter.fromBinary(binary);

    assert.equal(decoded.msg_id, original.msg_id);
    assert.equal(decoded.session, original.session);
    assert.equal(decoded.username, original.username);
    assert.equal(decoded.msg_type, original.msg_type);
    assert.equal(decoded.version, original.version);
  });

  it('handles empty parent header', () => {
    const binary = HeaderConverter.toBinary({
      msg_id: '', session: '', username: '', msg_type: '', version: '5.3.0'
    });
    const decoded = HeaderConverter.fromBinary(binary);
    assert.equal(decoded.msg_id, '');
    assert.equal(decoded.msg_type, 'unknown');
  });
});

// --- Golden vector tests (content only) ---

describe('Golden vectors (content converters)', () => {
  for (const [name, vec] of Object.entries(vectors)) {
    if (name === '_comment') continue;

    const msgType = resolveConverter(name, vec);
    if (!msgType) {
      it(`${name} — SKIPPED (no converter found)`);
      continue;
    }

    const converter = converters[msgType];

    it(`${name}: toBinary matches expected hex`, () => {
      const binary = converter.toBinary(vec.json);
      assert.equal(binary.toString('hex'), vec.binary,
        `toBinary mismatch for ${name}`);
    });

    it(`${name}: fromBinary produces expected JSON`, () => {
      if (vec.binary === '') {
        // empty content — fromBinary should return empty-ish object
        const result = converter.fromBinary(Buffer.alloc(0));
        assert.deepEqual(result, vec.json);
        return;
      }

      const buf = Buffer.from(vec.binary, 'hex');
      const result = converter.fromBinary(buf);
      assert.deepEqual(result, vec.json,
        `fromBinary mismatch for ${name}`);
    });

    it(`${name}: roundtrip toBinary -> fromBinary`, () => {
      const binary = converter.toBinary(vec.json);
      const decoded = converter.fromBinary(binary);
      assert.deepEqual(decoded, vec.json,
        `roundtrip mismatch for ${name}`);
    });
  }
});

// --- Full envelope tests ---

describe('JupyterMessageProtocol (full envelope)', () => {
  it('roundtrips a full execute_request message', () => {
    const msg = {
      header: {
        msg_id: 'test-123',
        session: 'session-456',
        username: 'user',
        msg_type: 'execute_request',
        version: '5.3.0',
      },
      parent_header: {},
      metadata: {},
      content: {
        code: 'print("hello")',
        silent: false,
        store_history: true,
        user_expressions: {},
        allow_stdin: false,
        stop_on_error: true,
      },
      buffers: [],
    };

    const binary = JupyterMessageProtocol.jsonToBinary(msg);
    const decoded = JupyterMessageProtocol.binaryToJson(binary);

    assert.equal(decoded.header.msg_id, msg.header.msg_id);
    assert.equal(decoded.header.msg_type, 'execute_request');
    assert.equal(decoded.content.code, 'print("hello")');
    assert.equal(decoded.content.store_history, true);
    assert.equal(decoded.content.allow_stdin, false);
  });

  it('roundtrips a message with parent_header', () => {
    const msg = {
      header: {
        msg_id: 'reply-789',
        session: 'session-456',
        username: 'kernel',
        msg_type: 'execute_reply',
        version: '5.3.0',
      },
      parent_header: {
        msg_id: 'req-123',
        session: 'session-456',
        username: 'user',
        msg_type: 'execute_request',
        version: '5.3.0',
      },
      metadata: {},
      content: { status: 'ok', execution_count: 7 },
      buffers: [],
    };

    const binary = JupyterMessageProtocol.jsonToBinary(msg);
    const decoded = JupyterMessageProtocol.binaryToJson(binary);

    assert.equal(decoded.parent_header.msg_id, 'req-123');
    assert.equal(decoded.parent_header.msg_type, 'execute_request');
    assert.equal(decoded.content.execution_count, 7);
  });

  it('rejects unknown message types', () => {
    const msg = {
      header: { msg_id: 'x', session: 'x', username: 'x', msg_type: 'bogus_request', version: '5.3.0' },
      content: {},
    };
    assert.throws(() => JupyterMessageProtocol.jsonToBinary(msg), /Unsupported/);
  });

  it('handles every registered message type without throwing', () => {
    for (const [name, vec] of Object.entries(vectors)) {
      if (name === '_comment') continue;
      const msgType = vec.msg_type || resolveConverter(name, vec);
      if (!msgType) continue;

      const msg = {
        header: {
          msg_id: `test-${name}`,
          session: 'test-session',
          username: 'test',
          msg_type: msgType,
          version: '5.3.0',
        },
        parent_header: {},
        metadata: {},
        content: vec.json,
        buffers: [],
      };

      // Should not throw
      const binary = JupyterMessageProtocol.jsonToBinary(msg);
      const decoded = JupyterMessageProtocol.binaryToJson(binary);
      assert.equal(decoded.header.msg_type, msgType);
    }
  });
});

// --- Edge case tests ---

describe('Edge cases', () => {
  it('handles empty code in execute_request', () => {
    const c = converters['execute_request'];
    const json = { code: '', silent: true, store_history: false, allow_stdin: false, stop_on_error: false, user_expressions: {} };
    const binary = c.toBinary(json);
    const decoded = c.fromBinary(binary);
    assert.equal(decoded.code, '');
    assert.equal(decoded.silent, true);
  });

  it('handles unicode strings', () => {
    const c = converters['stream'];
    const json = { name: 'stdout', text: 'hello \u00e9\u00e8\u00ea \u2603' };
    const binary = c.toBinary(json);
    const decoded = c.fromBinary(binary);
    assert.equal(decoded.text, json.text);
  });

  it('handles zero-length matches in complete_reply', () => {
    const c = converters['complete_reply'];
    const json = { matches: [], cursor_start: 0, cursor_end: 0, metadata: {}, status: 'ok' };
    const binary = c.toBinary(json);
    const decoded = c.fromBinary(binary);
    assert.deepEqual(decoded.matches, []);
  });

  it('handles multiple matches in complete_reply', () => {
    const c = converters['complete_reply'];
    const json = {
      matches: ['print', 'property', 'process'],
      cursor_start: 0, cursor_end: 2, metadata: {}, status: 'ok'
    };
    const binary = c.toBinary(json);
    const decoded = c.fromBinary(binary);
    assert.deepEqual(decoded.matches, ['print', 'property', 'process']);
  });

  it('handles abort status', () => {
    const c = converters['error'];
    const json = { status: 'abort' };
    // Abort is status=2 with no fields after
    const buf = Buffer.from([0x02]);
    const decoded = c.fromBinary(buf);
    assert.equal(decoded.status, 'abort');
  });

  it('handles multiple MIME types in execute_result', () => {
    const c = converters['execute_result'];
    const json = {
      execution_count: 3,
      data: { 'text/plain': '42', 'text/html': '<b>42</b>' },
      metadata: {}
    };
    const binary = c.toBinary(json);
    const decoded = c.fromBinary(binary);
    assert.equal(decoded.data['text/plain'], '42');
    assert.equal(decoded.data['text/html'], '<b>42</b>');
    assert.equal(decoded.execution_count, 3);
  });
});
