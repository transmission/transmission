## How to find a CI build in a pull request ##

At the bottom of a pull request, you'll find a list of checks (or expand them with "View details" if the PR was already merged).
Follow any link to "Details", but cut the jobs part of the URL, so that it looks like: <https://github.com/transmission/transmission/actions/runs/1234567890>.
Download the desired "from-tarball" Artifact at the bottom of the page.

## How to open a CI build on macOS ##

You need to mark Transmission.app both as executable and not quarantined using the Terminal application (in Applications/Utilities):

1. `chmod +x Transmission.app/Contents/MacOS/Transmission`

   If you're unfamiliar with using the Terminal application, the following steps might be of help.

   1. Open the Terminal application and type: chmod +x (and add a space after the x).
   2. Right-click or Ctrl-click the Transmission application and select 'Show Package Contents'.
   3. Navigate to the 'MacOS' directory; drag and drop 'Transmission' on to the Terminal window you had opened in step 1 and press Return. 

2. `xattr -rc Transmission.app`

   One of the MacOS security features is to 'quarantine' applications, and the xattr application allows the user to change the attributes of an application by setting different parameters. If xattr isn't available on your machine, you will need to install Apple's Developer Tools. Paste the following into a Terminal window to begin installation: xcode-select --install

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
