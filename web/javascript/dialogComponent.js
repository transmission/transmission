import { htmlToElements } from './utils.js';

function keepInRange(min, value, max) {
    return value < min ? min : (value > max ? max : value);
}

class DialogComponent extends HTMLElement {
  constructor() {
    super()

    let style = document.createElement('style')
    style.textContent = `
    .visible {
      visibility: visible;
      opacity: 1;
    }
    .hidden {
      visibility: hidden;
      opacity: 0;
    }

    #draggable {
      position: absolute;
      width: 300px;
      border: 1px solid #aaa;
      background: white;
      border-radius: 4px;
      z-index: 100;
      padding: 2px;
      transition: visibility 0.3s ease, opacity 0.3s ease;
    }
    .header {
      cursor: move;
      -moz-user-select: none;
      -webkit-user-select: none;
      -ms-user-select: none;
      user-select: none;
      border: 1px solid #aaa;
      background: #ccc url("images/ui-bg_highlight-soft_75_cccccc_1x100.png") 50% 50% repeat-x;
      color: #222;
      font-weight: bold;
      padding: 4.4px 11px 4.4px;
      border-radius: 4px;
      margin: 0;
      display: flex;
      align-items: baseline;
    }
    .header > #title {
      flex: 2;
    }
    button.close {
      background-image: url(images/ui-icons_888888_256x240.png);
      color: #555;
      font-family: Verdana,Arial,sans-serif;
      cursor: pointer;
    }`

    let markup = `
    <div id="draggable">
      <h4 class='header handle'>
        <span id='title'>
          No Title
        </span>
        <button class='close'>
          &#x274c;
        </button>
      </h4>
      <slot></slot>
    </div>`

    const shadowRoot = this.attachShadow({ mode: 'open' });
    shadowRoot.appendChild(style);
    shadowRoot.appendChild(htmlToElements(markup));
  }

  setTitle() {
    let title = this.getAttribute('title')
    this.shadowRoot.querySelector('#title').textContent = title
  }

  toggle(e) {
    this.shadowRoot.querySelector('#draggable').classList.toggle('visible')
    this.shadowRoot.querySelector('#draggable').classList.toggle('hidden')
  }

  connectedCallback() {
    this.setTitle()
    this.shadowRoot.querySelector('button.close')
      .addEventListener('click', this.toggle.bind(this))

    let handle = this.shadowRoot.querySelector('.handle')
    let draggable = this.shadowRoot.querySelector('#draggable')

    // Modeled after the dragging functionality here: http://infoheap.com/javascript-make-element-draggable/
    var dragStartX, dragStartY; var objInitLeft, objInitTop;
    var inDrag = false;
    handle.addEventListener("mousedown", function(e) {
      inDrag = true;
      objInitLeft = draggable.offsetLeft;
      objInitTop = draggable.offsetTop;
      dragStartX = e.pageX;
      dragStartY = e.pageY;
    });

    document.addEventListener("mousemove", function(e) {
      if (!inDrag) {return;}
      let newLeft = objInitLeft + e.pageX-dragStartX
      let newTop = objInitTop + e.pageY-dragStartY
      draggable.style.left =  keepInRange(0, newLeft, window.innerWidth - draggable.clientWidth)+ "px";
      draggable.style.top = keepInRange(0, newTop, window.innerHeight - draggable.clientHeight) + "px";
    });
    document.addEventListener("mouseup", function(e) {inDrag = false;});
  }
}

customElements.define('dialog-component', DialogComponent);
export default DialogComponent;
