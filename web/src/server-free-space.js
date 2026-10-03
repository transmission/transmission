/* @license This file Copyright (C) Mnemosyne LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only).
   License text can be found in the licenses/ folder. */

import { Formatter } from './formatter.js';

export function groupReadings(readings) {
  const groups = [];
  for (const reading of readings) {
    const group = groups.find((candidate) =>
      candidate.readings.some(
        (other) =>
          other.path === reading.path ||
          (reading.filesystemId && other.filesystemId === reading.filesystemId),
      ),
    );
    if (group) {
      group.readings.push(reading);
      group.bytes =
        group.bytes === null || reading.bytes === null
          ? null
          : Math.min(group.bytes, reading.bytes);
    } else {
      groups.push({ bytes: reading.bytes, readings: [reading] });
    }
  }
  return groups;
}

export class ServerFreeSpace {
  constructor(remote, root) {
    this.remote = remote;
    this.root = root;
    this.summary = root.querySelector('summary');
    this.paths = root.querySelector('.server-free-space-paths');
    this.roles = [];
    this.generation = 0;
    this.pending = false;
    this.queued = false;
    this.connected = false;
  }

  refresh(settings) {
    const roles = settings.incomplete_dir_enabled
      ? [
          { label: 'Incomplete', path: settings.incomplete_dir },
          { label: 'Downloads', path: settings.download_dir },
        ]
      : [{ label: 'Downloads', path: settings.download_dir }];
    if (JSON.stringify(roles) !== JSON.stringify(this.roles)) {
      this.roles = roles;
      this.generation++;
      this.render(roles.map((role) => ({ ...role, bytes: null })));
    }
    this.connected = true;
    if (this.pending) {
      this.queued = true;
      return;
    }
    this.startRefresh();
  }

  clear() {
    this.connected = false;
    this.generation++;
    this.queued = false;
    this.root.hidden = true;
    this.root.open = false;
    this.summary.replaceChildren();
    this.paths.replaceChildren();
  }

  startRefresh() {
    const { generation, roles } = this;
    const paths = [...new Set(roles.map((role) => role.path))];
    const results = new Map();
    this.pending = true;
    this.queued = false;
    for (const path of paths) {
      this.remote.getFreeSpace(path, (_path, bytes, filesystemId) => {
        results.set(path, {
          bytes: Number.isFinite(bytes) && bytes >= 0 ? bytes : null,
          filesystemId:
            typeof filesystemId === 'string' && filesystemId.length > 0
              ? filesystemId
              : null,
        });
        if (results.size === paths.length) {
          this.pending = false;
          if (this.connected && generation === this.generation) {
            this.render(
              roles.map((role) => ({ ...role, ...results.get(role.path) })),
            );
          }
          if (this.connected && this.queued) {
            this.startRefresh();
          }
        }
      });
    }
  }

  render(readings) {
    this.root.hidden = false;
    this.summary.replaceChildren();
    const groups = groupReadings(readings);
    for (const group of groups) {
      const span = document.createElement('span');
      const label =
        group.readings.length > 1
          ? 'Downloads & incomplete'
          : group.readings[0].label;
      const value =
        group.bytes === null
          ? 'Unavailable'
          : `${Formatter.size(group.bytes)} free`;
      span.textContent = `${label}: ${value}`;
      this.summary.append(span);
    }
    this.paths.replaceChildren();
    for (const { label, path } of readings) {
      const item = document.createElement('div');
      item.textContent = `${label}: ${path}`;
      this.paths.append(item);
    }
  }
}
