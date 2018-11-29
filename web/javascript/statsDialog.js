import DialogComponent from './dialogComponent.js'
import { htmlToElements } from './utils.js';

class StatsDialog extends HTMLElement {
  static get observedAttributes() { return ['stats']; }

  constructor() {
    super()

    let style = document.createElement('style')
    style.textContent = `
      section {
        padding-left: 20px;
        padding-right: 20px;
        padding-bottom: 6px;
      }
      section > h4 {
        font-size: larger;
        text-align: left;
        margin-bottom: 8px;
      }
      table {
        width: 100%;
        text-align: left;
        padding-left: 8px;
      }
      td {
        width: 50%;
        font-size: 11px;
        padding-top: 4px;
      }
    `

    let markup = `
    <dialog-component id='stats'>
      <h4 class="header handle">Statistics</h4>

      <section>
        <h4>Current Session</h4>
        <table id="current">
          <tbody>
            <tr>
              <td data-name="uploaded">Uploaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="downloaded">Downloaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="ratio">Ratio:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="running">Running Time:</td>
              <td>0</td>
            </tr>
          </tbody>
        </table>
      </section>

      <section>
        <h4>Total</h4>
        <table id="total">
          <tbody>
            <tr>
              <td data-name="started">Started:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="uploaded">Uploaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="downloaded">Downloaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="ratio">Ratio:</td>
              <td>0</td>
            </tr>
            <tr>
              <td data-name="running">Running Time:</td>
              <td>0</td>
            </tr>
          </tbody>
        </table>
      </section>
		</dialog-component>`

    const shadowRoot = this.attachShadow({ mode: 'open' });
    shadowRoot.appendChild(style);
    shadowRoot.appendChild(htmlToElements(markup));

    this._valueNodes = {
      current: this.mapTableValues('current'),
      total: this.mapTableValues('total')
    }
  }

  // Create a dictionary mapping a row name to the row's value DOM node
  mapTableValues(id) {
    let table = this.shadowRoot.getElementById(id)
    let out = {}

    Array.from(table.rows).forEach( (row) => {
      let [key, value] = row.children
      out[key.dataset.name] = value
    })

    return out
  }

  // Renders values from the `stats` attribute into their
  // appropriate row in the tables
  renderValues() {
    let stats = JSON.parse(this.attributes.stats.value)

    this._renderHelper(this._valueNodes.current, stats.current)
    this._renderHelper(this._valueNodes.total, stats.total)
  }

  _renderHelper(elements, values) {
    Object.entries(elements).forEach( ([key, value]) => {
      value.textContent = values[key]
    })
  }

  // Listen for the `stats` attribute change & update the table values
  attributeChangedCallback(name, oldValue, newValue) {
    if (name == 'stats') {
      this.renderValues(JSON.parse(newValue))
    }
  }
}

customElements.define('stats-dialog', StatsDialog);
