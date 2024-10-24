import { setTextContent } from './utils.js';

export class TorrentRowGroup {
  constructor(path) {
    this._element = TorrentRowGroup._create(path);
  }

  static _create(path) {
    const root = document.createElement('li');
    root.classList.add('folder');
    root.dataset.path = path;

    const folderData = document.createElement('div');
    folderData.classList.add('data');
    root.append(folderData);

    const icon = document.createElement('div');
    icon.classList.add('folder-icon');
    folderData.append(icon);

    const name = document.createElement('div');
    name.classList.add('folder-name');
    setTextContent(name, path);
    folderData.append(name);

    const ul = document.createElement('ul');
    root.append(ul);

    return root;
  }

  getElement() {
    return this._element;
  }
}
