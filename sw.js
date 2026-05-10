const CACHE_NAME = 'smatu-v1';
const assets = [
  './',
  './index.html',
  'https://unpkg.com/mqtt/dist/mqtt.min.js',
  'https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600&display=swap'
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE_NAME).then(cache => cache.addAll(assets)));
});

self.addEventListener('fetch', e => {
  e.respondWith(caches.match(e.request).then(res => res || fetch(e.request)));
});
