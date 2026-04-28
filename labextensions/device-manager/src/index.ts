// JupyterLab plugin entry. Mounts the device manager into the left
// sidebar and exposes a command (also bound to a top-bar button via
// the launcher / commandPalette).

import {
  ILayoutRestorer,
  JupyterFrontEnd,
  JupyterFrontEndPlugin,
} from '@jupyterlab/application';
import { ICommandPalette } from '@jupyterlab/apputils';

import { DeviceManagerWidget } from './widget';

import '../style/index.css';

namespace CommandIDs {
  export const open = 'ucore-device-manager:open';
}

const plugin: JupyterFrontEndPlugin<void> = {
  id: '@ucore/device-manager:plugin',
  description: 'Sidebar device manager for the ucore Jupyter kernel.',
  autoStart: true,
  requires: [ICommandPalette],
  optional: [ILayoutRestorer],
  activate: (
    app: JupyterFrontEnd,
    palette: ICommandPalette,
    restorer: ILayoutRestorer | null,
  ): void => {
    const widget = new DeviceManagerWidget();
    app.shell.add(widget, 'left', { rank: 600 });

    if (restorer) {
      restorer.add(widget, widget.id);
    }

    app.commands.addCommand(CommandIDs.open, {
      label: 'Show µcore Devices',
      execute: () => {
        app.shell.activateById(widget.id);
      },
    });
    palette.addItem({ command: CommandIDs.open, category: 'µcore' });
  },
};

export default plugin;
