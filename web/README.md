# Notes

- Node.js and npm are only needed for building.
  The result is a static file, `transmission-app.js`, which can be
  served from an Transmission session, e.g. transmission-daemon.

- The packages in package.json's `devDependencies` are *intentionally*
  to make building possible on stock older systems, e.g. Ubuntu 20.04.
  Please don't bump the Node.js requirement without asking first.
