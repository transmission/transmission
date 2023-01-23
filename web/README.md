# Notes

- Node.js and npm are only needed for building.
  The result is a static file, `transmission-app.js`, which can be
  served from an Transmission session, e.g. transmission-daemon.

- package.json's `devDependencies` are *intentionally* old to make
  building on older stock systems possible, e.g. Ubuntu 20.04 which
  ships with Node.js 10. Please don't bump deps without asking first.
