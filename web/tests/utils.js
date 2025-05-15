// @ts-check
/// <reference types="node" />
/// <reference lib="es2024" />

import * as path from 'node:path';
import child_process from 'node:child_process';

/**
 * Boots the repo's copy of transmission-daemon and waits for it to indicate
 * it's listening for web requests
 *
 * @param {{username: string; password: string} | null} credentials The
 * credentials the server will expect. If null, the server will be
 * unauthenticated.
 *
 * @returns {Promise<() => Promise<number>>} A function that shuts down the
 * server.
 */
export const launchTransmissionDaemon = async (credentials) => {
  const args = ['-f'];
  if (credentials === null) {
    args.push('-T');
  } else {
    args.push('-t', '-u', credentials.username, '-v', credentials.password);
  }

  const child = child_process.spawn(
    path.join(
      import.meta.dirname,
      '..',
      '..',
      'build',
      'daemon',
      'transmission-daemon',
    ),
    args,
    {
      env: {
        TRANSMISSION_WEB_HOME: path.join(
          import.meta.dirname,
          '..',
          'public_html',
        ),
      },
    },
  );

  const {
    promise: serverStartedPromise,
    resolve: serverStartedResolve,
    reject: serverStartedReject,
  } = Promise.withResolvers();

  child.stdout.on('data', (data) => console.log(data.toString()));
  child.stderr.on('data', (data) => {
    if (data.toString().includes('Listening for RPC and Web requests')) {
      serverStartedResolve(null);
    }
    console.error(data.toString());
  });
  setTimeout(
    () =>
      serverStartedReject(new Error('Timed out waiting for server to start')),
    5000,
  );

  await new Promise((resolve, reject) => {
    child.on('spawn', () => resolve(null));
    child.on('error', (err) => reject(err));
  });
  await serverStartedPromise;

  const closeP = new Promise((resolve) => {
    child.on('close', (code) => resolve(code));
  });
  return async () => {
    child.kill();
    return await closeP;
  };
};