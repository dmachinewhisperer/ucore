import crypto from 'crypto';

class WireProtocol {
    constructor({ key = '', signatureScheme = 'hmac-sha256' } = {}) {
        this.key = key;
        this.digestMethod = signatureScheme.split('-')[1] || 'sha256';
        this.hmacKey = Buffer.from(this.key, 'utf8');
    }

    sign(header, parent, metadata, content) {
        if (!this.key) return '';

        const hmac = crypto.createHmac(this.digestMethod, this.hmacKey);
        for (const item of [header, parent, metadata, content]) {
            hmac.update(item);
        }
        return hmac.digest('hex');
    }

    verify(signature, header, parent, metadata, content) {
        if (!this.key) return true;
        const expected = this.sign(header, parent, metadata, content);
        return expected === signature;
    }

    async send(socket, idents, headerObj, parentObj, metadataObj, contentObj, buffers = []) {
        const header = JSON.stringify(headerObj);
        const parent = JSON.stringify(parentObj);
        const metadata = JSON.stringify(metadataObj);
        const content = JSON.stringify(contentObj);
        const signature = this.sign(header, parent, metadata, content);

        const messageParts = [
            ...idents,
            Buffer.from("<IDS|MSG>"),
            Buffer.from(signature),
            Buffer.from(header),
            Buffer.from(parent),
            Buffer.from(metadata),
            Buffer.from(content),
            ...buffers
        ];

        await socket.send(messageParts);
    }

    receive(parts) {
        const idents = [];
        let i = 0;
        while (i < parts.length && parts[i].toString() !== "<IDS|MSG>") {
            idents.push(parts[i]);
            i++;
        }

        if (i >= parts.length || parts[i].toString() !== "<IDS|MSG>") {
            throw new Error("Invalid message: missing <IDS|MSG>");
        }

        i++; // Skip the delimiter
        const signature = parts[i++].toString();
        const headerStr = parts[i++].toString();
        const parentStr = parts[i++].toString();
        const metadataStr = parts[i++].toString();
        const contentStr = parts[i++].toString();
        const buffers = parts.slice(i);

        const verified = this.verify(signature, headerStr, parentStr, metadataStr, contentStr);
        if (!verified) {
            throw new Error("Invalid message signature");
        }

        return {
            idents,
            signature,
            header: JSON.parse(headerStr),
            parent_header: JSON.parse(parentStr),
            metadata: JSON.parse(metadataStr),
            content: JSON.parse(contentStr),
            buffers
        };
    }
}

// module.exports = WireProtocol;
export { WireProtocol };
export default WireProtocol;