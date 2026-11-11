// スクロールすると、ページに置いた物をスライムが飲みこんで大きくなる。
// 大きさはゲーム「チリツモ」と同じく、下の巻尺 (赤い線がいまの大きさ) で見せる。
(() => {
  'use strict';
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;

  // ---- 飲みこむ物 (絵は 64x64 の SVG。黒い太線で描く) ----
  const K = 'stroke="#161616" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"';
  const ITEMS = {
    eraser:  { cm: 4,   svg: `<g transform="rotate(-18 32 32)"><rect x="10" y="22" width="44" height="22" rx="3" fill="#fff" ${K}/><rect x="26" y="20" width="22" height="26" fill="#2ec4e6" ${K}/></g>` },
    candy:   { cm: 2,   svg: `<path d="M14 32 L4 22 L6 42 Z M50 32 L60 22 L58 42 Z" fill="#ffb3c7" ${K}/><circle cx="32" cy="32" r="14" fill="#ff7aa2" ${K}/><path d="M24 28 q8 -6 16 0" fill="none" stroke="#fff" stroke-width="3"/>` },
    pencil:  { cm: 17,  svg: `<g transform="rotate(-35 32 32)"><rect x="6" y="26" width="40" height="12" fill="#ffd23f" ${K}/><path d="M46 26 L60 32 L46 38 Z" fill="#f3d6a4" ${K}/><path d="M56 30 L60 32 L56 34 Z" fill="#161616"/><rect x="2" y="26" width="6" height="12" fill="#ff8fab" ${K}/></g>` },
    shogi:   { cm: 3,   svg: `<path d="M32 6 L50 16 L54 58 L10 58 L14 16 Z" fill="#e9b96e" ${K}/><text x="32" y="46" font-size="24" font-weight="900" text-anchor="middle" font-family="serif" fill="#161616">歩</text>` },
    card:    { cm: 9,   svg: `<g transform="rotate(10 32 32)"><rect x="14" y="6" width="36" height="52" rx="4" fill="#fff" ${K}/><path d="M32 20 L42 32 L32 44 L22 32 Z" fill="#f25a4f"/><path d="M19 13 l4 4 m0 -4 l-4 4" stroke="#f25a4f" stroke-width="2"/></g>` },
    pasta:   { cm: 25,  svg: `<ellipse cx="32" cy="38" rx="28" ry="16" fill="#fff" ${K}/><path d="M14 36 q6 -10 12 0 t12 0 t12 0" fill="none" stroke="#f2c14e" stroke-width="5" stroke-linecap="round"/><path d="M16 40 q6 -8 12 0 t12 0 t10 0" fill="none" stroke="#e8a838" stroke-width="4" stroke-linecap="round"/><circle cx="32" cy="30" r="5" fill="#f25a4f" ${K}/><path d="M40 14 L46 34" ${K}/>` },
    mikan:   { cm: 7,   svg: `<circle cx="32" cy="36" r="20" fill="#ff8a1f" ${K}/><path d="M32 16 q8 -8 16 -2 q-8 6 -16 2" fill="#5bb33a" ${K}/><circle cx="25" cy="30" r="2" fill="#ffd2a1"/>` },
    book:    { cm: 18,  svg: `<g transform="rotate(-8 32 32)"><rect x="12" y="8" width="40" height="50" fill="#fff" ${K}/><rect x="12" y="8" width="9" height="50" fill="#f25a4f" ${K}/><rect x="27" y="16" width="19" height="16" fill="#ffd23f" ${K}/><path d="M27 40 h18 M27 46 h12" ${K}/></g>` },
    teddy:   { cm: 35,  svg: `<circle cx="16" cy="18" r="9" fill="#b9773f" ${K}/><circle cx="48" cy="18" r="9" fill="#b9773f" ${K}/><circle cx="32" cy="34" r="22" fill="#c98a4e" ${K}/><ellipse cx="32" cy="42" rx="10" ry="7" fill="#f0d0a8" ${K}/><circle cx="24" cy="30" r="3" fill="#161616"/><circle cx="40" cy="30" r="3" fill="#161616"/><circle cx="32" cy="40" r="2.5" fill="#161616"/>` },
    balloon: { cm: 40,  svg: `<ellipse cx="32" cy="24" rx="17" ry="20" fill="#f25a4f" ${K}/><path d="M29 44 L35 44 L32 48 Z" fill="#f25a4f" ${K}/><path d="M32 48 q-6 6 0 10 t0 6" fill="none" ${K}/><ellipse cx="25" cy="17" rx="4" ry="6" fill="#ff9d94"/>` },
    chair:   { cm: 85,  svg: `<rect x="18" y="6" width="28" height="22" rx="3" fill="#c98a4e" ${K}/><rect x="14" y="30" width="36" height="8" fill="#b9773f" ${K}/><path d="M18 38 L16 60 M46 38 L48 60 M20 28 v2 M44 28 v2" ${K}/>` },
    bicycle: { cm: 170, svg: `<circle cx="16" cy="42" r="12" fill="#fff" ${K}/><circle cx="48" cy="42" r="12" fill="#fff" ${K}/><path d="M16 42 L28 24 L42 24 L48 42 M28 24 L34 42 L16 42 M42 24 L40 16 M36 16 h8 M26 20 h6" fill="none" stroke="#2ec4e6" stroke-width="4" stroke-linecap="round"/><circle cx="34" cy="42" r="3" fill="#161616"/>` },
    vending: { cm: 183, big: true, svg: `<rect x="12" y="4" width="40" height="56" rx="2" fill="#f25a4f" ${K}/><rect x="17" y="9" width="30" height="24" fill="#fff" ${K}/><path d="M23 12 v18 M29 12 v18 M35 12 v18 M41 12 v18" stroke="#2ec4e6" stroke-width="4"/><rect x="17" y="44" width="22" height="8" fill="#161616"/><rect x="42" y="38" width="5" height="8" fill="#ffd23f" ${K}/>` },
    car:     { cm: 450, big: true, svg: `<path d="M4 42 L8 26 L20 16 H52 L60 28 V42 Z" fill="#fff" ${K}/><path d="M22 20 H50 L54 28 H18 Z" fill="#2ec4e6" ${K}/><rect x="4" y="34" width="56" height="8" fill="#2a4fa0" ${K}/><circle cx="18" cy="44" r="7" fill="#161616" ${K}/><circle cx="46" cy="44" r="7" fill="#161616" ${K}/>` },
    house:   { cm: 900, big: true, svg: `<path d="M6 30 L32 8 L58 30 Z" fill="#f25a4f" ${K}/><rect x="12" y="30" width="40" height="28" fill="#f3e2c0" ${K}/><rect x="17" y="35" width="10" height="9" fill="#2ec4e6" ${K}/><rect x="37" y="35" width="10" height="9" fill="#2ec4e6" ${K}/><rect x="28" y="44" width="9" height="14" fill="#8a5a3c" ${K}/><rect x="42" y="12" width="6" height="12" fill="#a33" ${K}/>` },
    building:{ cm: 2000, big: true, svg: `<rect x="16" y="4" width="32" height="56" fill="#b8c2cc" ${K}/>${[10,20,30,40].map(y => [21,31,39].map(x => `<rect x="${x}" y="${y}" width="5" height="6" fill="#2ec4e6"/>`).join('')).join('')}<rect x="27" y="50" width="10" height="10" fill="#161616"/><path d="M28 4 V0 M36 4 V-2" ${K}/>` },
  };
  // 飲んだ数ごとの大きさ (cm)。最後はゲームの住宅街の目標と同じ 22m
  const SIZES = [5, 6, 7.5, 9, 11, 14, 18, 25, 34, 48, 70, 100, 160, 260, 500, 1100, 2200];
  // 「いまの大きさ」のたとえ
  const LIKE = [[5, '消しゴムくらい'], [8, 'あめ玉 4個ぶん'], [11, '将棋の駒 3個ぶん'], [14, 'トランプくらい'], [18, 'みかん 2個ぶん'],
    [25, 'パスタのお皿くらい'], [34, 'マンガ 2冊ぶん'], [48, 'ぬいぐるみくらい'], [70, '風船くらい'], [100, '椅子くらい'],
    [160, '自転車くらい'], [260, '自販機より大きい'], [500, '車くらい'], [1100, '家くらい'], [2200, 'ビルに届きそう。まだ、大きくなります']];
  const WORDS = ['もぐ', 'ぱくん', 'ごくん', 'もぐもぐ', 'ちゅるん'];

  const svgOf = (name) => `<svg viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">${ITEMS[name].svg}</svg>`;
  const imageOf = {};
  for (const name of Object.keys(ITEMS)) { const im = new Image(); im.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgOf(name)); imageOf[name] = im; }

  // ---- ページの物 ----
  const pickups = [...document.querySelectorAll('.pickup')].map((el) => {
    const name = el.dataset.item; el.innerHTML = svgOf(name); el.setAttribute('aria-hidden', 'true');
    if (ITEMS[name].big) el.classList.add('big');
    return { el, name, eaten: false };
  });

  // ---- スライム ----
  const canvas = document.getElementById('slime');
  const ctx = canvas.getContext('2d');
  const flyers = document.getElementById('flyers');
  const sizeValue = document.getElementById('size-value');
  const sizeLike = document.getElementById('size-like');
  const eatenCount = document.getElementById('eaten-count');
  const tape = document.getElementById('tape');
  const tapeBox = tape.parentElement;
  let vw = 0, vh = 0, dpr = 1, hud = 72;
  const state = { size: SIZES[0], target: SIZES[0], bounce: 0, inside: [], pops: [], blink: 0, eyeX: 0, eyeY: 0 };

  function resize() {
    vw = innerWidth; vh = innerHeight; dpr = Math.min(2, devicePixelRatio || 1);
    canvas.width = vw * dpr; canvas.height = vh * dpr;
    hud = document.querySelector('.hud').offsetHeight;
    buildTape();
  }
  // 画面の上でのスライムの大きさ (px)。大きさの対数で育てる
  function radiusPx() {
    const t = Math.log(state.size / SIZES[0]) / Math.log(SIZES[SIZES.length - 1] / SIZES[0]);
    const max = vw >= 1280 ? 118 : vw >= 860 ? 92 : 58;
    const min = vw >= 860 ? 26 : 20;
    return min + (max - min) * Math.min(1, Math.max(0, t));
  }
  function slimeCenter(r) {
    const x = vw >= 1280 ? vw - Math.max(150, (vw - 1040) / 2 - 20) * 0.62 : vw - r * 1.3 - 14;
    return { x, y: vh - hud - r * 0.62 - 8 };
  }

  // ---- 巻尺: 1cm〜100m を対数で並べる ----
  let ppd = 260; // 1桁ぶんの長さ (px)
  const fmt = (cm) => cm < 100 ? `${cm}cm` : `${cm / 100}m`;
  function buildTape() {
    ppd = vw < 560 ? 170 : 260;
    let html = '';
    for (let d = 0; d <= 4; ++d) for (let m = 1; m <= 9; ++m) {
      const cm = m * 10 ** d; const x = Math.log10(cm) * ppd;
      const cls = m === 1 ? 'major' : m === 5 ? 'mid' : 'small';
      html += `<i class="${cls}" style="left:${x}px"></i>`;
      if (m === 1 || m === 2 || m === 5) html += `<b style="left:${x}px">${fmt(cm)}</b>`;
      for (let s = 1; s < 4 && m < 9; ++s) { const sx = Math.log10(cm + s * 10 ** d / 4) * ppd; html += `<i class="small" style="left:${sx}px"></i>`; }
    }
    html += `<i class="major" style="left:${5 * ppd}px"></i><b style="left:${5 * ppd}px">100m</b>`;
    tape.innerHTML = html;
  }
  function updateHud() {
    const s = state.size;
    tape.style.transform = `translateX(${tapeBox.clientWidth / 2 - Math.log10(s) * ppd}px)`;
    const text = s < 100 ? s.toFixed(1) : (s / 100).toFixed(s < 1000 ? 2 : 1);
    sizeValue.innerHTML = `${text}<small>${s < 100 ? 'cm' : 'm'}</small>`;
  }
  function likeText(target) { let t = LIKE[0][1]; for (const [cm, text] of LIKE) if (target >= cm) t = text; return t; }

  // ---- 飲みこむ・吐き出す ----
  function eat(p, animate) {
    p.eaten = true; p.el.classList.add('eaten');
    const done = () => {
      state.inside.push({ name: p.name, a: Math.random() * Math.PI * 2, d: .15 + Math.random() * .4, spin: Math.random() * 6, rot: (Math.random() - .5) * .6 });
      if (state.inside.length > 14) state.inside.shift();
      state.bounce = 1;
      state.pops.push({ text: WORDS[Math.floor(Math.random() * WORDS.length)], age: 0, dx: (Math.random() - .5) * 1.4 });
    };
    if (!animate || reduce) { done(); return; }
    const from = p.el.getBoundingClientRect();
    const r = radiusPx(); const to = slimeCenter(r);
    const f = document.createElement('div'); f.className = 'flyer'; f.innerHTML = svgOf(p.name);
    f.style.width = from.width + 'px'; f.style.height = from.height + 'px';
    flyers.appendChild(f);
    const sx = from.left, sy = from.top, ex = to.x - from.width / 2, ey = to.y - from.height / 2;
    const cx = (sx + ex) / 2 + (ex > sx ? -80 : 80), cy = Math.min(sy, ey) - 120; // 放物線の頂点
    const t0 = performance.now(), dur = 620;
    (function step(now) {
      const u = Math.min(1, (now - t0) / dur), e = u * u * (3 - 2 * u);
      const x = (1 - e) ** 2 * sx + 2 * (1 - e) * e * cx + e * e * ex;
      const y = (1 - e) ** 2 * sy + 2 * (1 - e) * e * cy + e * e * ey;
      const sc = 1 - .65 * e;
      f.style.transform = `translate(${x}px, ${y}px) rotate(${e * 540}deg) scale(${sc})`;
      if (u < 1) requestAnimationFrame(step); else { f.remove(); done(); }
    })(t0);
  }
  function spit(p) {
    p.eaten = false; p.el.classList.remove('eaten');
    const i = state.inside.map((c) => c.name).lastIndexOf(p.name); if (i >= 0) state.inside.splice(i, 1);
    state.bounce = .6;
  }
  // 物が画面の上 45% より上へ来たら飲みこむ。戻ると吐き出す
  function check(animate) {
    const line = vh * .45;
    // いちばん下まで来たら、残りも全部飲みこむ
    const atEnd = innerHeight + scrollY >= document.documentElement.scrollHeight - 4;
    for (const p of pickups) {
      const r = p.el.getBoundingClientRect();
      const passed = atEnd || r.top + r.height / 2 < line;
      if (passed && !p.eaten) eat(p, animate);
      else if (!passed && p.eaten) spit(p);
    }
    const n = pickups.filter((p) => p.eaten).length;
    state.target = SIZES[Math.min(n, SIZES.length - 1)];
    eatenCount.textContent = n;
    sizeLike.textContent = likeText(state.target);
    // 目は次に飲む物の方を見る
    const next = pickups.find((p) => !p.eaten);
    if (next) { const r = next.el.getBoundingClientRect(); const c = slimeCenter(radiusPx()); const dx = r.left - c.x, dy = r.top - c.y, l = Math.hypot(dx, dy) || 1; state.eyeX = dx / l; state.eyeY = dy / l; }
    else { state.eyeX = 0; state.eyeY = -.3; }
  }

  // ---- 描画 ----
  function blobPath(cx, cy, r, t) {
    const b = state.bounce, n = 56;
    ctx.beginPath();
    for (let i = 0; i <= n; ++i) {
      const a = i / n * Math.PI * 2;
      const w = 1 + .035 * Math.sin(3 * a + t * 2.1) + .025 * Math.sin(5 * a - t * 1.4) + .05 * b * Math.sin(2 * a + t * 9);
      let x = cx + Math.cos(a) * r * 1.22 * w * (1 + .14 * b);
      let y = cy + Math.sin(a) * r * .95 * w * (1 - .12 * b);
      const floor = cy + r * .58; if (y > floor) y = floor + (y - floor) * .18; // 底は平ら (床に乗っている)
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.closePath();
  }
  let last = performance.now();
  function frame(now) {
    const dt = Math.min(.05, (now - last) / 1000); last = now; const t = now / 1000;
    // 大きさは対数でなめらかに追いかける
    const ls = Math.log(state.size), lt = Math.log(state.target);
    state.size = Math.exp(ls + (lt - ls) * Math.min(1, dt * (reduce ? 20 : 3.5)));
    state.bounce = Math.max(0, state.bounce - dt * 2.2);
    updateHud();

    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, vw, vh);
    const r = radiusPx(); const { x: cx, y: cy } = slimeCenter(r);
    // 影
    ctx.fillStyle = 'rgba(0,0,0,.18)'; ctx.beginPath(); ctx.ellipse(cx + 6, cy + r * .6 + 4, r * 1.25, r * .16, 0, 0, Math.PI * 2); ctx.fill();
    // 体 (半透明)
    blobPath(cx, cy, r, t);
    ctx.fillStyle = 'rgba(166, 226, 46, .62)'; ctx.fill();
    // 中身: 飲んだ物が体の中に浮かぶ。小さい物ほど小さく見える
    ctx.save(); blobPath(cx, cy, r * .96, t); ctx.clip();
    for (const c of state.inside) {
      const im = imageOf[c.name]; if (!im.complete) continue;
      const s = Math.max(9, Math.min(r * .8, ITEMS[c.name].cm / state.size * r * 2.4));
      const a = c.a + t * .25, x = cx + Math.cos(a) * r * c.d * 1.05, y = cy + Math.sin(a) * r * c.d * .6 + Math.sin(t * 1.7 + c.spin) * 2;
      ctx.save(); ctx.globalAlpha = .82; ctx.translate(x, y); ctx.rotate(c.rot + Math.sin(t + c.spin) * .2); ctx.drawImage(im, -s / 2, -s / 2, s, s); ctx.restore();
    }
    ctx.fillStyle = 'rgba(166, 226, 46, .28)'; ctx.fillRect(cx - r * 2, cy - r * 2, r * 4, r * 4);
    ctx.restore();
    // 輪郭とツヤ
    blobPath(cx, cy, r, t); ctx.lineWidth = Math.max(2.5, r * .045); ctx.strokeStyle = '#161616'; ctx.stroke();
    ctx.fillStyle = 'rgba(255,255,255,.7)'; ctx.beginPath(); ctx.ellipse(cx - r * .55, cy - r * .5, r * .26, r * .1, -.5, 0, Math.PI * 2); ctx.fill();
    // 目 (ときどきまばたき)
    state.blink -= dt; if (state.blink < -3.2 - Math.random()) state.blink = .14;
    const open = state.blink > 0 ? .15 : 1;
    for (const side of [-1, 1]) {
      const ex = cx + side * r * .3 + state.eyeX * r * .06, ey = cy - r * .22 + state.eyeY * r * .05;
      ctx.fillStyle = '#161616'; ctx.beginPath(); ctx.ellipse(ex, ey, r * .1, r * .15 * open, 0, 0, Math.PI * 2); ctx.fill();
      if (open > .5) { ctx.fillStyle = '#fff'; ctx.beginPath(); ctx.arc(ex - r * .03, ey - r * .05, r * .035, 0, Math.PI * 2); ctx.fill(); }
    }
    // 擬音
    for (const p of state.pops) {
      p.age += dt; const u = p.age / .8; if (u >= 1) continue;
      const fs = Math.max(18, r * .42) * (u < .15 ? .6 + u * 2.7 : 1);
      ctx.save(); ctx.globalAlpha = u > .7 ? (1 - u) / .3 : 1;
      ctx.translate(cx + p.dx * r, cy - r * 1.05 - u * 26); ctx.rotate(-.12);
      ctx.font = `${fs}px "Dela Gothic One", sans-serif`; ctx.textAlign = 'center';
      ctx.lineWidth = 6; ctx.strokeStyle = '#161616'; ctx.strokeText(p.text, 0, 0); ctx.fillStyle = '#fff'; ctx.fillText(p.text, 0, 0);
      ctx.restore();
    }
    state.pops = state.pops.filter((p) => p.age < .8);
    requestAnimationFrame(frame);
  }

  // ---- 動画: 押すまで YouTube を読み込まない ----
  function setupVideos(root) {
    for (const box of root.querySelectorAll('.video[data-video]')) {
      const id = box.dataset.video, title = box.dataset.title || '紹介動画';
      const btn = document.createElement('button'); btn.className = 'video-btn'; btn.type = 'button';
      btn.innerHTML = '<span>紹介動画を再生</span>'; btn.setAttribute('aria-label', `${title}を再生`);
      btn.addEventListener('click', () => {
        box.innerHTML = `<iframe src="https://www.youtube-nocookie.com/embed/${id}?autoplay=1&rel=0" title="${title}" allow="autoplay; encrypted-media; picture-in-picture" allowfullscreen></iframe>`;
      });
      box.appendChild(btn);
      const link = document.createElement('a'); link.className = 'video-link'; link.href = `https://youtu.be/${id}`; link.target = '_blank'; link.rel = 'noopener'; link.textContent = 'YouTubeで開く ↗';
      box.appendChild(link);
    }
  }

  // ---- 作品の詳細: カードを押すと #work-… になり、同じ名前の template をダイアログで開く ----
  const dialog = document.getElementById('detail');
  const detailBody = dialog.querySelector('.detail-body');
  let opener = null;
  function openDetail(id) {
    const tpl = document.getElementById(id);
    if (!tpl || tpl.tagName !== 'TEMPLATE') return false;
    detailBody.className = `detail-body work work-${tpl.dataset.color || 'paper'}`;
    detailBody.replaceChildren(tpl.content.cloneNode(true));
    setupVideos(detailBody);
    opener = document.activeElement;
    if (!dialog.open) dialog.showModal();
    dialog.scrollTop = 0;
    document.documentElement.classList.add('locked');
    return true;
  }
  function closeDetail() {
    if (dialog.open) dialog.close();
  }
  dialog.addEventListener('close', () => {
    if (dialog.open) return; // 閉じた直後に別の作品を開いたとき
    detailBody.replaceChildren(); // 動画も止まる
    document.documentElement.classList.remove('locked');
    if (location.hash.startsWith('#work-')) history.replaceState(null, '', location.pathname + location.search);
    if (opener && opener.focus) opener.focus();
  });
  dialog.querySelector('.detail-close').addEventListener('click', closeDetail);
  dialog.addEventListener('click', (e) => { if (e.target === dialog) closeDetail(); }); // 外側を押すと閉じる
  function fromHash() { const id = decodeURIComponent(location.hash.slice(1)); if (id.startsWith('work-')) openDetail(id); else closeDetail(); }
  addEventListener('hashchange', fromHash);

  // ---- 起動 ----
  resize(); check(false); state.size = state.target; updateHud();
  addEventListener('resize', () => { resize(); check(false); });
  let ticking = false;
  addEventListener('scroll', () => { if (ticking) return; ticking = true; requestAnimationFrame(() => { ticking = false; check(true); }); }, { passive: true });
  addEventListener('load', () => check(false));
  fromHash();
  requestAnimationFrame(frame);
})();
