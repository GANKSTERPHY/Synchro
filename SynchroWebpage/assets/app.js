// ===== Mobile menu toggle =====
const burger = document.querySelector('.hamburger');
const nav = document.querySelector('.nav');
burger?.addEventListener('click', () => {
  const expanded = burger.getAttribute('aria-expanded') === 'true';
  burger.setAttribute('aria-expanded', String(!expanded));
  nav.classList.toggle('open');
});

// ===== Smooth scroll with header offset =====
const header = document.querySelector('.site-header');
const links = document.querySelectorAll('.nav-link');

function smoothScrollTo(id) {
  const el = document.getElementById(id);
  if (!el) return;
  const headerH = header?.offsetHeight ?? 0;
  const y = el.getBoundingClientRect().top + window.pageYOffset - (headerH + 10);
  window.scrollTo({ top: y, behavior: 'smooth' });
}

links.forEach(a => {
  a.addEventListener('click', (e) => {
    e.preventDefault();
    const id = a.dataset.target;
    smoothScrollTo(id);
    nav.classList.remove('open');
    burger.setAttribute('aria-expanded', 'false');
  });
});

// ===== Scroll Spy (highlight active link) =====
const sections = [...document.querySelectorAll('section[id]')];
function setActive(id) {
  links.forEach(l => l.classList.toggle('active', l.dataset.target === id));
}

let ticking = false;
window.addEventListener('scroll', () => {
  if (ticking) return;
  window.requestAnimationFrame(() => {
    const headerH = header?.offsetHeight ?? 0;
    const pos = window.scrollY + headerH + 32;
    let current = 'home';
    sections.forEach(sec => { if (pos >= sec.offsetTop) current = sec.id; });
    setActive(current);
    ticking = false;
  });
  ticking = true;
});

// ===== Offset when opening with a hash =====
window.addEventListener('load', () => {
  const id = window.location.hash?.replace('#','');
  if (id) setTimeout(()=> smoothScrollTo(id), 60);
});

// Demo: play/pause toggle
document.addEventListener('click', (e) => {
  const btn = e.target.closest('.btn-play');
  if (!btn) return;
  const isPlaying = btn.classList.toggle('playing');
  btn.textContent = isPlaying ? '❚❚' : '▶';
});
