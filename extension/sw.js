const PET = 'http://127.0.0.1:47731/ext/';
let uziTab = null, keptTab = null, seq = -1, busy = false, loaded = false;

async function load() {
  if (loaded) return;
  const s = await chrome.storage.session.get(['uziTab', 'keptTab', 'seq']);
  uziTab = s.uziTab ?? null;
  keptTab = s.keptTab ?? null;
  seq = s.seq ?? -1;
  loaded = true;
}
async function setTab(id) { uziTab = id; await chrome.storage.session.set({uziTab: id}); }
async function setKept(id) { keptTab = id; await chrome.storage.session.set({keptTab: id}); }

let key = null;
async function getKey(fresh) {
  if (key && !fresh) return key;
  try { key = (await (await fetch(chrome.runtime.getURL('token.json'), {cache: 'no-store'})).json()).t; } catch (e) { key = null; }
  return key;
}
async function call(what, init) {
  init = init || {};
  init.headers = {'X-Uzi': (await getKey()) || ''};
  let r = await fetch(PET + what, init);
  if (r.status === 403) {
    init.headers = {'X-Uzi': (await getKey(true)) || ''};
    r = await fetch(PET + what, init);
  }
  return r;
}

async function post(what, body) {
  try { await call(what, {method: 'POST', body: JSON.stringify(body)}); } catch (e) {}
}

async function poll() {
  await load();
  if (busy) return;
  busy = true;
  try {
    const j = await (await call('poll?since=' + seq + '&tab=' + (uziTab ?? -1))).json();
    if (seq >= 0) for (const c of j.cmds) await run(c);
    seq = j.seq;
    await chrome.storage.session.set({seq});
  } catch (e) {}
  busy = false;
}

async function alive(id) {
  if (id == null) return false;
  try { await chrome.tabs.get(id); return true; } catch (e) { return false; }
}

async function run(c) {
  if (c.c === 'visit') {
    if (keptTab != null && keptTab !== uziTab && (await alive(keptTab))) chrome.tabs.remove(keptTab).catch(() => {});
    await setKept(null);
    if (!(await alive(uziTab))) {
      const w = await chrome.windows.getLastFocused({windowTypes: ['normal']});
      const t = await chrome.tabs.create({windowId: w.id, url: 'https://www.google.com/?hl=ru', active: false});
      await setTab(t.id);
      await chrome.storage.session.set({pending: c.q ? {q: c.q, shame: c.shame || 0, img: !!c.img} : null, meta: null});
    }
  } else if (c.c === 'leave') {
    if (await alive(uziTab)) await setKept(uziTab);
    await setTab(null);
  } else if (await alive(uziTab)) {
    chrome.tabs.sendMessage(uziTab, c).catch(() => {});
  }
}

chrome.runtime.onMessage.addListener((msg, sender, reply) => {
  (async () => {
    await load();
    const mine = sender.tab && sender.tab.id === uziTab;
    if (msg.q === 'who') {
      let pending = null;
      if (mine) {
        const s = await chrome.storage.session.get('pending');
        pending = s.pending || null;
      }
      const m = await chrome.storage.session.get('meta');
      reply({uzi: mine, pending, meta: mine ? (m.meta || null) : null});
    } else if (msg.q === 'meta' && mine) {
      await chrome.storage.session.set({meta: msg.meta, pending: null});
      reply({});
    } else if (msg.q === 'state' && mine) {
      post('state', msg.s);
      reply({});
    } else reply({});
  })();
  return true;
});

chrome.tabs.onUpdated.addListener(async (id, info, tab) => {
  await load();
  if (id !== uziTab) return;
  const url = info.url || (info.status === 'complete' ? tab.url : null);
  if (url && /^https?:/.test(url)) {
    chrome.history.deleteUrl({url}).catch(() => {});
    setTimeout(() => chrome.history.deleteUrl({url}).catch(() => {}), 3000);
  }
});

chrome.tabs.onActivated.addListener(async info => {
  await load();
  if (info.tabId === keptTab) await setKept(null);
});

chrome.tabs.onRemoved.addListener(async id => {
  await load();
  if (id === keptTab) await setKept(null);
  if (id === uziTab) {
    await setTab(null);
    post('state', {closed: true});
  }
});

chrome.runtime.onConnect.addListener(port => port.onMessage.addListener(() => poll()));
chrome.alarms.create('uzi', {periodInMinutes: 0.5});
chrome.alarms.onAlarm.addListener(() => poll());
setInterval(poll, 700);
poll();
