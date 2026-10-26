import assert from 'node:assert/strict';
import test from 'node:test';
import { build } from 'esbuild';

const bundle = await build({
  bundle: true,
  entryPoints: ['./src/remote.js'],
  format: 'esm',
  write: false,
});
const { Remote } = await import(
  `data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString('base64')}`
);

test('free-space callback keeps its path/bytes contract and passes optional identity', () => {
  for (const filesystem_id of [null, 'volume']) {
    const remote = new Remote(null);
    remote.sendRequest = (request, callback) => {
      assert.equal(request.method, 'free_space');
      assert.equal(request.params.path, '/downloads');
      callback({
        result: { filesystem_id, path: '/downloads', size_bytes: 0 },
      });
    };
    const context = {};
    remote.getFreeSpace(
      '/downloads',
      function (path, bytes, id) {
        // eslint-disable-next-line no-invalid-this -- verifies the existing callback context contract
        assert.equal(this, context);
        assert.equal(path, '/downloads');
        assert.equal(bytes, 0);
        assert.equal(id, filesystem_id);
      },
      context,
    );
  }
});

test('RPC failures without result data complete the callback with unavailable space', () => {
  const remote = new Remote(null);
  remote.sendRequest = (_request, callback) => callback({ error: { code: 1 } });
  remote.getFreeSpace('/missing', (path, bytes) => {
    assert.equal(path, '/missing');
    assert.equal(bytes, -1);
  });
});

test('transport failure completes pending free-space refreshes', () => {
  const remote = new Remote(null);
  remote.sendRequest = (_request, _callback, _context, onError) => onError();
  remote.getFreeSpace('/downloads', (path, bytes) => {
    assert.equal(path, '/downloads');
    assert.equal(bytes, -1);
  });
});
