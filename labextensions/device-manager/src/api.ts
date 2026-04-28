// Thin wrapper around the /ucore/* REST surface exposed by the
// jupyter-server extension in `ukernel.server_extension`.
//
// Uses ServerConnection so XSRF, base_url, auth tokens and proxied
// JupyterHub paths all just work — never construct a raw fetch URL
// against window.location here.

import { ServerConnection } from '@jupyterlab/services';
import { URLExt } from '@jupyterlab/coreutils';

export interface Device {
  id: string;
  path: string;
  label: string;
  vid: string | null;
  pid: string | null;
  kind: string;
  speaks_jmp: boolean | null;
  info: Record<string, unknown> | null;
}

export interface ActiveState {
  selected_device: string | null;
  kernel_attached: boolean;
  kernel_pid: number | null;
}

const NAMESPACE = 'ucore';

async function request<T>(endpoint: string, init: RequestInit = {}): Promise<T> {
  const settings = ServerConnection.makeSettings();
  const url = URLExt.join(settings.baseUrl, NAMESPACE, endpoint);
  const response = await ServerConnection.makeRequest(url, init, settings);
  if (!response.ok) {
    const text = await response.text().catch(() => response.statusText);
    throw new ServerConnection.ResponseError(response, text || response.statusText);
  }
  if (response.status === 204) {
    return undefined as unknown as T;
  }
  return (await response.json()) as T;
}

export function listDevices(): Promise<{ devices: Device[] }> {
  return request('devices');
}

export function probeDevices(): Promise<{ devices: Device[] }> {
  return request('devices/probe', { method: 'POST' });
}

export function resetDevice(id: string): Promise<void> {
  return request(`devices/${encodeURIComponent(id)}/reset`, { method: 'POST' });
}

export function selectDevice(id: string | null): Promise<{ selected_device: string | null }> {
  return request('devices/select', {
    method: 'POST',
    body: JSON.stringify({ id: id ?? '' }),
  });
}

export function getActive(): Promise<ActiveState> {
  return request('active');
}
