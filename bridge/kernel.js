import { v4 as uuidv4 } from 'uuid';
import fs from 'fs';
import * as zmq from 'zeromq';

import WireProtocol from './jmp_wire.js';
import TransportFactory from './transport.js';

class JupyterKernel {
    constructor(config, transport) {
        this.device = "3fa85f64-5717-4562-b3fc-2c963f66afa6";
        this.username = 'kernel';
        this.jmpVersion = '5.3.0';
        this.authToken = uuidv4();
        this.config = config;
        this.transport = transport;
        this.sockets = {};
        this.wireProtocols = {};
        this.session = uuidv4();
        this.execution_count = 0;
        this.isShuttingDown = false;
        this.messageHandlers = [];

        // Track pending requests: msg_id -> { identity, channel }
        this.pendingRequests = new Map();

        // Message types that are implemented
        this.implementedMessages = new Set([
            'execute_request',
            'kernel_info_request',
            'shutdown_request',
            'interrupt_request',
            'complete_request',
            'inspect_request',
            'is_complete_request',
            'stream',
            'error',
            'status',
            'input_request',
            'input_reply'
        ]);

        // Channel mapping for message types
        this.messageTypeToChannel = {
            // Shell channel requests/replies
            'execute_request': 'shell',
            'execute_reply': 'shell',
            'kernel_info_request': 'shell',
            'kernel_info_reply': 'shell',
            'inspect_request': 'shell',
            'inspect_reply': 'shell',
            'complete_request': 'shell',
            'complete_reply': 'shell',
            'history_request': 'shell',
            'history_reply': 'shell',
            'is_complete_request': 'shell',
            'is_complete_reply': 'shell',
            'comm_info_request': 'shell',
            'comm_info_reply': 'shell',

            // Control channel
            'shutdown_request': 'control',
            'shutdown_reply': 'control',
            'interrupt_request': 'control',
            'interrupt_reply': 'control',
            'debug_request': 'control',
            'debug_reply': 'control',

            // IOPub channel
            'stream': 'iopub',
            'display_data': 'iopub',
            'update_display_data': 'iopub',
            'execute_input': 'iopub',
            'execute_result': 'iopub',
            'error': 'iopub',
            'status': 'iopub',
            'clear_output': 'iopub',
            'debug_event': 'iopub',

            //note that the frontend listens to comms on iopub, while kernel on shell
            'comm_open': 'iopub',
            'comm_msg': 'iopub',
            'comm_close': 'iopub',

            // Stdin channel
            'input_request': 'stdin',
            'input_reply': 'stdin'
        };

        //TODO: it is possible we switch completely to local only kernel so server control messages will not be needed then
        // Server control messages
        this.serverControlMessages = new Set([
            "auth_request",
            "auth_reply",
            "target_not_found",
            "client_limit_reached",
            "hard_error"
        ]);

        this.requestResponseChannels = new Set(['shell', 'control', 'stdin']);

        this._setupTransportHandlers();
    }

    _setupTransportHandlers() {
        this.transport.onConnect(() => {
            console.log('Transport connected');

            // auth is not need when the kernel is running locally
            //this.doAuth();
        });

        this.transport.onDisconnect(() => {
            console.log('Transport disconnected');
        });

        this.transport.onError((error) => {
            console.error('Transport error:', error);
        });

        this.transport.onMessage((message) => {
            if (message.header && this.serverControlMessages.has(message.header.msg_type)) {
                this.handleTransportControlMessages(message);
                return;
            }
            this.handleTransportMessage(message);
        });
    }

    async start() {
        console.log('Starting Jupyter Kernel...');

        await this.transport.connect();
        await this.setupSockets();
        this.startMessageHandling();


        //TODO: add support for comm messages in jmp_converter as this routine sends comm_* packets
        //await this.setupNotificationChannels();

        console.log('Kernel started successfully');
    }

    async setupSockets() {
        const key = this.config.key || '';
        const signatureScheme = key ? this.config.signature_scheme || 'hmac-sha256' : 'hmac-sha256';

        console.log(`key = ${key}`);

        // Create wire protocol instances for each channel
        this.wireProtocols.shell = new WireProtocol({ key, signatureScheme });
        this.wireProtocols.control = new WireProtocol({ key, signatureScheme });
        this.wireProtocols.iopub = new WireProtocol({ key, signatureScheme });
        this.wireProtocols.stdin = new WireProtocol({ key, signatureScheme });

        // Create ZMQ sockets using 6.x API
        this.sockets.shell = new zmq.Router();
        this.sockets.control = new zmq.Router();
        this.sockets.iopub = new zmq.Publisher();
        this.sockets.stdin = new zmq.Router();
        this.sockets.heartbeat = new zmq.Reply();

        const { ip, transport } = this.config;

        await this.bindSocket('shell', `${transport}://${ip}:${this.config.shell_port}`);
        await this.bindSocket('control', `${transport}://${ip}:${this.config.control_port}`);
        await this.bindSocket('iopub', `${transport}://${ip}:${this.config.iopub_port}`);
        await this.bindSocket('stdin', `${transport}://${ip}:${this.config.stdin_port}`);
        await this.bindSocket('heartbeat', `${transport}://${ip}:${this.config.hb_port}`);

        console.log('ZMQ sockets bound successfully');
    }

    async bindSocket(name, address) {
        try {
            await this.sockets[name].bind(address);
            console.log(`${name} socket bound to ${address}`);
        } catch (error) {
            console.error(`Failed to bind ${name} socket to ${address}:`, error);
            throw error;
        }
    }

    startMessageHandling() {
        this.messageHandlers.push(this.handleSocket('shell'));
        this.messageHandlers.push(this.handleSocket('control'));
        this.messageHandlers.push(this.handleSocket('stdin'));
        this.messageHandlers.push(this.handleHeartbeat());
    }

    //comm sockets support

    // comm state: comm_id -> { target_name, data, ... }
    comms = new Map();

    // supported comm targets: target_name -> handler function
    commTargets = new Map();

    registerCommTarget(target_name, handler) {
        this.commTargets.set(target_name, handler);
    }

    unregisterCommTarget(target_name) {
        this.commTargets.delete(target_name);
    }

    // handles comm messages from both device and frontend
    async handleCommMessage(msg, from = "device") {
        const msgType = msg.header?.msg_type;
        if (!msgType || !msg.content) return;

        const comm_id = msg.content.comm_id;
        const target_name = msg.content.target_name;

        if (msgType === "comm_open") {
            if (this.commTargets.has(target_name)) {
                this.comms.set(comm_id, {
                    target_name,
                    data: msg.content.data,
                    from
                });
                // Call the handler
                try {
                    await this.commTargets.get(target_name).call(this, msg, from);
                } catch (e) {
                    console.error(`Comm target handler error for ${target_name}:`, e);
                }
                return;
            } else {
                // unsupported target: reply with comm_close as per protocol
                await this.sendCommClose(comm_id, from === "device" ? "device" : "frontend", "Unknown comm target");
                return;
            }
        }

        // Handle comm_msg
        if (msgType === "comm_msg") {
            const comm = this.comms.get(comm_id);
            if (comm && this.commTargets.has(comm.target_name)) {
                try {
                    await this.commTargets.get(comm.target_name).call(this, msg, from);
                } catch (e) {
                    console.error(`Comm target handler error for ${comm.target_name}:`, e);
                }
                return;
            }
        }

        // comm_close: cleanup
        if (msgType === "comm_close") {
            this.comms.delete(comm_id);
        }

        // // Notifications: pass through to frontend if from device
        // if (target_name === "notification" && from === "device") {
        //     await this.routeResponse({
        //         ...msg,
        //         header: { ...msg.header, msg_type: msgType },
        //     });
        //     return;
        // }

        // // By default, forward all comm messages to frontend (iopub)
        // if (from === "device") {
        //     await this.routeResponse({
        //         ...msg,
        //         header: { ...msg.header, msg_type: msgType },
        //     });
        // }
    }

    async sendComm(msgType, commMsg, to = "device") {
        if (!msgType.startsWith("comm_")) {
            throw new Error("Not a comm message");
        }
        const msg = {
            header: {
                msg_id: uuidv4(),
                username: this.username,
                session: this.session,
                date: new Date().toISOString(),
                msg_type: msgType,
                version: this.jmpVersion
            },
            parent_header: {},
            metadata: {},
            content: commMsg,
            buffers: [],
        };
        if (to === "device") {
            await this.transport.send(msg);
        } else {
            await this.routeResponse(msg);
        }
    }

    async sendCommOpen(comm_id, target_name, data = {}, to = "device") {
        const commOpen = {
            comm_id,
            target_name,
            data
        };

        await this.sendComm("comm_open", commOpen, to);
    }

    async sendCommMessage(comm_id, data = {}, to = "device") {
        const commMsg = {
            comm_id,
            data
        };

        await this.sendComm("comm_msg", commMsg, to);
    }

    async sendCommClose(comm_id, to = "device", error_msg = "") {
        const closeMsg = {
            comm_id,
            data: error_msg ? { error: error_msg } : {}
        };

        await this.sendComm("comm_close", closeMsg, to);
    }


    // At startup, create notification comms on both device and frontend
    async setupNotificationChannels() {
        const deviceCommId = uuidv4();
        const frontendCommId = uuidv4();
        await this.sendCommOpen(deviceCommId, "notification", {}, "device");
        await this.sendCommOpen(frontendCommId, "notification", {}, "frontend");

        //store comms
        this.comms.set(deviceCommId, {
            target_name: "notification",
            from: "device",
            paired_comm: frontendCommId
        });
        this.comms.set(frontendCommId, {
            target_name: "notification",
            from: "frontend",
            paired_comm: deviceCommId
        });
    }


    async notificationCommHandler(msg, from) {
        // Pass notifications from device to frontend
        if (from === "device") {
            // Find the frontend comm_id for notifications
            const frontendComm = Array.from(this.comms.entries())
                .find(([id, comm]) => comm.target_name === "notification" && comm.from === "frontend");

            if (frontendComm) {
                await this.sendCommMessage(frontendComm[0], msg.content.data, "frontend");
            }
        }
    }

    registerDefaultCommTargets() {
        this.registerCommTarget("notification", this.notificationCommHandler);
    }

    //TODO: need to handle shutdown requests, this kernel should catch it, process it
    //and also propagate it to the device
    async handleSocket(channel) {
        const socket = this.sockets[channel];
        const wireProtocol = this.wireProtocols[channel];

        try {
            for await (const parts of socket) {
                try {
                    const msg = wireProtocol.receive(parts);
                    console.log(`Received ${msg.header.msg_type} on ${channel}`);

                    if (msg.header.msg_type.endsWith("_request")) {
                        this.pendingRequests.set(msg.header.msg_id, {
                            identity: msg.idents,
                            channel: channel
                        });
                    }

                    // Intercept comm messages from frontend
                    if (msg.header.msg_type.startsWith("comm_")) {
                        await this.handleCommMessage(msg, "frontend");
                        continue;
                    }

                    if (!this.implementedMessages.has(msg.header.msg_type)) {
                        await this.sendStatus('busy');
                        await this.sendError(msg, {
                            status: 'error',
                            ename: 'NotImplementedError',
                            evalue: `msg_type='${msg.header.msg_type}' is not implemented`,
                            traceback: ''
                        });
                        await this.sendStatus('idle');
                        continue;
                    }

                    this.forwardToTransport(msg);

                } catch (error) {
                    console.error(`Error handling ${channel} message:`, error);
                }
            }
        } catch (error) {
            if (!this.isShuttingDown) {
                console.error(`Error in ${channel} message loop:`, error);
            }
        }
    }

    async handleHeartbeat() {
        const socket = this.sockets.heartbeat;

        try {
            for await (const [msg] of socket) {
                await socket.send([msg]);
            }
        } catch (error) {
            if (!this.isShuttingDown) {
                console.error('Error in heartbeat handler:', error);
            }
        }
    }

    async forwardToTransport(msg) {
        const sent = await this.transport.send(msg);

        if (!sent) {
            await this.sendStatus('busy');
            await this.sendError(msg, {
                status: 'error',
                ename: 'TransportUnreachable',
                evalue: 'Backend transport is not available',
                traceback: ''
            });
            await this.sendStatus('idle');
            console.error('Transport not connected, cannot forward message');
        }
    }

    handleTransportMessage(msg) {
        try {
            const msgType = msg.header?.msg_type;
            if (!msgType) {
                console.error('Malformed message: missing msg_type:', msg);
                return;
            }
            if (msgType.startsWith("comm_")) {
                this.handleCommMessage(msg, "device");
                return;
            }
            this.routeResponse(msg);
        } catch (err) {
            console.error('Error handling transport message:', err);
        }
    }

    handleTransportControlMessages(msg) {
        if (msg.header.msg_type == 'auth_reply') {
            console.log(`status=${msg.content.status}`);
        }
        else if (msg.header.msg_type === 'target_not_found') {
            this.sendStatus('busy');

            this.sendError(msg, {
                status: 'error',
                ename: 'TargetDeviceNotFound',
                evalue: `Device '${this.device}' was not found. Is your device turned on and connected?`,
                traceback: [
                    'Error: TargetDeviceNotFound',
                    `    at DeviceManager.check (${this.device})`,
                    '    at processRequest (processes.js:123:45)',
                    '    at handleMessage (server.js:67:12)'
                ]
            });

            this.sendStatus('idle');
        }
        else if (msg.header.msg_type == 'client_limit_reached') {
            this.sendStatus('busy');
            this.sendError(msg, {
                status: 'error',
                ename: 'ClientLimitReached',
                evalue: 'The maximum number of allowed clients has been reached.',
                traceback: [
                    'Error: ClientLimitReached',
                    '    at ClientManager.connect (client_manager.js:88:15)',
                    '    at handleConnection (server.js:42:10)'
                ]
            });

            this.sendStatus('idle');
        }
    }

    /*     async routeResponse(response) {
            const which_channel = this.messageTypeToChannel[response.header?.msg_type];
            if (!which_channel) {
                console.error(`Unknown msg_type: ${response.header?.msg_type}`);
                return;
            }
            
            if(which_channel == 'iopub'){
                await this.wireProtocols[which_channel].send(
                    this.sockets[which_channel],
                    [],
                    response.header,
                    response.parent_header || {},
                    response.metadata || {},
                    response.content,
                    response.buffers || []
                );
                console.log(`Sent ${response.header.msg_type} to iopub`);
                return;
            }
    
            const parentMsgId = response.parent_header?.msg_id;
            if (parentMsgId && this.pendingRequests.has(parentMsgId)) {
                const { identity, channel } = this.pendingRequests.get(parentMsgId);
        
                await this.wireProtocols[channel].send(
                    this.sockets[channel],
                    identity,
                    response.header,
                    response.parent_header || {},
                    response.metadata || {},
                    response.content,
                    response.buffers || []
                );
    
                if(response.header.msg_type.endsWith("_reply")){
                    this.pendingRequests.delete(parentMsgId);
                }
                
                console.log(`Sent ${response.header.msg_type} to ${channel} (matched via pendingRequests)`);
                return;
            }
        
            console.error(`No route for response msg_type=${response.header?.msg_type} msg_id=${parentMsgId}`);
        } */
    async routeResponse(msg) {
        const msgType = msg.header?.msg_type;
        if (!msgType) {
            console.error("routeResponse: missing msg_type", msg);
            return;
        }

        // 1. IOPub is always broadcast
        const declaredChannel = this.messageTypeToChannel[msgType];
        if (declaredChannel === "iopub") {
            await this.wireProtocols.iopub.send(
                this.sockets.iopub,
                [],
                msg.header,
                msg.parent_header || {},
                msg.metadata || {},
                msg.content,
                msg.buffers || []
            );
            console.log(`Sent ${msgType} to iopub`);
            return;
        }

        // 2. Replies: route via pendingRequests
        if (msgType.endsWith("_reply")) {
            const parentMsgId = msg.parent_header?.msg_id;
            if (!parentMsgId || !this.pendingRequests.has(parentMsgId)) {
                console.error(
                    `No pending request for reply ${msgType}, parent=${parentMsgId}`
                );
                return;
            }

            const { identity, channel } =
                this.pendingRequests.get(parentMsgId);

            await this.wireProtocols[channel].send(
                this.sockets[channel],
                identity,
                msg.header,
                msg.parent_header || {},
                msg.metadata || {},
                msg.content,
                msg.buffers || []
            );

            this.pendingRequests.delete(parentMsgId);

            console.log(`Sent ${msgType} to ${channel} (reply)`);
            return;
        }

        // 3. Requests (including input_request)
        if (msgType.endsWith("_request")) {
            if (!declaredChannel) {
                console.error(`No channel mapping for request ${msgType}`);
                return;
            }

            // stdin requests (input_request) land here correctly
            const socket = this.sockets[declaredChannel];
            const wire = this.wireProtocols[declaredChannel];

            // stdin & shell require ROUTER identity
            // try to bind to the active execute_request if possible
            let identity = [];

            if (declaredChannel !== "iopub") {
                const parentMsgId = msg.parent_header?.msg_id;
                if (parentMsgId && this.pendingRequests.has(parentMsgId)) {
                    identity = this.pendingRequests.get(parentMsgId).identity;
                }
            }

            await wire.send(
                socket,
                identity,
                msg.header,
                msg.parent_header || {},
                msg.metadata || {},
                msg.content,
                msg.buffers || []
            );

            console.log(`Sent ${msgType} to ${declaredChannel} (request)`);
            return;
        }

        // 4. Fallback (should be rare)
        if (declaredChannel) {
            await this.wireProtocols[declaredChannel].send(
                this.sockets[declaredChannel],
                [],
                msg.header,
                msg.parent_header || {},
                msg.metadata || {},
                msg.content,
                msg.buffers || []
            );
            console.log(`Sent ${msgType} to ${declaredChannel} (fallback)`);
            return;
        }

        console.error(`No routing rule for msg_type=${msgType}`);
    }

    doAuth() {
        const authMsg = {
            header: {
                msg_id: uuidv4(),
                msg_type: "auth_request",
                session: "",
                username: "",
                date: new Date().toISOString(),
                version: this.jmpVersion
            },
            parent_header: {},
            metadata: {},
            content: {
                device_id: this.device,
                timestamp: new Date().toISOString(),
                hmac_sha256: this.authToken
            },
            buffers: []
        };

        this.transport.send(authMsg);
    }

    async sendError(originalMsg, content) {
        console.log(originalMsg);

        const header = (originalMsg.parent_header && Object.keys(originalMsg.parent_header).length > 0)
            ? originalMsg.parent_header
            : originalMsg.header;

        const replyType = header.msg_type.endsWith('_request')
            ? header.msg_type.replace('_request', '_reply')
            : 'error';

        const errorReply = {
            header: {
                msg_id: uuidv4(),
                username: header.username || '',
                session: header.session,
                date: new Date().toISOString(),
                msg_type: replyType,
                version: this.jmpVersion
            },
            parent_header: header,
            metadata: {},
            content: content,
            buffers: []
        };

        console.log(errorReply);
        await this.routeResponse(errorReply);
    }

    async sendStatus(status, parentHeader = {}) {
        const statusMsg = {
            header: {
                msg_id: uuidv4(),
                username: parentHeader.username || 'kernel',
                session: this.session,
                date: new Date().toISOString(),
                msg_type: 'status',
                version: this.jmpVersion
            },
            parent_header: parentHeader,
            metadata: {},
            content: {
                execution_state: status
            },
            buffers: []
        };

        await this.wireProtocols.iopub.send(
            this.sockets.iopub,
            [],
            statusMsg.header,
            statusMsg.parent_header,
            statusMsg.metadata,
            statusMsg.content,
            statusMsg.buffers
        );
        console.log(`Sent status: ${status}`);
    }

    async shutdown() {
        console.log('Shutting down kernel...');
        this.isShuttingDown = true;

        await this.transport.disconnect();

        for (const [name, socket] of Object.entries(this.sockets)) {
            try {
                socket.close();
                console.log(`Closed ${name} socket`);
            } catch (error) {
                console.error(`Error closing ${name} socket:`, error);
            }
        }

        this.pendingRequests.clear();
        console.log('Kernel shutdown complete');
    }
}

// Usage example and startup function
async function startKernel(configFile, transportType = 'socket') {
    try {
        const config = JSON.parse(fs.readFileSync(configFile, 'utf8'));

        const transport = TransportFactory.create(transportType, config);

        const kernel = new JupyterKernel(config, transport);
        await kernel.start();

        // Send initial status
        await kernel.sendStatus('starting');

        // Handle graceful shutdown
        process.on('SIGINT', async () => {
            console.log('Received SIGINT, shutting down...');
            await kernel.shutdown();
            process.exit(0);
        });

        process.on('SIGTERM', async () => {
            console.log('Received SIGTERM, shutting down...');
            await kernel.shutdown();
            process.exit(0);
        });

    } catch (error) {
        console.error('Failed to start kernel:', error);
        process.exit(1);
    }
}

// Export classes and functions
// module.exports = { 
//     JupyterKernel,  
//     startKernel 
// };
export { JupyterKernel, startKernel };

import { fileURLToPath } from 'url';
const __filename = fileURLToPath(import.meta.url);

if (process.argv[1] === __filename) {
    const configFile = process.argv[2];
    const transportType = process.argv[3] || 'socket';

    if (!configFile) {
        console.error('Usage: node kernel.js <config_file> [transport_type]');
        console.error('Transport types: websocket, socket, serial');
        process.exit(1);
    }

    startKernel(configFile, transportType);
}