import DialogComponent from './dialogComponent.js'
import { htmlToElements } from './utils.js';

class StatsDialog extends HTMLElement {
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
        <table>
          <tbody>
            <tr>
              <td>Uploaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Downloaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Ratio:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Running Time:</td>
              <td>0</td>
            </tr>
          </tbody>
        </table>
      </section>

      <section>
        <h4>Total</h4>
        <table>
          <tbody>
            <tr>
              <td>Started:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Uploaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Downloaded:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Ratio:</td>
              <td>0</td>
            </tr>
            <tr>
              <td>Running Time:</td>
              <td>0</td>
            </tr>
          </tbody>
        </table>
      </section>
		</dialog-component>`

    const shadowRoot = this.attachShadow({ mode: 'open' });
    shadowRoot.appendChild(style);
    shadowRoot.appendChild(htmlToElements(markup));
  }
}

customElements.define('stats-dialog', StatsDialog);
