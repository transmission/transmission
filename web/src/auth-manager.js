import { RPC } from './remote.js';
import { getAppRoot } from './utils.js';

const localStorageKey = 'credentials';

export const AuthManager = {
  /**
   * Given draft headers for a request, adds the headers for authorization.
   * @param {Headers} headers
   */
  addAuthHeaders(headers, credentials = this.loadCredentials()) {
    // Transmission can be configured without authentication. We cannot assume
    // that absence of stored credentials indicates absence of authorization.
    if (credentials) {
      const encodedCredentials = btoa(
        `${credentials.username}:${credentials.password}`,
      );
      headers.append('Authorization', `Basic ${encodedCredentials}`);
    }
    // This prevents the server from forcing a basic auth dialog upon us if our
    // credentials are invalidated out from underneath us.
    headers.append('TransmissionRPC-Suppress-Basic-Auth-Popup', 'true');
  },

  /**
   * Purges the stored credentials
   */
  clearCredentials() {
    localStorage.removeItem(localStorageKey);
  },

  /**
   * Retrieves the stored credentials
   * @return {{username: string; password: string; createdAt: Date} | null}
   */
  loadCredentials() {
    const credentialsJsor = localStorage.getItem(localStorageKey);
    if (credentialsJsor === null) {
      return null;
    }
    const credentials = JSON.parse(credentialsJsor);
    credentials.createdAt = new Date(credentials.createdAt);
    return credentials;
  },

  redirectToLogin() {
    window.location.href = `${getAppRoot()}/login/index.html`;
  },

  /**
   * Stores the supplied credentials
   * @param {string} username
   * @param {string} password
   */
  storeCredentials(username, password) {
    localStorage.setItem(
      localStorageKey,
      JSON.stringify({
        createdAt: new Date().toISOString(),
        password,
        username,
      }),
    );
  },

  /**
   * Makes a test HTTP request to confirm the supplied credentials are valid
   * @param {{username: string, password: string} | null} credentials
   * @return {Promise<boolean>}
   */
  async testCredentials(credentials) {
    const headers = new Headers();
    this.addAuthHeaders(headers, credentials);
    // Issue an empty bodied no-op request to the RPC endpoint to see if our
    // credentials (or lack thereof) are valid
    const response = await fetch(RPC._Root, {
      headers,
      method: 'POST',
    });

    if (response.status === 401) {
      return false;
      // RPC endpoint returns 409 if session id isn't established yet. But we are
      // actually authenticated.
    } else if (response.status !== 204 && response.status !== 409) {
      throw new Error(
        'Unexpected response from RPC auth check',
        response.status,
      );
    }

    return true;
  },
};
