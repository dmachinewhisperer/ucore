# Flash firmware

Plug in a supported board over USB, then flash µcore directly from this
page. No `esptool`, no IDF, no Python required for this step.

<div style="margin: 32px 0;">
  <esp-web-install-button manifest="manifest.json">
    <button slot="activate" class="md-button md-button--primary">
      Connect &amp; flash
    </button>
    <span slot="unsupported">
      <button class="md-button" disabled>Browser not supported</button>
    </span>
    <span slot="not-allowed">
      <button class="md-button" disabled>Open this page over HTTPS</button>
    </span>
  </esp-web-install-button>
</div>

<script type="module"
        src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>

!!! info "Browser support"
    Web Serial requires a Chromium browser (Chrome, Edge, Brave, Opera).
    Firefox and Safari users can fall back to:

    ```bash
    esptool.py write_flash 0 ucore-<board>.bin
    ```

    Download the binary for your board from the
    [latest release](https://github.com/dmachinewhisperer/ucore/releases/latest).

## Supported boards

| Board | Chip | Binary |
|---|---|---|
| ESP32 (generic) | ESP32 | `ucore-esp32-generic.bin` |
| ESP32-S3 | ESP32-S3 | `ucore-esp32-s3.bin` |

## What's on the device after flashing

A frozen MicroPython firmware that exposes the `ucore` module: named
pipes back to the host, JMP transport over USB CDC, and the
[full MicroPython standard library](https://docs.micropython.org/).
After flashing, head to [Getting started](getting-started.md) to
install the Jupyter kernel.
