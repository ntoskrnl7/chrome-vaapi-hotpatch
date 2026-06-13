const { app, BrowserWindow } = require('electron');
const path = require('path');

const features = [
  'AcceleratedVideoEncoder',
  'VaapiVideoDecodeLinuxGL',
  'AcceleratedVideoDecodeLinuxGL',
  'AcceleratedVideoDecodeLinuxZeroCopyGL',
  'VaapiIgnoreDriverChecks',
].join(',');

app.commandLine.appendSwitch('enable-features', features);
app.commandLine.appendSwitch('enable-blink-features', 'WebCodecs');
app.commandLine.appendSwitch('ignore-gpu-blocklist');
app.commandLine.appendSwitch('enable-logging', 'stderr');
app.commandLine.appendSwitch('vmodule', '*vaapi*=4,*video*=3,*media*=3,*webcodecs*=3');
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');

let done = false;
let sawHardwareSupport = false;
let sawChunk = false;
let sawError = false;

function finish(code) {
  if (done) return;
  done = true;
  setTimeout(() => app.exit(code), 300);
}

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 900,
    height: 700,
    show: false,
    webPreferences: {
      backgroundThrottling: false,
    },
  });

  win.webContents.on('console-message', (_event, level, message, line, sourceId) => {
    console.log(`[renderer:${level}] ${message} (${sourceId}:${line})`);
    if (message.includes('"supported":true') &&
        message.includes('"hardwareAcceleration":"prefer-hardware"')) {
      sawHardwareSupport = true;
    }
    if (message.startsWith('chunk ')) {
      sawChunk = true;
    }
    if (message.includes('encoder error') || message.includes('fatal:') ||
        message.includes('flush rejected')) {
      sawError = true;
    }
    if (message.includes('flush resolved') || message.includes('done chunks=')) {
      console.log(`[result] hardwareSupport=${sawHardwareSupport} chunk=${sawChunk} error=${sawError}`);
      finish(sawHardwareSupport && sawChunk && !sawError ? 0 : 2);
    }
  });

  win.webContents.on('render-process-gone', (_event, details) => {
    console.log(`[result] render-process-gone reason=${details.reason} exitCode=${details.exitCode}`);
    finish(3);
  });

  win.webContents.on('did-fail-load', (_event, errorCode, errorDescription) => {
    console.log(`[result] did-fail-load code=${errorCode} desc=${errorDescription}`);
    finish(4);
  });

  setTimeout(() => {
    console.log(`[result] timeout hardwareSupport=${sawHardwareSupport} chunk=${sawChunk} error=${sawError}`);
    finish(sawHardwareSupport && sawChunk && !sawError ? 0 : 5);
  }, 15000);

  win.loadFile(path.join(__dirname, 'renderer.html'));
});

app.on('window-all-closed', () => finish(0));
