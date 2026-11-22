const endpoint = 'http://127.0.0.1:18765';
const address = document.getElementById('url');
const button = document.getElementById('go');
const status = document.getElementById('status');

function showStatus(message, ok) {
  status.textContent = message;
  status.className = ok ? 'ok' : 'err';
}

function httpAddress(value) {
  if (!value) return '';
  try {
    const url = new URL(value);
    if ((url.protocol === 'http:' || url.protocol === 'https:') && !url.username && !url.password) return url.href;
  } catch {
    return '';
  }
  return '';
}

async function getAddress() {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  if (tab?.id != null) {
    try {
      const [{ result }] = await chrome.scripting.executeScript({
        target: { tabId: tab.id },
        func: () => {
          const videos = [...document.querySelectorAll('video')];
          const direct = videos.filter(video => /^https?:\/\//i.test(video.currentSrc));
          return (direct.find(video => !video.paused) || direct[0])?.currentSrc;
        }
      });
      if (httpAddress(result)) return httpAddress(result);
    } catch {
      // Some tabs allow the current URL to be read but do not allow injection.
    }
  }
  return httpAddress(tab?.url);
}

let selectedUrl = '';
getAddress().then(url => {
  selectedUrl = url;
  address.textContent = url;
  button.disabled = !url;
  if (!url) showStatus('The current tab has no usable HTTP or HTTPS address', false);
}).catch(() => showStatus('Could not read the current tab', false));

button.addEventListener('click', async () => {
  if (!selectedUrl) return;
  button.disabled = true;
  showStatus('Connecting to Veyra…', true);
  try {
    const running = await fetch(endpoint + '/ping', {
      cache: 'no-store',
      signal: AbortSignal.timeout(2000)
    }).then(response => response.ok, () => false);
    if (!running) {
      showStatus('Starting Veyra via the protocol…', true);
      window.location.href = 'veyra://play?url=' + encodeURIComponent(selectedUrl);
      return;
    }
    const response = await fetch(endpoint + '/play', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url: selectedUrl }),
      signal: AbortSignal.timeout(5000)
    });
    if (!response.ok) throw new Error('Veyra refused the play request');
    showStatus('Address sent', true);
  } catch (error) {
    showStatus(error.message || 'Failed to send address', false);
    button.disabled = false;
  }
});
