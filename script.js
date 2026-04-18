// ─── Scroll animations ───
const observer = new IntersectionObserver((entries) => {
    entries.forEach((entry, i) => {
        if (entry.isIntersecting) {
            setTimeout(() => entry.target.classList.add('visible'), entry.target.dataset.delay || 0);
        }
    });
}, { threshold: 0.12 });

document.querySelectorAll('.stat-item, .flow-step, .sensor-tile, .feature-item, .hw-item').forEach((el, i) => {
    el.dataset.delay = (i % 4) * 80;
    observer.observe(el);
});