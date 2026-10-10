/*
 * FlamingoIM 使用教程的页面脚本：根据 toc.js 生成顶栏、左侧章节导航、本页目录、上一章 / 下一章，
 * 提供站内搜索（数据在 search-index.js，由 tools/build_index.py 生成）、点击配图看大图与深浅色切换。
 * 每个章节页面只写 <article class="doc">，其余部分都由本脚本补上；body 的 data-root 是到教程根目录的相对路径，
 * data-chapter 是本章在 toc.js 里的 id。全部是本地文件，直接双击 index.html 打开即可。
 * balloonwj@qq.com   2026-10-09
 */
(function () {
  'use strict';

  var body = document.body;
  var root = body.getAttribute('data-root') || '';
  var current = body.getAttribute('data-chapter') || '';
  var toc = window.HELP_TOC || [];

  // 记住的深浅色设置（访问不到本地存储时忽略）
  try {
    var savedTheme = window.localStorage.getItem('help-theme');
    if (savedTheme === 'light' || savedTheme === 'dark') {
      document.documentElement.setAttribute('data-theme', savedTheme);
    }
  } catch (e) { /* 隐私模式等情况下本地存储不可用 */ }

  function el(tag, attrs, html) {
    var node = document.createElement(tag);
    if (attrs) {
      for (var k in attrs) {
        if (Object.prototype.hasOwnProperty.call(attrs, k)) node.setAttribute(k, attrs[k]);
      }
    }
    if (html !== undefined) node.innerHTML = html;
    return node;
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  // 全部章节按顺序展开，带上所属部分，供导航、上一章 / 下一章与搜索使用
  var flat = [];
  toc.forEach(function (part) {
    part.chapters.forEach(function (ch, i) {
      flat.push({ part: part, ch: ch, num: i + 1 });
    });
  });

  // ---------- 顶栏 ----------
  var topbar = el('header', { 'class': 'topbar' });
  topbar.innerHTML =
    '<button class="menu-btn" type="button" aria-label="打开目录">☰</button>' +
    '<a class="brand" href="' + root + 'index.html"><span class="brand-logo">F</span>' +
    '<span class="brand-text">FlamingoIM 使用教程<small>员工 · 企业管理员 · 平台超管</small></span></a>' +
    '<div class="search"><input type="search" placeholder="搜索教程，例如：撤回、群公告、锁屏" aria-label="搜索教程">' +
    '<div class="search-results"></div></div>' +
    '<button class="theme-btn" type="button" title="切换深色 / 浅色">◐</button>';
  body.insertBefore(topbar, body.firstChild);

  // ---------- 左侧导航 ----------
  var sidebar = el('nav', { 'class': 'sidebar', 'aria-label': '章节目录' });
  toc.forEach(function (part) {
    var box = el('div', { 'class': 'nav-part' });
    var head = el('button', { 'class': 'nav-part-title', type: 'button' },
      escapeHtml(part.title) + '<span class="chev">▾</span>');
    var list = el('ol');
    var hasCurrent = false;
    part.chapters.forEach(function (ch, i) {
      var li = el('li');
      var a = el('a', { href: root + ch.file },
        '<span class="num">' + (i + 1) + '</span><span>' + escapeHtml(ch.title) + '</span>');
      if (ch.id === current) {
        a.className = 'current';
        hasCurrent = true;
      }
      li.appendChild(a);
      list.appendChild(li);
    });
    // 首页三部分都展开；章节页只展开当前所在的部分
    if (current && !hasCurrent) box.className += ' collapsed';
    head.addEventListener('click', function () { box.classList.toggle('collapsed'); });
    box.appendChild(head);
    box.appendChild(list);
    sidebar.appendChild(box);
  });
  body.insertBefore(sidebar, topbar.nextSibling);

  // ---------- 正文外框：把 article 放进布局里 ----------
  var article = document.querySelector('article.doc');
  var layout = el('div', { 'class': 'layout' });
  var wrap = el('div', { 'class': 'content-wrap' });
  layout.appendChild(wrap);
  body.insertBefore(layout, sidebar.nextSibling);
  if (article) wrap.appendChild(article);

  // 首页：生成三个部分的章节卡片
  var partsHost = document.getElementById('parts');
  if (partsHost) {
    toc.forEach(function (part) {
      var card = el('section', { 'class': 'part-card', id: part.id });
      var grid = part.chapters.map(function (ch, i) {
        return '<a href="' + root + ch.file + '"><span class="num">' + (i + 1) + '</span>' + escapeHtml(ch.title) + '</a>';
      }).join('');
      card.innerHTML = '<h2>' + escapeHtml(part.title) + '</h2><p>' + escapeHtml(part.desc) + '</p>' +
        '<div class="chapter-grid">' + grid + '</div>';
      partsHost.appendChild(card);
    });
  }

  // ---------- 章节页：面包屑、标题序号、上一章 / 下一章、本页目录 ----------
  var idx = -1;
  for (var n = 0; n < flat.length; n++) {
    if (flat[n].ch.id === current) { idx = n; break; }
  }
  if (article && idx >= 0) {
    var me = flat[idx];
    document.title = me.ch.title + ' · FlamingoIM 使用教程';
    var h1 = article.querySelector('h1');
    if (h1 && !h1.querySelector('.ch')) {
      h1.insertAdjacentHTML('afterbegin', '<span class="ch">第 ' + me.num + ' 章</span>');
    }
    if (!article.querySelector('.crumbs')) {
      article.insertAdjacentHTML('afterbegin', '<div class="crumbs"><a href="' + root + 'index.html">首页</a> › ' +
        escapeHtml(me.part.title) + '</div>');
    }
    var pager = el('nav', { 'class': 'pager' });
    var prev = flat[idx - 1];
    var next = flat[idx + 1];
    pager.innerHTML =
      (prev ? '<a class="prev" href="' + root + prev.ch.file + '"><small>上一章 · ' + escapeHtml(prev.part.title) +
        '</small>‹ ' + escapeHtml(prev.ch.title) + '</a>' : '<span class="spacer"></span>') +
      (next ? '<a class="next" href="' + root + next.ch.file + '"><small>下一章 · ' + escapeHtml(next.part.title) +
        '</small>' + escapeHtml(next.ch.title) + ' ›</a>' : '<span class="spacer"></span>');
    article.appendChild(pager);
  }

  if (article) {
    var heads = article.querySelectorAll('h2');
    if (heads.length > 1) {
      var side = el('aside', { 'class': 'page-toc' }, '<div class="t">本页内容</div>');
      Array.prototype.forEach.call(heads, function (h, i) {
        if (!h.id) h.id = 's' + (i + 1);
        side.appendChild(el('a', { href: '#' + h.id }, escapeHtml(h.textContent)));
      });
      wrap.appendChild(side);
      var links = side.querySelectorAll('a');
      var onScroll = function () {
        var active = 0;
        Array.prototype.forEach.call(heads, function (h, i) {
          if (h.getBoundingClientRect().top < 120) active = i;
        });
        Array.prototype.forEach.call(links, function (a, i) { a.classList.toggle('active', i === active); });
      };
      window.addEventListener('scroll', onScroll, { passive: true });
      onScroll();
    }
  }

  // ---------- 看大图 ----------
  var box = el('div', { 'class': 'lightbox' }, '<img alt=""><div class="cap"></div>');
  body.appendChild(box);
  document.addEventListener('click', function (ev) {
    var img = ev.target;
    if (img.tagName === 'IMG' && img.closest('figure.shot')) {
      box.querySelector('img').src = img.src;
      var cap = img.closest('figure').querySelector('figcaption');
      box.querySelector('.cap').textContent = cap ? cap.textContent : '';
      box.classList.add('open');
    } else if (box.classList.contains('open') && box.contains(img)) {
      box.classList.remove('open');
    }
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape') {
      box.classList.remove('open');
      body.classList.remove('nav-open');
    }
  });

  // ---------- 窄屏目录按钮、深浅色切换 ----------
  topbar.querySelector('.menu-btn').addEventListener('click', function () { body.classList.toggle('nav-open'); });
  topbar.querySelector('.theme-btn').addEventListener('click', function () {
    var dark = window.getComputedStyle(document.documentElement).colorScheme === 'dark';
    var theme = dark ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', theme);
    try { window.localStorage.setItem('help-theme', theme); } catch (e) { /* 本地存储不可用时只在本页生效 */ }
  });

  // ---------- 站内搜索 ----------
  var input = topbar.querySelector('.search input');
  var results = topbar.querySelector('.search-results');
  var index = window.HELP_INDEX || [];
  var chapterById = {};
  flat.forEach(function (f) { chapterById[f.ch.id] = f; });

  function highlight(text, terms) {
    var out = escapeHtml(text);
    terms.forEach(function (t) {
      if (!t) return;
      var re = new RegExp(escapeHtml(t).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
      out = out.replace(re, function (m) { return '<mark>' + m + '</mark>'; });
    });
    return out;
  }

  function snippet(text, term) {
    var i = text.toLowerCase().indexOf(term);
    if (i < 0) return text.slice(0, 70);
    var start = Math.max(0, i - 24);
    return (start > 0 ? '…' : '') + text.slice(start, i + 46) + '…';
  }

  function search(q) {
    var terms = q.toLowerCase().split(/\s+/).filter(Boolean);
    if (!terms.length) return [];
    var hits = [];
    index.forEach(function (page) {
      var info = chapterById[page.c];
      if (!info) return;
      page.s.forEach(function (sec) {
        var heading = sec[0], anchor = sec[1], text = sec[2];
        var hay = (page.t + ' ' + heading + ' ' + text).toLowerCase();
        var ok = terms.every(function (t) { return hay.indexOf(t) >= 0; });
        if (!ok) return;
        var score = 0;
        terms.forEach(function (t) {
          if (page.t.toLowerCase().indexOf(t) >= 0) score += 10;
          if (heading.toLowerCase().indexOf(t) >= 0) score += 6;
          if (text.toLowerCase().indexOf(t) >= 0) score += 1;
        });
        hits.push({ score: score, info: info, heading: heading, anchor: anchor, text: text });
      });
    });
    hits.sort(function (a, b) { return b.score - a.score; });
    return hits.slice(0, 24).map(function (h) { h.terms = terms; return h; });
  }

  var activeIdx = -1;
  function render() {
    var q = input.value.trim();
    activeIdx = -1;
    if (!q) { results.classList.remove('open'); results.innerHTML = ''; return; }
    var hits = search(q);
    if (!hits.length) {
      results.innerHTML = '<div class="r-empty">没有找到与「' + escapeHtml(q) + '」有关的内容，换个说法试试。</div>';
    } else {
      results.innerHTML = hits.map(function (h) {
        return '<a href="' + root + h.info.ch.file + (h.anchor ? '#' + h.anchor : '') + '">' +
          '<div class="r-title">' + highlight(h.heading || h.info.ch.title, h.terms) + '</div>' +
          '<div class="r-path">' + escapeHtml(h.info.part.title + ' › ' + h.info.ch.title) + '</div>' +
          '<div class="r-snip">' + highlight(snippet(h.text, h.terms[0]), h.terms) + '</div></a>';
      }).join('');
    }
    results.classList.add('open');
  }
  input.addEventListener('input', render);
  input.addEventListener('focus', render);
  input.addEventListener('keydown', function (ev) {
    var items = results.querySelectorAll('a');
    if (ev.key === 'ArrowDown' || ev.key === 'ArrowUp') {
      if (!items.length) return;
      ev.preventDefault();
      activeIdx = (activeIdx + (ev.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length;
      Array.prototype.forEach.call(items, function (a, i) { a.classList.toggle('active', i === activeIdx); });
      items[activeIdx].scrollIntoView({ block: 'nearest' });
    } else if (ev.key === 'Enter') {
      var target = items[activeIdx >= 0 ? activeIdx : 0];
      if (target) window.location.href = target.getAttribute('href');
    }
  });
  document.addEventListener('click', function (ev) {
    if (!topbar.querySelector('.search').contains(ev.target)) results.classList.remove('open');
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key === '/' && document.activeElement !== input) {
      ev.preventDefault();
      input.focus();
    }
  });
})();
