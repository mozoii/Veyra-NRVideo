# Play in Veyra

The extension hands the direct HTTP or HTTPS address of the HTML5 video currently playing in the tab to Veyra, and also supports opening a direct media address in a tab. Signed-in web pages, DRM, blob addresses and browser-specific video streams must play in the browser.

1. Open the extension management page in Chrome or Edge and enable developer mode.
2. Choose "Load unpacked" and select this directory.
3. Open a media address, click the extension button, then click "Open address in Veyra".

When Veyra is running, the extension sends the address through `127.0.0.1:18765`. To start Veyra from a closed state, first run `install-protocol.cmd` in this directory to register `veyra://play`. In the player you can also paste an HTTP, HTTPS or RTSP media link directly via "Play address".
