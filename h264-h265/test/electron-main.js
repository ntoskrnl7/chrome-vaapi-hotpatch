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
let sawSupport = false;
let sawChunk = false;
let sawError = false;

function finish(code) {
  if (done) return;
  done = true;
  setTimeout(() => app.exit(code), 300);
}

async function readGpuVideoAcceleratorsInfo() {
  const gpuWin = new BrowserWindow({
    width: 900,
    height: 700,
    show: false,
    webPreferences: { backgroundThrottling: false },
  });

  await gpuWin.loadURL('chrome://gpu');
  const info = await gpuWin.webContents.mainFrame.executeJavaScript(`
    new Promise((resolve, reject) => {
      const started = Date.now();
      const timer = setInterval(() => {
        if (window.browserBridge?.gpuInfo_?.videoAcceleratorsInfo) {
          clearInterval(timer);
          resolve(window.browserBridge.gpuInfo_.videoAcceleratorsInfo);
        } else if (Date.now() - started > 5000) {
          clearInterval(timer);
          reject(new Error('Timed out waiting for chrome://gpu videoAcceleratorsInfo'));
        }
      }, 20);
    })
  `);
  gpuWin.destroy();
  return info;
}

app.whenReady().then(() => {
  const gpuInfoPromise = readGpuVideoAcceleratorsInfo().catch(error => {
    console.log('[gpu-info-error] ' + error.stack);
    return null;
  });

  const win = new BrowserWindow({
    width: 900,
    height: 700,
    show: false,
    webPreferences: { backgroundThrottling: false },
  });

  win.webContents.on('console-message', (_event, level, message, line, sourceId) => {
    console.log(`[renderer:${level}] ${message} (${sourceId}:${line})`);
    if (message.startsWith('selected=')) sawSupport = true;
    if (message.startsWith('chunk ')) sawChunk = true;
    if (message.includes('encoder error') || message.includes('fatal:') ||
        message.includes('flush rejected')) {
      sawError = true;
    }
    if (message.includes('flush resolved')) {
      console.log(`[result] support=${sawSupport} chunk=${sawChunk} error=${sawError}`);
      finish(sawSupport && sawChunk && !sawError ? 0 : 2);
    }
  });

  win.webContents.on('render-process-gone', (_event, details) => {
    console.log(`[result] render-process-gone reason=${details.reason} exitCode=${details.exitCode}`);
    finish(3);
  });

  setTimeout(() => {
    console.log(`[result] timeout support=${sawSupport} chunk=${sawChunk} error=${sawError}`);
    finish(sawSupport && sawChunk && !sawError ? 0 : 5);
  }, 18000);

  win.webContents.on('did-finish-load', async () => {
    const info = await gpuInfoPromise;
    await win.webContents.executeJavaScript(`
      window.__videoAcceleratorsInfo = ${JSON.stringify(info)};
      window.dispatchEvent(new Event('accelerators-ready'));
    `);
  });

  win.loadFile(path.join(__dirname, 'h265-encode-force-hw.html'));
});

app.on('window-all-closed', () => finish(0));
