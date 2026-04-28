// Sidebar icon: an IC / DIP chip glyph. Inlined as an SVG string so the
// extension has no asset dependency at runtime, and rendered through
// LabIcon so it inherits the JupyterLab theme's foreground colour.

import { LabIcon } from '@jupyterlab/ui-components';

const chipSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round">
  <rect x="4" y="4" width="8" height="8" rx="0.6"/>
  <circle cx="5.6" cy="5.6" r="0.55" fill="currentColor" stroke="none"/>
  <path d="M2 6h2 M2 8h2 M2 10h2 M12 6h2 M12 8h2 M12 10h2 M6 2v2 M8 2v2 M10 2v2 M6 12v2 M8 12v2 M10 12v2"/>
</svg>`;

export const chipIcon = new LabIcon({
  name: 'ucore:chip',
  svgstr: chipSvg,
});
