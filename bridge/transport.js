import { SerialPort } from 'serialport';
import { v4 as uuidv4 } from 'uuid';
import * as net from 'net';
import WebSocket from 'ws';
import { decode as cobsDecode, encode as cobsEncode } from 'cobs';
import { JupyterMessageProtocol } from './jmp_converter.js';

/**
 * Abstract base transport class
 * Provides common interface for all transport implementations
 */
class BaseTransport {
    constructor() {
        this.messageHandler = null;
        this.controlMessageHandler = null;
        this.isConnected = false;
        this.connectHandler = null;
        this.disconnectHandler = null;
        this.errorHandler = null;
        this.sessionId = uuidv4();
        this.rxBuffer = Buffer.alloc(0);
    }

    async connect() {
        throw new Error('connect() must be implemented by subclass');
    }

    async disconnect() {
        throw new Error('disconnect() must be implemented by subclass');
    }

    async send(message) {
        throw new Error('send() must be implemented by subclass');
    }

    // Event handlers
    onMessage(handler) {
        this.messageHandler = handler;
    }

    onControlMessage(handler) {
        this.controlMessageHandler = handler;
    }

    onConnect(handler) {
        this.connectHandler = handler;
    }

    onDisconnect(handler) {
        this.disconnectHandler = handler;
    }

    onError(handler) {
        this.errorHandler = handler;
    }

    // Helper methods for subclasses
    _handleMessage(message) {
        if (this.messageHandler) {
            this.messageHandler(message);
        }
    }

    _handleControlMessage(message) {
        if (this.controlMessageHandler) {
            this.controlMessageHandler(message);
        }
    }

    _handleConnect() {
        this.isConnected = true;
        if (this.connectHandler) {
            this.connectHandler();
        }
    }

    _handleDisconnect() {
        this.isConnected = false;
        if (this.disconnectHandler) {
            this.disconnectHandler();
        }
    }

    _handleError(error) {
        if (this.errorHandler) {
            this.errorHandler(error);
        }
    }

    /**
     * Common COBS-framed data handler
     * Used by all transport types for uniform message processing
     */
    _handleFramedData(data) {

        this.rxBuffer = Buffer.concat([this.rxBuffer, data]);

        let delimiterIndex;
        while ((delimiterIndex = this.rxBuffer.indexOf(0x00)) !== -1) {
            const encodedFrame = this.rxBuffer.slice(0, delimiterIndex);
            this.rxBuffer = this.rxBuffer.slice(delimiterIndex + 1);

            if (encodedFrame.length === 0) continue;

            //log raw data recieved 
            //console.debug(`[RX encoded, len=${encodedFrame.length}]`, encodedFrame.toString('hex').match(/.{2}/g).join(' '));


            let decoded;
            try {
                decoded = cobsDecode(encodedFrame);
            } catch (error) {
                console.error('COBS decoding failed:', error);
                this._handleError(error);
                continue;
            }

            //log decoded data
            //console.debug(`[RX decoded, len=${decoded.length}]`, decoded.toString('hex').match(/.{2}/g).join(' '));


            try {
                if (decoded.length < 36) {
                    throw new Error(`Decoded data too short (len=${decoded.length})`);
                }

                //sessionid is a 36-bytes prefix added to support routing when multiclient. 
                //it is expected to be stripped out before the packet leaves the transport layer
                const sessionId = decoded.slice(0, 36).toString('utf8');
                const payload = decoded.slice(36);

                const message = JupyterMessageProtocol.binaryToJson(payload);
                console.debug(message);
                message._sessionId = sessionId;

                this._handleMessage(message);
            } catch (error) {
                console.error('Failed to parse decoded message:', error);
                this._handleError(error);
            }
        }
    }

    /**
     * Common method to encode and frame messages
     * Used by all transport types for uniform message sending
     */
    _encodeMessage(message) {
        const jsonMessage = typeof message === 'string' ? JSON.parse(message) : message;
        const binaryMsg = JupyterMessageProtocol.jsonToBinary(jsonMessage);
        const framedPayload = Buffer.concat([Buffer.from(this.sessionId), binaryMsg]);

        //console.debug(`[TX decoded, len=${framedPayload.length}]`, framedPayload.toString('hex').match(/.{2}/g).join(' '));

        const encoded = cobsEncode(framedPayload);
        const packet = Buffer.concat([encoded, Buffer.from([0x00])]);

        //console.debug(`[TX encoded, len=${packet.length}]`, packet.toString('hex').match(/.{2}/g).join(' '));

        return packet;
    }
}

/**
 * WebSocket transport implementation
 * Connects to a WebSocket server for kernel communication
 * Uses COBS framing for binary data like other transports
 */
class WebSocketTransport extends BaseTransport {
    constructor(config) {
        super();
        this.config = config;
        this.websocket = null;
        this.reconnectDelay = config.reconnect_delay || 5000;
    }

    async connect() {
        return new Promise((resolve, reject) => {
            const wsUrl = this.config.websocket_url || 'ws://localhost:8080/kernel';
            console.log(`Connecting to WebSocket server at ${wsUrl}`);

            this.websocket = new WebSocket(wsUrl);

            // Set binary type to handle binary data properly
            this.websocket.binaryType = 'arraybuffer';

            this.websocket.on('open', () => {
                console.log('WebSocket connection established');
                this._handleConnect();
                resolve();
            });

            this.websocket.on('message', (data) => {
                try {
                    // Convert data to Buffer for uniform processing
                    let buffer;
                    if (data instanceof ArrayBuffer) {
                        buffer = Buffer.from(data);
                    } else if (Buffer.isBuffer(data)) {
                        buffer = data;
                    } else if (typeof data === 'string') {
                        buffer = Buffer.from(data);
                    } else {
                        buffer = Buffer.from(data);
                    }

                    // Use common framed data handler
                    this._handleFramedData(buffer);
                } catch (error) {
                    console.error('Error processing WebSocket message:', error);
                    this._handleError(error);
                }
            });

            this.websocket.on('error', (error) => {
                console.error('WebSocket error:', error);
                this._handleError(error);
                reject(error);
            });

            this.websocket.on('close', () => {
                console.log('WebSocket connection closed');
                this._handleDisconnect();

                // Auto-reconnect if not intentionally disconnected
                if (this.isConnected) {
                    setTimeout(() => this.connect().catch(console.error), this.reconnectDelay);
                }
            });
        });
    }

    async disconnect() {
        this.isConnected = false;
        if (this.websocket) {
            this.websocket.close();
            this.websocket = null;
        }
    }

    async send(message) {
        if (this.websocket && this.websocket.readyState === WebSocket.OPEN) {
            try {
                // Use common encoding method
                const packet = this._encodeMessage(message);
                this.websocket.send(packet);
                return true;
            } catch (error) {
                console.error('Failed to encode or send message:', error);
                this._handleError(error);
                return false;
            }
        } else {
            console.error('WebSocket not connected, cannot send message');
            return false;
        }
    }
}

/**
 * TCP Socket transport implementation
 * Connects to a TCP server for kernel communication
 * Uses COBS framing for binary data like other transports
 */
class SocketTransport extends BaseTransport {
    constructor(config) {
        super();
        this.config = config;
        this.socket = null;
        this.reconnectDelay = config.reconnect_delay || 5000;
    }

    async connect() {
        return new Promise((resolve, reject) => {
            const host = this.config.host || 'localhost';
            const port = this.config.transport_port || 5555;

            console.log(`Connecting to TCP server at ${host}:${port}`);

            this.socket = new net.Socket();

            this.socket.connect(port, host, () => {
                console.log('TCP socket connection established');
                this._handleConnect();
                resolve();
            });

            this.socket.on('data', (data) => {
                // Use common framed data handler
                this._handleFramedData(data);
            });

            this.socket.on('error', (error) => {
                console.error('TCP socket error:', error);
                this._handleError(error);
                reject(error);
            });

            this.socket.on('close', () => {
                console.log('TCP socket connection closed');
                this._handleDisconnect();

                // Auto-reconnect if not intentionally disconnected
                if (this.isConnected) {
                    setTimeout(() => this.connect().catch(console.error), this.reconnectDelay);
                }
            });
        });
    }

    async disconnect() {
        this.isConnected = false;
        if (this.socket) {
            this.socket.destroy();
            this.socket = null;
        }
    }

    async send(message) {
        if (!this.socket || this.socket.destroyed) {
            console.log('TCP socket not connected, attempting to connect...');
            try {
                await this.connect();
            } catch (error) {
                console.error('Failed to connect before sending message:', error);
                return false;
            }
        }

        try {
            // Use common encoding method
            const packet = this._encodeMessage(message);
            this.socket.write(packet);
            return true;
        } catch (error) {
            console.error('Failed to encode or send message:', error);
            this._handleError(error);
            return false;
        }
    }
}

/**
 * Serial transport implementation
 * Communicates over serial port with COBS framing
 */
class SerialTransport extends BaseTransport {
    constructor(config) {
        super();
        this.config = config;
        this.port = null;
    }

    async connect() {
        return new Promise((resolve, reject) => {
            this.port = new SerialPort({
                path: this.config.port_path || '/dev/ttyUSB0',
                baudRate: this.config.baud_rate || 115200,
            });

            this.port.on('open', () => {
                console.log('Serial port opened');
                this._handleConnect();
                resolve();
            });

            this.port.on('data', (data) => {
                // Use common framed data handler
                this._handleFramedData(data);
            });

            this.port.on('error', (error) => {
                console.error('Serial port error:', error);
                this._handleError(error);
                reject(error);
            });

            this.port.on('close', () => {
                console.log('Serial port closed');
                this._handleDisconnect();
            });
        });
    }

    async disconnect() {
        this.isConnected = false;
        if (this.port) {
            this.port.close();
            this.port = null;
        }
    }

    async send(message) {
        if (this.port && this.port.isOpen) {
            try {
                // Use common encoding method
                const packet = this._encodeMessage(message);
                this.port.write(packet);
                return true;
            } catch (error) {
                console.error('Failed to encode or send message:', error);
                this._handleError(error);
                return false;
            }
        } else {
            console.error('Serial port not connected, cannot send message');
            return false;
        }
    }
}

/**
 * TransportFactory creates transport instances based on type
 */
class TransportFactory {
    static create(type, config) {
        switch (type.toLowerCase()) {
            case 'websocket':
            case 'ws':
                return new WebSocketTransport(config);
            case 'socket':
            case 'tcp':
                return new SocketTransport(config);
            case 'serial':
                return new SerialTransport(config);
            default:
                throw new Error(`Unknown transport type: ${type}`);
        }
    }

    static getSupportedTypes() {
        return ['websocket', 'ws', 'socket', 'tcp', 'serial'];
    }
}

export {
    BaseTransport,
    WebSocketTransport,
    SocketTransport,
    SerialTransport,
    TransportFactory
};

export default TransportFactory;