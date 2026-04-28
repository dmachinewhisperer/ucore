// The sidebar widget. Pure DOM (no React) to keep dependencies
// minimal — this surface is small enough that JSX would be ceremony.
//
// Selection is decoupled from kernel restart on purpose: clicking a
// row writes the selection to the state file, but the running kernel
// only picks it up on the next launch. The widget renders a hint
// reminding the user to restart manually when the choice changes.

import { Widget } from '@lumino/widgets';
import { Notification } from '@jupyterlab/apputils';

import {
  ActiveState,
  Device,
  getActive,
  listDevices,
  probeDevices,
  resetDevice,
  selectDevice,
} from './api';
import { chipIcon } from './icon';

const REFRESH_INTERVAL_MS = 5000;

export class DeviceManagerWidget extends Widget {
  private _devices: Device[] = [];
  private _active: ActiveState | null = null;
  private _refreshTimer: number | null = null;
  private _busy = false;

  constructor() {
    super();
    this.id = 'ucore-device-manager';
    this.title.icon = chipIcon;
    // Caption is the hover tooltip — kept for accessibility. Label is
    // intentionally empty so the sidebar tab shows only the icon.
    this.title.caption = 'µcore devices';
    this.title.label = '';
    this.addClass('ucore-DeviceManager');
    this._render();
  }

  protected onAfterAttach(): void {
    void this.refresh();
    this._refreshTimer = window.setInterval(() => {
      void this.refresh({ silent: true });
    }, REFRESH_INTERVAL_MS);
  }

  protected onBeforeDetach(): void {
    if (this._refreshTimer !== null) {
      window.clearInterval(this._refreshTimer);
      this._refreshTimer = null;
    }
  }

  async refresh(opts: { silent?: boolean } = {}): Promise<void> {
    try {
      const [{ devices }, active] = await Promise.all([listDevices(), getActive()]);
      this._devices = devices;
      this._active = active;
      this._render();
    } catch (err) {
      if (!opts.silent) {
        Notification.error(`Failed to load devices: ${describeError(err)}`);
      }
    }
  }

  private async _runAction(label: string, fn: () => Promise<unknown>): Promise<void> {
    if (this._busy) {
      return;
    }
    this._busy = true;
    this._render();
    try {
      await fn();
    } catch (err) {
      Notification.error(`${label} failed: ${describeError(err)}`);
    } finally {
      this._busy = false;
      await this.refresh({ silent: true });
    }
  }

  private async _onProbe(): Promise<void> {
    await this._runAction('Probe', async () => {
      const { devices } = await probeDevices();
      this._devices = devices;
    });
  }

  private async _onSelect(id: string): Promise<void> {
    await this._runAction('Select', async () => {
      await selectDevice(id);
      Notification.info(
        `Selected ${id}. Restart the kernel to attach to it.`,
        { autoClose: 4000 },
      );
    });
  }

  private async _onReset(id: string): Promise<void> {
    await this._runAction('Reset', async () => {
      await resetDevice(id);
      Notification.success(`Reset ${id}.`, { autoClose: 2500 });
    });
  }

  private _render(): void {
    const root = this.node;
    root.replaceChildren();
    root.appendChild(this._renderHeader());
    if (this._devices.length === 0) {
      root.appendChild(el('p', { class: 'ucore-empty' }, [
        'No serial devices detected. Plug one in and click Probe.',
      ]));
      return;
    }
    const list = el('ul', { class: 'ucore-DeviceList' }, []);
    const selectedId = this._active?.selected_device ?? null;
    for (const d of this._devices) {
      list.appendChild(this._renderRow(d, selectedId));
    }
    root.appendChild(list);
    root.appendChild(this._renderFooter());
  }

  private _renderHeader(): HTMLElement {
    const probeBtn = el('button', {
      class: 'jp-Button',
      type: 'button',
      ...(this._busy ? { disabled: '' } : {}),
    }, ['Probe']);
    probeBtn.addEventListener('click', () => void this._onProbe());

    const refreshBtn = el('button', {
      class: 'jp-Button',
      type: 'button',
      ...(this._busy ? { disabled: '' } : {}),
    }, ['Refresh']);
    refreshBtn.addEventListener('click', () => void this.refresh());

    return el('header', { class: 'ucore-Header' }, [
      el('span', { class: 'ucore-Title' }, ['Devices']),
      el('div', { class: 'ucore-Actions' }, [refreshBtn, probeBtn]),
    ]);
  }

  private _renderRow(d: Device, selectedId: string | null): HTMLElement {
    const isSelected = d.id === selectedId;
    const cls = ['ucore-Device'];
    if (isSelected) {
      cls.push('ucore-Device--selected');
    }
    if (d.kind === 'unknown') {
      cls.push('ucore-Device--unknown');
    }

    const status = (() => {
      if (d.speaks_jmp === true) return 'JMP';
      if (d.speaks_jmp === false) return 'no reply';
      return 'unprobed';
    })();

    const selectBtn = el('button', {
      class: 'jp-Button',
      type: 'button',
      title: isSelected ? 'Selected' : 'Use on next kernel start',
      ...(this._busy || isSelected ? { disabled: '' } : {}),
    }, [isSelected ? 'Selected' : 'Select']);
    selectBtn.addEventListener('click', () => void this._onSelect(d.id));

    const resetBtn = el('button', {
      class: 'jp-Button',
      type: 'button',
      title: 'Hardware reset (DTR/RTS)',
      ...(this._busy ? { disabled: '' } : {}),
    }, ['Reset']);
    resetBtn.addEventListener('click', () => void this._onReset(d.id));

    return el('li', { class: cls.join(' ') }, [
      el('div', { class: 'ucore-Device-main' }, [
        el('span', { class: 'ucore-Device-id' }, [d.id]),
        el('span', { class: 'ucore-Device-kind' }, [d.kind]),
      ]),
      el('div', { class: 'ucore-Device-meta' }, [
        el('span', { class: 'ucore-Device-label' }, [d.label]),
        el('span', { class: 'ucore-Device-status' }, [status]),
      ]),
      el('div', { class: 'ucore-Device-actions' }, [selectBtn, resetBtn]),
    ]);
  }

  private _renderFooter(): HTMLElement {
    const attached = this._active?.kernel_attached;
    const selected = this._active?.selected_device ?? '—';
    const pid = this._active?.kernel_pid ?? '—';
    return el('footer', { class: 'ucore-Footer' }, [
      el('div', {}, [`selected: ${selected}`]),
      el('div', {}, [`kernel: ${attached ? `pid ${pid}` : 'not attached'}`]),
    ]);
  }
}

// ── tiny DOM helper ────────────────────────────────────────────────────

function el(
  tag: string,
  attrs: Record<string, string>,
  children: Array<HTMLElement | string>,
): HTMLElement {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'disabled' || k === 'hidden') {
      (node as unknown as Record<string, boolean>)[k] = true;
    } else {
      node.setAttribute(k, v);
    }
  }
  for (const c of children) {
    node.append(typeof c === 'string' ? document.createTextNode(c) : c);
  }
  return node;
}

function describeError(err: unknown): string {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}
