// Render each mockup in mockups.html to docs/ux/NN-*.png at 2x.
//   node docs/ux/render.js
const { chromium } = require('playwright');
const path = require('path');

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ deviceScaleFactor: 2, viewport: { width: 1200, height: 900 } });
  await page.goto('file://' + path.join(__dirname, 'mockups.html'));
  await page.evaluate(() => document.fonts.ready);
  const names = ['home', 'search', 'list', 'menu', 'confirm', 'settings', 'player', 'keys', 'panel', 'all-apps'];
  for (let i = 0; i < names.length; i++) {
    const id = 's' + String(i + 1).padStart(2, '0');
    await page.locator('#' + id).screenshot({ path: path.join(__dirname, `${id.slice(1)}-${names[i]}.png`) });
  }
  await page.screenshot({ path: path.join(__dirname, 'overview.png'), fullPage: true });
  await browser.close();
})();
