#!/usr/bin/env node
/**
 * App logo → app_icon.png (256) + app_icon.ico (multi-size)
 *
 * Source (first match wins):
 *   1. resources/icons/app_icon_source.png  ← place your new logo here
 *   2. resources/icons/app_icon.svg         ← legacy SVG workflow
 */
import { readFileSync, writeFileSync, existsSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const iconsDir = join(repoRoot, "resources", "icons");
const sourcePngPath = join(iconsDir, "app_icon_source.png");
const svgPath = join(iconsDir, "app_icon.svg");
const pngPath = join(iconsDir, "app_icon.png");
const icoPath = join(iconsDir, "app_icon.ico");

const sizes = [256, 128, 64, 48, 32, 16];
const toIco = (await import("to-ico")).default;

async function pngBuffersFromSource(sourceBuffer) {
  const sharp = (await import("sharp")).default;

  // Drop Figma export margins; letterboxing with default black caused a visible bar on Windows.
  const trimmed = await sharp(sourceBuffer).trim({ threshold: 8 }).toBuffer();
  const meta = await sharp(trimmed).metadata();
  const side = Math.max(meta.width ?? 0, meta.height ?? 0);

  const square = await sharp(trimmed)
    .extend({
      top: Math.floor((side - (meta.height ?? side)) / 2),
      bottom: Math.ceil((side - (meta.height ?? side)) / 2),
      left: Math.floor((side - (meta.width ?? side)) / 2),
      right: Math.ceil((side - (meta.width ?? side)) / 2),
      background: { r: 0, g: 0, b: 0, alpha: 0 },
    })
    .png()
    .toBuffer();

  return Promise.all(
    sizes.map((size) => sharp(square).resize(size, size, { fit: "fill" }).png().toBuffer())
  );
}

function prepareSvgForResvg(svgText) {
  const needsFix =
    svgText.includes("foreignObject") || svgText.includes("data-figma-gradient-fill");

  const pathMatch = svgText.match(
    /<path\s+d="([^"]+)"\s+fill="(#[0-9A-Fa-f]{3,8})"\s*\/?>/
  );
  if (!pathMatch) {
    if (needsFix) {
      console.warn("Could not parse logo path; using SVG as-is (gradient may be missing).");
    }
    return svgText;
  }

  const [, pathD, logoFill] = pathMatch;
  const bgMatch = svgText.match(/<rect[^>]*fill="(#[0-9A-Fa-f]{3,8})"/i);
  const bgFill = bgMatch ? bgMatch[1] : "#1E1E1E";

  if (needsFix) {
    console.log(
      "Note: Figma HTML gradient replaced with native SVG (resvg cannot render foreignObject)."
    );
  }

  return `<svg width="700" height="700" viewBox="0 0 700 700" fill="none" xmlns="http://www.w3.org/2000/svg">
<rect width="700" height="700" fill="${bgFill}"/>
<defs>
  <linearGradient id="circleGrad" gradientUnits="userSpaceOnUse" x1="350" y1="0" x2="350" y2="700">
    <stop offset="0%" stop-color="#110A28"/>
    <stop offset="100%" stop-color="#4186A9"/>
  </linearGradient>
  <mask id="logoMask" style="mask-type:luminance" maskUnits="userSpaceOnUse" x="0" y="0" width="700" height="700">
    <circle cx="350" cy="350" r="350" fill="white"/>
  </mask>
</defs>
<circle cx="350" cy="350" r="350" fill="url(#circleGrad)"/>
<g mask="url(#logoMask)">
  <path d="${pathD}" fill="${logoFill}"/>
</g>
</svg>`;
}

async function pngBuffersFromSvg() {
  if (!existsSync(svgPath)) return null;

  const { Resvg } = await import("@resvg/resvg-js");
  const raw = readFileSync(svgPath, "utf8");
  const svg = prepareSvgForResvg(raw);
  return sizes.map((size) => {
    const resvg = new Resvg(svg, {
      fitTo: { mode: "width", value: size },
      background: "#1E1E1E",
    });
    return resvg.render().asPng();
  });
}

let pngs = null;

if (existsSync(sourcePngPath)) {
  console.log("Source: app_icon_source.png");
  pngs = await pngBuffersFromSource(readFileSync(sourcePngPath));
} else {
  pngs = await pngBuffersFromSvg();
  if (pngs) console.log("Source: app_icon.svg");
}

if (!pngs) {
  console.error(
    "No icon source found. Place your logo PNG at:\n  " +
      sourcePngPath +
      "\n\nThen run:  npm run icon"
  );
  process.exit(1);
}

writeFileSync(pngPath, pngs[0]);
writeFileSync(icoPath, await toIco(pngs));

console.log("Wrote:", pngPath);
console.log("Wrote:", icoPath);
console.log("\nNext: rebuild MipsyncEngine (cmake --build build --config Release)");
