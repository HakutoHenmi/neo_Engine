(() => {
  'use strict';
  const data = window.PORTFOLIO;
  const grid = document.querySelector('#work-grid');
  const dialog = document.querySelector('#project-dialog');
  let lastFocus;
  const node = (tag, className, text) => { const el = document.createElement(tag); if (className) el.className = className; if (text != null) el.textContent = text; return el; };
  const safeUrl = (value, email = false) => { try { const u = new URL(value); return (u.protocol === 'https:' || (email && u.protocol === 'mailto:')) ? u.href : ''; } catch { return ''; } };
  const assetUrl = value => typeof value==='string' && /^\.\/assets\/[a-zA-Z0-9_./-]+$/.test(value) && !value.includes('..') ? value : '';
  const cover = (p, detailed = false) => {
    const el = node('div', `work-cover ${p.category}${detailed ? ' detail-cover' : ''}`);
    const imageUrl = assetUrl(p.image) || safeUrl(p.image);
    if (imageUrl) { const img = node('img','custom-cover'); img.src = p.image; img.alt = p.title; img.loading = 'lazy'; el.append(img); }
    else { const top = node('div','cover-top'); top.append(node('span','',`ARCHIVE ${p.id}`),node('span','','SAMPLE PROJECT')); const bottom = node('div','cover-bottom'); bottom.append(node('span','',p.category==='game'?'GAME PROJECT':'TECHNICAL DEMO'),node('span','','PREVIEW COMING SOON')); el.append(top,node('div','cover-number',p.id),bottom); }
    return el;
  };
  const tags = p => { const el = node('div','tags'); p.tags.forEach(t => el.append(node('span','',t))); return el; };
  function openModal(el) { lastFocus = document.activeElement; el.showModal(); document.body.classList.add('modal-open'); }
  function closeModal(el) { el.close(); }
  [dialog].forEach(el => { el.querySelector('.dialog-close').addEventListener('click',()=>closeModal(el)); el.addEventListener('close',()=>{ document.body.classList.remove('modal-open'); lastFocus?.focus(); }); el.addEventListener('click',e=>{ if (e.target !== el) return; const rect = el.getBoundingClientRect(); if(e.clientX < rect.left || e.clientX > rect.right || e.clientY < rect.top || e.clientY > rect.bottom) closeModal(el); }); });
  function showProject(p) {
    const detail = document.querySelector('#project-detail'); detail.replaceChildren();
    const heading=node('h2','detail-title',p.title); heading.id='detail-title';
    detail.append(node('p','eyebrow',`ARCHIVE ${p.id} / ${p.category==='game'?'GAME':'TECH DEMO'}${p.year?' / '+p.year:''}`),cover(p,true),heading,tags(p),node('p','detail-description',p.description));
    const facts=node('dl','detail-facts');
    [['担当範囲',p.responsibility],['実装の工夫',p.highlights],['使用技術',p.tools]].forEach(([label,value])=>{const row=node('div','detail-row');row.append(node('dt','',label),node('dd','',value || '追加予定'));facts.append(row);});
    detail.append(facts);
    const video = assetUrl(p.video) || safeUrl(p.video);
    if (video) {
      const u = new URL(video,location.href); let embed = '';
      if (['www.youtube.com','youtube.com','m.youtube.com'].includes(u.hostname)) { const id = u.searchParams.get('v') || u.pathname.match(/^\/(?:embed|shorts)\/([\w-]{11})/)?.[1]; if (/^[\w-]{11}$/.test(id || '')) embed = `https://www.youtube-nocookie.com/embed/${id}`; }
      if (u.hostname === 'youtu.be' && /^\/[\w-]{11}$/.test(u.pathname)) embed = `https://www.youtube-nocookie.com/embed${u.pathname}`;
      if (embed) { const frame = node('iframe','detail-video'); frame.src = embed; frame.title = `${p.title} 紹介動画`; frame.allow = 'fullscreen; picture-in-picture'; frame.referrerPolicy = 'strict-origin-when-cross-origin'; detail.append(frame); const link=node('a','button','YouTubeで見る ↗'); link.href=video; link.target='_blank'; link.rel='noopener noreferrer'; detail.append(link); }
      else if (/\.mp4$/i.test(u.pathname)) { const player = node('video','detail-video'); player.src = video; player.controls = true; player.preload = 'metadata'; player.playsInline=true; const poster=assetUrl(p.image)||safeUrl(p.image); if(poster)player.poster=poster; player.setAttribute('aria-label',`${p.title} 紹介動画`);detail.append(player); }
      else { const link = node('a','button','紹介動画を見る ↗'); link.href = video; link.target = '_blank'; link.rel = 'noopener noreferrer'; detail.append(link); }
    } else { const placeholder = node('div','video-placeholder'); placeholder.append(node('b','','▷'),node('span','','紹介動画は準備中です')); detail.append(placeholder); }
    const actions = node('div','detail-actions'); const url = safeUrl(p.url);
    if (url) { const link = node('a','button primary',`${p.urlLabel || '作品サイトへ'} ↗`); link.href=url; link.target='_blank'; link.rel='noopener noreferrer'; actions.append(link); }
    else actions.append(node('p','placeholder-note','作品URLは準備中です。'));
    detail.append(actions);if(p.sourceLabel && safeUrl(p.sourceUrl)){const source=node('p','detail-source');const link=node('a','',p.sourceLabel);link.href=safeUrl(p.sourceUrl);link.target='_blank';link.rel='noopener noreferrer';source.append(link);detail.append(source);}openModal(dialog);
  }
  data.projects.forEach(p => { const card = node('article','work-card'); card.dataset.category=p.category; card.hidden=p.category!=='game'; const info=node('div','work-info'); info.append(tags(p),node('h3','',p.title),node('p','',p.subtitle));const meta=node('div','card-meta');meta.append(node('span','',p.year),node('span','','OVERVIEW / IMPLEMENTATION'));info.append(meta); const button=node('button','work-open','↗'); button.type='button'; button.setAttribute('aria-label',`${p.title} の詳細を見る`); button.addEventListener('click',()=>showProject(p)); card.append(cover(p),info,button); grid.append(card); });
  const counter=document.querySelector('.section-counter');
  counter.textContent=`${data.projects.filter(p=>p.category==='game').length} PROJECTS`;
  document.querySelectorAll('.filter').forEach(button=>button.addEventListener('click',()=>{ document.querySelectorAll('.filter').forEach(b=>{ const selected = b===button; b.classList.toggle('active',selected); b.setAttribute('aria-pressed',selected); }); let count=0; grid.querySelectorAll('.work-card').forEach(card=>{ card.hidden=card.dataset.category!==button.dataset.filter; if(!card.hidden) count++; }); counter.textContent=`${count} PROJECTS`;document.querySelector('#announcement').textContent = `${count}件の作品を表示しています`;scheduleUpdate(); }));
  document.querySelectorAll('[data-name]').forEach(el=>el.textContent=data.name);
  document.querySelector('[data-role]').textContent=data.role;
  document.querySelector('#bio').textContent=data.bio;
  document.querySelector('#year').textContent=new Date().getFullYear();
  document.title=`${data.name} — Game Development Portfolio`;
  data.contacts.forEach(c=>{ const url=assetUrl(c.url)||safeUrl(c.url,true); const el=node(url?'a':'span','contact-link',c.label); if(url){el.href=url;if(!url.startsWith('mailto:')){el.target='_blank';el.rel='noopener noreferrer';}el.append(node('span','','↗'));}else{el.setAttribute('aria-disabled','true');el.append(node('small','','準備中'));}document.querySelector('#contact-links').append(el); });
  dialog.addEventListener('close',()=>{dialog.querySelectorAll('video').forEach(v=>v.pause());dialog.querySelectorAll('iframe').forEach(f=>f.remove());});
  const hud=document.querySelector('.hud');
  const levelLabel=document.querySelector('#player-level');
  const exp=document.querySelector('#exp-meter');
  const reducedMotion=matchMedia('(prefers-reduced-motion: reduce)');
  let scheduled=false;let lastLevel=1;let levelTimer;let reactionTimer;
  function updateStatus(){scheduled=false;const maxScroll=Math.max(1,document.documentElement.scrollHeight-innerHeight);const ratio=Math.min(1,Math.max(0,scrollY/maxScroll));const percent=Math.round(ratio*100);const level=Math.min(3,1+Math.floor(percent/34));exp.value=percent;document.querySelector('#exp-value').textContent=`${percent}%`;levelLabel.textContent=String(level).padStart(2,'0');if(lastLevel!==level){hud.classList.remove('level-up');void hud.offsetWidth;hud.classList.add('level-up');clearTimeout(levelTimer);levelTimer=setTimeout(()=>hud.classList.remove('level-up'),650);lastLevel=level;}const undergroundY=document.querySelector('.underground').offsetTop;const isUnderground=scrollY+document.querySelector('.hud').getBoundingClientRect().bottom+32>=undergroundY;document.querySelector('#zone-name').textContent=isUnderground?'UNDERGROUND':'SURFACE';document.querySelector('#depth-value').textContent=isUnderground?`−${String(Math.round(ratio*300)).padStart(3,'0')} m`:'000 m';if(!reducedMotion.matches)document.querySelector('.surface-scene').style.setProperty('--surface-shift',`${Math.min(scrollY*.12,75)}px`);}
  function scheduleUpdate(){if(!scheduled){scheduled=true;requestAnimationFrame(updateStatus);}}
  window.addEventListener('scroll',scheduleUpdate,{passive:true});window.addEventListener('resize',scheduleUpdate);window.addEventListener('load',scheduleUpdate);new ResizeObserver(scheduleUpdate).observe(document.querySelector('main'));scheduleUpdate();
  document.querySelectorAll('.button,.filter,.work-open,.back-top').forEach(el=>{el.addEventListener('pointerenter',()=>{hud.classList.add('reacting');clearTimeout(reactionTimer);reactionTimer=setTimeout(()=>hud.classList.remove('reacting'),250);});});
})();


