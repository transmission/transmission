# Notes

- Node.js and npm are only needed for building.
  The result is a static file, `transmission-app.js`.

- The packages in `devDependencies` are intentionally old so that
  building is possible on older systems that ship with Node.js 10,
  such as Ubuntu 20.04. Please don't bump the Node.js requirement
  without asking the maintainers first.
