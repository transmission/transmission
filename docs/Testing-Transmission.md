## How to find a CI build in a pull request ##

At the bottom of a pull request, you'll find a list of checks (or expand them with "View details" if the PR was already merged).
Follow any link to "Details", but cut the jobs part of the URL, so that it looks like: <https://github.com/transmission/transmission/actions/runs/1234567890>.
Download the desired "from-tarball" Artifact at the bottom of the page.

## How to open a CI build on macOS ##

You need to mark Transmission.app both as executable and not quarantined:

1. `chmod +x Transmission.app/Contents/MacOS/Transmission`
2. `xattr -rc Transmission.app`

## On Apple Silicon, you also need a working install of Rosetta ##

Because CI builds are Intel only, if you're on Apple Silicon and previous steps are not enough to get it to work, then you'll also need to repair your installation of Rosetta:

1. Reboot into recovery (Turn off your Mac, then turn on your Mac with a long press on the power button, then choose "Options")
2. In Terminal, run `csrutil disable` and confirm
3. In Terminal, run `reboot`
4. In Terminal, obtain a list of Rosetta files and LaunchAgents with: `pkgutil --files com.apple.pkg.RosettaUpdateAuto`
5. In Terminal, delete the Rosetta files from previous step:
```
sudo rm -rf /Library/Apple/usr/lib/libRosettaAot.dylib
sudo rm -rf /Library/Apple/usr/libexec/oah
sudo rm -rf /Library/Apple/usr/share/rosetta
```
6. Reboot into recovery
7. In Terminal, run `csrutil enable` and confirm
8. In Terminal, run `reboot`
9. In Terminal, reinstall Rosetta with: `softwareupdate --install-rosetta`

And finally the CI builds of Transmission.app will be openable.
