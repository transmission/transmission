import assert from 'node:assert/strict';
import test from 'node:test';
import { groupReadings, ServerFreeSpace } from './src/server-free-space.js';

function reading(path, bytes, filesystemId = null) {
  return { bytes, filesystemId, label: path, path };
}

test('merge only identical paths or known equal filesystems; use the minimum', () => {
  for (const ids of [
    [null, null],
    ['', ''],
    ['one', 'two'],
  ]) {
    assert.equal(
      groupReadings([reading('/a', 120, ids[0]), reading('/b', 120, ids[1])])
        .length,
      2,
    );
  }
  const shared = groupReadings([
    reading('/a', 120, 'one'),
    reading('/b', 35, 'one'),
  ]);
  assert.equal(shared.length, 1);
  assert.equal(shared[0].bytes, 35);
  assert.equal(shared[0].readings.length, 2);
  assert.equal(
    groupReadings([reading('/a', 120), reading('/a', 0)])[0].bytes,
    0,
  );
});

function harness() {
  const paths = {
    replaceChildren() {
      /* DOM stub */
    },
  };
  const summary = {
    replaceChildren() {
      /* DOM stub */
    },
  };
  const root = {
    hidden: true,
    open: false,
    querySelector: (selector) => (selector === 'summary' ? summary : paths),
  };
  const requests = [];
  const remote = {
    getFreeSpace: (path, callback) => requests.push({ callback, path }),
  };
  const view = new ServerFreeSpace(remote, root);
  const renders = [];
  view.render = (readings) => {
    renders.push(groupReadings(readings));
    root.hidden = false;
  };
  return { renders, requests, root, view };
}

const settings = {
  download_dir: '/downloads',
  incomplete_dir: '/incomplete',
  incomplete_dir_enabled: true,
};

function respond(request, bytes, id = null) {
  request.callback(request.path, bytes, id);
}

test('disabled incomplete directory and identical paths need only one request', () => {
  const single = harness();
  single.view.refresh({ ...settings, incomplete_dir_enabled: false });
  assert.equal(single.requests.length, 1);
  respond(single.requests[0], 0);
  assert.equal(single.renders.at(-1)[0].bytes, 0);
  const same = harness();
  same.view.refresh({ ...settings, incomplete_dir: settings.download_dir });
  assert.equal(same.requests.length, 1);
  respond(same.requests[0], 120);
  assert.equal(same.renders.at(-1)[0].readings.length, 2);
});

test('unknown identity, partial failure, and invalid readings remain separate', () => {
  for (const invalid of [
    -1,
    Number.NaN,
    Number.POSITIVE_INFINITY,
    '120',
    null,
  ]) {
    const { renders, requests, view } = harness();
    view.refresh(settings);
    respond(requests[0], invalid);
    respond(requests[1], 0);
    const groups = renders.at(-1);
    assert.equal(groups.length, 2);
    assert.equal(groups[0].bytes, null);
    assert.equal(groups[1].bytes, 0);
  }
});

test('known shared identity merges both roles and paths with the lower reading', () => {
  const { renders, requests, view } = harness();
  view.refresh(settings);
  respond(requests[0], 120, 'volume');
  respond(requests[1], 35, 'volume');
  const [group] = renders.at(-1);
  assert.equal(group.bytes, 35);
  assert.deepEqual(
    group.readings.map(({ path }) => path),
    ['/incomplete', '/downloads'],
  );
});

test('refreshes do not overlap and superseded settings cannot render old results', () => {
  const { renders, requests, view } = harness();
  view.refresh(settings);
  view.refresh(settings);
  assert.equal(requests.length, 2);
  view.refresh({ ...settings, download_dir: '/new' });
  const count = renders.length;
  respond(requests[0], 120, 'old');
  respond(requests[1], 120, 'old');
  assert.equal(renders.length, count);
  assert.equal(requests.length, 4);
  assert.equal(requests[3].path, '/new');
  respond(requests[2], 35, 'one');
  respond(requests[3], 120, 'two');
  assert.equal(renders.at(-1).length, 2);
  view.refresh({ ...settings, download_dir: '/new' });
  assert.equal(requests.length, 6);
});

test('disconnect clears readings, rejects in-flight responses, and recovers', () => {
  const { renders, requests, root, view } = harness();
  view.refresh(settings);
  view.clear();
  assert.equal(root.hidden, true);
  const count = renders.length;
  respond(requests[0], 120, 'volume');
  respond(requests[1], 120, 'volume');
  assert.equal(renders.length, count);
  assert.equal(root.hidden, true);
  view.refresh(settings);
  respond(requests[2], 0, 'volume');
  respond(requests[3], 35, 'volume');
  assert.equal(root.hidden, false);
  assert.equal(renders.at(-1)[0].bytes, 0);
});
