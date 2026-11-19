(() => {
  let port = null;
  function ping() {
    try {
      if (!port) {
        port = chrome.runtime.connect({name: 'uzi'});
        port.onDisconnect.addListener(() => { port = null; });
      }
      port.postMessage(1);
    } catch (e) { port = null; }
  }
  ping();
  setInterval(() => { if (!document.hidden) ping(); }, 15000);
  chrome.runtime.sendMessage({q: 'who'}).then(r => { if (r && r.uzi) uzi(r); }).catch(() => {});

  function uzi(info) {
    const FACE = n => chrome.runtime.getURL('face' + n + '.png');
    const isGoogle = /(^|\.)google\.[a-z.]+$/.test(location.hostname);
    const isResults = isGoogle && location.pathname.startsWith('/search');
    const isImages = isResults && /(^|&)(udm=2|tbm=isch)(&|$)/.test(location.search.slice(1));
    const hash = new URLSearchParams(location.hash.slice(1));
    let q = (info.meta && info.meta.q) || '', shame = (info.meta && info.meta.shame) || 0;
    if (isResults) q = new URLSearchParams(location.search).get('q') || q;
    if (hash.has('uzis')) shame = +hash.get('uzis');
    let face = shame === 2 ? 2 : 0;

    function mark() {
      if (!document.title.startsWith('Узи · ')) document.title = 'Узи · ' + (document.title || location.hostname);
      if (!document.head) return;
      let l = document.getElementById('uzi-ico');
      if (!l) {
        document.querySelectorAll('link[rel*="icon"]').forEach(e => e.remove());
        l = document.createElement('link');
        l.id = 'uzi-ico';
        l.rel = 'icon';
        document.head.append(l);
      }
      if (l.href !== FACE(face)) l.href = FACE(face);
    }
    mark();
    new MutationObserver(mark).observe(document.head || document.documentElement, {childList: true, subtree: true, characterData: true});

    const OK = ['wikipedia.org', 'wikimedia.org', 'stackoverflow.com', 'stackexchange.com', 'habr.com', 'github.com', 'developer.mozilla.org',
      'learn.microsoft.com', 'minecraft.wiki', 'fandom.com', 'nplus1.ru', 'naked-science.ru', 'elementy.ru', 'postnauka.ru', 'nasa.gov',
      'esa.int', 'blender.org', 'unrealengine.com', 'learnopengl.com', 'khronos.org', 'unsplash.com', 'pexels.com', 'pixabay.com',
      'wikihow.com', 'britannica.com'];
    const realHref = href => {
      try {
        const u = new URL(href);
        if (/(^|\.)google\.[a-z.]+$/.test(u.hostname) && /^\/(url|goto)$/.test(u.pathname)) return u.searchParams.get('url') || u.searchParams.get('q') || href;
        return href;
      } catch (e) { return href; }
    };
    const okHost = h => OK.some(d => h === d || h.endsWith('.' + d));
    const allowed = href => { try { return okHost(new URL(realHref(href)).hostname); } catch (e) { return false; } };
    const resultOk = a => {
      if (!a) return false;
      if (allowed(a.href)) return true;
      const box = a.closest('[data-hveid], .g, div') || a;
      const cite = box.querySelector('cite');
      const m = cite && cite.textContent.match(/([a-z0-9-]+\.)+[a-z]{2,}/i);
      return !!(m && okHost(m[0].toLowerCase()));
    };
    const open = a => {
      const h = realHref(a.href);
      if (/^https?:/.test(h) && allowed(h)) location.href = h;
      else a.click();
    };

    const ids = new WeakMap(), byId = new Map();
    let nextId = 1;
    const idOf = e => { let i = ids.get(e); if (!i) { i = nextId++; ids.set(e, i); byId.set(i, e); } return i; };
    function collect() {
      const out = [], vh = innerHeight;
      const add = (e, type) => {
        const r = e.getBoundingClientRect();
        if (r.width < 40 || r.height < 8 || r.bottom < 0 || r.top > vh) return;
        out.push([idOf(e), Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height), type]);
      };
      if (isImages) {
        document.querySelectorAll('#search img, #islrg img, div[data-ri] img').forEach(e => { if (e.width > 80 && e.height > 60) add(e, 3); });
      } else if (isGoogle) {
        document.querySelectorAll('a h3').forEach(e => add(e, resultOk(e.closest('a')) ? 3 : 1));
        const box = document.querySelector('textarea[name=q], input[name=q]');
        if (box) add(box, 1);
      } else {
        document.querySelectorAll('h1, h2, h3').forEach(e => add(e, 1));
        document.querySelectorAll('p').forEach(e => { if (e.innerText.length > 60) add(e, 0); });
        document.querySelectorAll('img').forEach(e => { if (e.width > 90 && e.height > 60) add(e, 2); });
        document.querySelectorAll('pre').forEach(e => add(e, 4));
      }
      out.sort((a, b) => a[2] - b[2]);
      return out.slice(0, 60);
    }
    let last = 0;
    function send(force) {
      const now = Date.now();
      if (!force && now - last < 60) return;
      last = now;
      const topic = q || document.title.replace(/^Узи · /, '');
      const de = document.documentElement;
      const s = {visible: !document.hidden, focused: document.hasFocus(), iw: innerWidth, ih: innerHeight, dpr: devicePixelRatio,
        sy: scrollY, sh: de.scrollHeight, cw: de.clientWidth,
        mode: isGoogle ? 'search' : 'site', shame, topic, title: document.title, els: collect()};
      chrome.runtime.sendMessage({q: 'state', s}).catch(() => {});
    }
    addEventListener('scroll', () => send(), {passive: true});
    addEventListener('resize', () => send(true));
    addEventListener('focus', () => send(true));
    addEventListener('blur', () => send(true));
    document.addEventListener('visibilitychange', () => send(true));
    (function beat() { send(true); setTimeout(beat, document.hidden ? 4000 : 700); })();

    function search(nq, sh, img) {
      chrome.runtime.sendMessage({q: 'meta', meta: {q: nq, shame: sh}}).catch(() => {});
      if (isGoogle && !isResults) return type(nq, sh, img);
      location.href = 'https://www.google.com/?hl=ru#uziq=' + encodeURIComponent(nq) + '&uzis=' + sh + (img ? '&uzii=1' : '');
    }
    function type(nq, sh, img) {
      chrome.runtime.sendMessage({q: 'meta', meta: {q: nq, shame: sh}}).catch(() => {});
      const box = document.querySelector('textarea[name=q], input[name=q]');
      const go = () => { location.href = 'https://www.google.com/search?hl=ru&safe=active' + (img ? '&udm=2' : '') + '&q=' + encodeURIComponent(nq) + '#uzis=' + sh; };
      if (!box || document.hidden) return go();
      let i = 0;
      const step = () => {
        box.value = nq.slice(0, ++i);
        box.dispatchEvent(new Event('input', {bubbles: true}));
        if (i < nq.length) setTimeout(step, 70 + Math.random() * 90);
        else setTimeout(go, 600);
      };
      step();
    }
    if (isGoogle && !isResults && hash.has('uziq')) setTimeout(() => type(hash.get('uziq'), shame, hash.has('uzii')), 900);
    else if (info.pending) setTimeout(() => search(info.pending.q, info.pending.shame, info.pending.img), 1200);

    function press(e) {
      e.style.transition = 'transform .12s';
      e.style.transform = 'translateY(3px)';
      setTimeout(() => { e.style.transform = ''; }, 200);
      if (isImages) { setTimeout(() => (e.closest('a, [role=button], div[data-ri]') || e).click(), 260); return; }
      const a = e.closest && e.closest('a');
      if (a && resultOk(a)) setTimeout(() => open(a), 260);
    }
    let imgClicks = 0;
    function auto() {
      if (isImages) {
        const tiles = [...document.querySelectorAll('#search img, #islrg img, div[data-ri] img')].filter(e => e.width > 80 && e.height > 60);
        if (tiles.length && imgClicks++ % 2 === 0) press(tiles[Math.floor(Math.random() * Math.min(tiles.length, 20))]);
        else scrollBy({top: innerHeight * 0.7, behavior: 'smooth'});
        return;
      }
      if (isResults) {
        if (shame === 2) return;
        const links = [...document.querySelectorAll('a h3')].map(h => h.closest('a')).filter(resultOk);
        if (links.length) open(links[Math.floor(Math.random() * Math.min(links.length, 5))]);
        else scrollBy({top: 500, behavior: 'smooth'});
      } else if (!isGoogle) {
        const hs = [...document.querySelectorAll('h2, h3')].map(h => h.textContent.trim().replace(/\[.*?\]/g, ''))
          .filter(t => t.length > 3 && t.length < 50 && !/^(примечания|литература|ссылки|содержание|см\. также|навигация)/i.test(t));
        if (hs.length && shame === 0 && Math.random() < 0.25) search(hs[Math.floor(Math.random() * hs.length)], 0);
        else if (scrollY + innerHeight < document.documentElement.scrollHeight - 80 && Math.random() < 0.7) scrollBy({top: innerHeight * 0.7, behavior: 'smooth'});
        else history.back();
      }
    }
    chrome.runtime.onMessage.addListener(c => {
      if (c.c === 'search') search(c.q, c.shame || 0, c.img);
      else if (c.c === 'scroll') scrollBy({top: c.dy, behavior: 'smooth'});
      else if (c.c === 'press') { const e = byId.get(c.id); if (e) press(e); }
      else if (c.c === 'back') history.back();
      else if (c.c === 'auto') auto();
      else if (c.c === 'hide') search(c.q, 0);
      else if (c.c === 'face') { face = c.n; mark(); }
    });
    setInterval(() => { if (document.hidden && shame < 2) { face = 1; mark(); setTimeout(() => { face = 0; mark(); }, 250); } }, 5200);
  }
})();
