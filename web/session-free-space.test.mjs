import assert from 'node:assert/strict';
import test from 'node:test';
import { build } from 'esbuild';

// Session polling does not use the torrent-list renderer or a browser DOM.
const bundle = await build({
  bundle: true,
  entryPoints: ['./src/transmission.js'],
  format: 'esm',
  plugins: [
    {
      name: 'stub-torrent-renderer',
      setup(builder) {
        builder.onResolve({ filter: /^clusterize\.js$/ }, () => ({
          namespace: 'stub',
          path: 'clusterize',
        }));
        builder.onLoad({ filter: /.*/, namespace: 'stub' }, () => ({
          contents: 'export default class {}',
        }));
      },
    },
  ],
  write: false,
});
globalThis.matchMedia = () => ({ matches: false });
const { Transmission } = await import(
  `data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString('base64')}`
).catch((error) => {
  throw new Error(error.message);
});

test('every successful session poll refreshes space, including unchanged settings', () => {
  const controller = Object.setPrototypeOf(
    new EventTarget(),
    Transmission.prototype,
  );
  const settings = {
    download_dir: '/downloads',
    incomplete_dir_enabled: false,
  };
  let sessionUpdates = 0;
  const refreshes = [];
  controller._openTorrentFromUrl = () => {
    /* unrelated UI action */
  };
  controller._updateGuiFromSession = () => sessionUpdates++;
  controller.remote = {
    loadDaemonPrefs: (callback) => callback({ result: { ...settings } }),
  };
  controller.serverFreeSpace = { refresh: (value) => refreshes.push(value) };
  controller.loadDaemonPrefs();
  controller.loadDaemonPrefs();
  assert.equal(sessionUpdates, 1);
  assert.equal(refreshes.length, 2);
  settings.download_dir = '/new';
  controller.loadDaemonPrefs();
  assert.equal(sessionUpdates, 2);
  assert.equal(refreshes.at(-1).download_dir, '/new');
  controller.remote.loadDaemonPrefs = (callback) =>
    callback({ error: { code: 1 } });
  controller.loadDaemonPrefs();
  assert.equal(refreshes.length, 3);
});
