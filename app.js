(() => {
  const menu = document.querySelector("#mobile-nav");
  const menuToggle = document.querySelector(".menu-toggle");
  const demoDialog = document.querySelector("#demo-dialog");
  const heroVideo = document.querySelector(".hero__media");
  const toast = document.querySelector(".toast");
  let toastTimer;

  const renderIcons = () => {
    if (window.lucide) {
      window.lucide.createIcons();
    }
  };

  const setMenuOpen = (open) => {
    if (!menu || !menuToggle) {
      return;
    }

    menu.hidden = !open;
    menuToggle.setAttribute("aria-expanded", String(open));
    menuToggle.setAttribute("aria-label", open ? "关闭章节导航" : "打开章节导航");
    document.body.classList.toggle("menu-open", open);
  };

  const showToast = (message) => {
    if (!toast) {
      return;
    }

    window.clearTimeout(toastTimer);
    const label = toast.querySelector("span");
    if (label) {
      label.textContent = message;
    }
    toast.hidden = false;
    toastTimer = window.setTimeout(() => {
      toast.hidden = true;
    }, 1800);
  };

  const copyText = async (text) => {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return;
    }

    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";
    document.body.append(textarea);
    textarea.select();
    document.execCommand("copy");
    textarea.remove();
  };

  menuToggle?.addEventListener("click", () => {
    setMenuOpen(menu.hidden);
  });

  menu?.querySelectorAll("[data-close-menu], a").forEach((element) => {
    element.addEventListener("click", () => setMenuOpen(false));
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && menu && !menu.hidden) {
      setMenuOpen(false);
      menuToggle?.focus();
    }
  });

  document.querySelector("[data-open-demo]")?.addEventListener("click", () => {
    if (!demoDialog) {
      return;
    }
    const video = demoDialog.querySelector("video");
    const source = video?.querySelector("source[data-src]");
    if (source && !source.src) {
      source.src = source.dataset.src;
    }
    demoDialog.showModal();
  });

  document.querySelector("[data-close-demo]")?.addEventListener("click", () => {
    demoDialog?.close();
  });

  demoDialog?.addEventListener("click", (event) => {
    if (event.target === demoDialog) {
      demoDialog.close();
    }
  });

  demoDialog?.addEventListener("close", () => {
    const video = demoDialog.querySelector("video");
    if (video) {
      video.pause();
      video.currentTime = 0;
    }
  });

  document.querySelectorAll("[data-copy]").forEach((button) => {
    button.addEventListener("click", async () => {
      try {
        await copyText(button.dataset.copy || "");
        showToast("命令已复制");
      } catch {
        showToast("复制失败，请手动选择");
      }
    });
  });

  document.querySelectorAll("[data-check]").forEach((checkbox) => {
    const key = `work-assistant-guide:${checkbox.dataset.check}`;
    try {
      checkbox.checked = window.localStorage.getItem(key) === "true";
    } catch {
      checkbox.checked = false;
    }

    checkbox.addEventListener("change", () => {
      try {
        window.localStorage.setItem(key, String(checkbox.checked));
      } catch {
        // The checklist remains usable when storage is unavailable.
      }
    });
  });

  const navigationLinks = new Map();
  document.querySelectorAll(".chapter-nav a[href^='#']").forEach((link) => {
    navigationLinks.set(link.getAttribute("href").slice(1), link);
  });

  const observedSections = Array.from(navigationLinks.keys())
    .map((id) => document.getElementById(id))
    .filter(Boolean);

  if ("IntersectionObserver" in window && observedSections.length) {
    const observer = new IntersectionObserver(
      (entries) => {
        const visible = entries
          .filter((entry) => entry.isIntersecting)
          .sort((a, b) => b.intersectionRatio - a.intersectionRatio)[0];

        if (!visible) {
          return;
        }

        navigationLinks.forEach((link, id) => {
          link.classList.toggle("is-active", id === visible.target.id);
        });
      },
      {
        rootMargin: "-18% 0px -62% 0px",
        threshold: [0, 0.15, 0.35],
      }
    );

    observedSections.forEach((section) => observer.observe(section));
  }

  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    heroVideo?.pause();
    heroVideo?.removeAttribute("autoplay");
  }

  renderIcons();
})();
