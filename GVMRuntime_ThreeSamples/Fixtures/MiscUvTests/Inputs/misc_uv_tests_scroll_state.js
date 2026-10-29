const sectionIndex = Number(globalThis.__GVM_MISC_UV_SECTION_INDEX__ || 0);
const headings = document.querySelectorAll('h3');
if (sectionIndex < 0 || sectionIndex >= headings.length) {
  throw new Error(`Invalid misc UV section index ${sectionIndex}.`);
}
window.scrollTo(0, headings[sectionIndex].offsetTop);
