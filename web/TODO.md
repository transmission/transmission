# TODO

- [ ] one unified stylesheet

- [ ] decide on a single mobile-and-desktop friendly apparance of Inspector

- [ ] remove jQuery
  - [ ] remove use of jquery-ui-tabs from prefs dialog
  - [ ] remove context menu
  - [ ] replace popup menu with more menu
    - [ ] add 'more menu' to toolbar
      - [ ] add 'pause all' to more menu
      - [ ] add 'start all' to more menu
      - [ ] remove 'pause all' from toolbar
      - [ ] remove 'start all' from toolbar
      - [ ] add About to more menu
      - [ ] remove About from popup menu
      - [ ] after completing the proof-of-concept above, add more TODO items here

## Short-term TODO
- [x] compact mode is broken
- [x] overlaid progressbars are broken
- [x] dialogs are broken in smol mode
- [x] buttons in 'remove' dialog are wrong
- [x] entry field in 'rename' dialog isn't wide enough
- [x] entry field in 'set location' dialog isn't wide enough
- [x] buttons in 'set location' dialog are wrong
- [x] dialogs that appear to be using jquery-ui but could use the dialog refactor:
  - [x] 'about'
  - [x] 'shortcuts'
  - [x] 'stats'
- [x] 'paused' class is not set on the torrent-name items when the torrent is paused
- [x] torrent-row pause/resume buttons don't highlight
- [x] replace png files with svgs where possible
- [x] extract the configs out of package.json -- preferably into .js files where comments can be used
- [x] ratio should have one decimal place, not two
- [x] can't open the 'about' dialog after opening and closing the 'remove' dialog
- [x] 'open' shortcut 'o' only works once
- [x] compact mode overflows on iPhone
- [x] more menu
  - [x] more menu should 'slide' out from left
  - [x] figure out what to do on submenus
  - [x] styling
  - [x] scrolling on mobile?
  - [x] wire in start all
  - [x] wire in stop all
  - [x] wire in homepage
  - [x] wire in tip jar
  - [x] should OVERLAY torrent container, not go before it
- [ ] stats dialog looks bad on mobile
- [ ] inspector styling is broken
- [ ] filterbar isn't responsive
- [ ] what to do about context menu? it still uses jQuery
- [ ] what to do about prefs dialog? it still uses jQuery
- [ ] what to do about footer? it's not useful right now
