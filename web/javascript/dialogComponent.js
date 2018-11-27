
function htmlToElements(html) {
    var template = document.createElement('template');
    template.innerHTML = html;
    return template.content;
}

class DialogComponent extends HTMLElement {
  constructor() {
    super()

    let style = document.createElement('style')
    style.textContent = `
    #draggable {
      position: absolute;
      width: 300px;
      border: 1px solid #aaa;
      background: white;
      border-radius: 4px;
      z-index: 100;
      padding: 2px;
    }
    ::slotted(.handle) {
      cursor: move;
    }
    ::slotted(.header) {
      border: 1px solid #aaa;
      background: #ccc url("images/ui-bg_highlight-soft_75_cccccc_1x100.png") 50% 50% repeat-x;
      color: #222;
      font-weight: bold;
      padding: 4.4px 11px 4.4px;
      border-radius: 4px;
      margin: 0;
    }`

    let markup = `
    <div id="draggable">
      <slot></slot>
    </div>`

    const shadowRoot = this.attachShadow({ mode: 'open' });
    shadowRoot.appendChild(style);
    shadowRoot.appendChild(htmlToElements(markup));
  }

  connectedCallback() {
    let handle = this.shadowRoot.querySelector('slot').assignedNodes()
      .filter(element => element.classList && element.classList.contains('handle'))[0]

    let draggable = this.shadowRoot.querySelector('#draggable')
    if (!handle) {
      handle = draggable
    }

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
      draggable.style.left = (objInitLeft + e.pageX-dragStartX) + "px";
      draggable.style.top = (objInitTop + e.pageY-dragStartY) + "px";
    });
    document.addEventListener("mouseup", function(e) {inDrag = false;});
  }
}

customElements.define('dialog-component', DialogComponent);
