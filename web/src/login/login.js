import { AuthManager } from '../auth-manager.js';
import { getAppRoot } from '../utils.js';

document.addEventListener('DOMContentLoaded', async () => {
  // There's an edge case where the user can wind up on the login page even
  // though they don't need credentials. Maybe they used a browser bookmark, or
  // they logged in with a different tab. Maybe Transmission was previously
  // configured to require authentication, but now isn't.
  //
  // We'll just send the user straight into the web UI, since there's nothing
  // for them to do here, and Transmission might not be set up for logins
  // anyway.
  const alreadyLoggedIn = await AuthManager.testCredentials(
    AuthManager.loadCredentials(),
  );
  if (alreadyLoggedIn) {
    window.location.href = getAppRoot();
  }

  const loginForm = document.querySelector('#login-form');

  loginForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    document.querySelector('#login-error').classList.remove('draw-attention');

    const username = document.querySelector('#username').value;
    const password = document.querySelector('#password').value;

    const credsAreValid = await AuthManager.testCredentials({
      password,
      username,
    });
    if (!credsAreValid) {
      document.querySelector('#login-error').classList.remove('hidden');
      document.querySelector('#login-error').classList.add('draw-attention');
      return;
    }

    AuthManager.storeCredentials(username, password);
    window.location.href = getAppRoot();
  });
});
