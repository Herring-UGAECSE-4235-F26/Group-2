// PARMCO project site - AI-generated (Claude).
// 1. Condenses the masthead as the page scrolls (sets --t from 0 to 1).
// 2. Turns the propeller clockwise from 0° at the top of the page to 360° at
//    the bottom, in 1.8° steps (one full step of the Nema 17 per increment).
(function () {
  const root = document.documentElement;
  const STEP_DEG = 1.8;          // full-step angle of the motor
  const STEPS_PER_REV = 200;
  let ticking = false;

  // Distance (px) over which the masthead condenses: the difference between
  // its expanded and condensed heights, so the title shrinks as you scroll it away.
  function shrinkDistance() {
    const styles = getComputedStyle(root);
    const probe = document.createElement("div");
    probe.style.cssText = "position:absolute;visibility:hidden;height:var(--head-max)";
    document.body.appendChild(probe);
    const max = probe.getBoundingClientRect().height;
    probe.style.height = "var(--head-min)";
    const min = probe.getBoundingClientRect().height;
    probe.remove();
    return Math.max(1, max - min);
  }

  let distance = 1;

  function update() {
    ticking = false;
    const y = window.scrollY;
    const t = Math.min(1, Math.max(0, y / distance));
    root.style.setProperty("--t", t.toFixed(4));

    const scrollable = document.documentElement.scrollHeight - window.innerHeight;
    const progress = scrollable > 0 ? Math.min(1, Math.max(0, y / scrollable)) : 0;
    const steps = Math.round(progress * STEPS_PER_REV);
    root.style.setProperty("--angle", (steps * STEP_DEG).toFixed(1) + "deg");
  }

  function request() {
    if (!ticking) {
      ticking = true;
      requestAnimationFrame(update);
    }
  }

  function measure() {
    distance = shrinkDistance();
    update();
  }

  window.addEventListener("scroll", request, { passive: true });
  window.addEventListener("resize", measure);
  document.addEventListener("DOMContentLoaded", measure);
  if (document.readyState !== "loading") measure();
})();
