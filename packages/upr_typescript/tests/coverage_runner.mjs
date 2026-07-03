// Collects TypeScript line coverage under Bazel using monocart-coverage-reports
// (MCR). The compiled dist/*.js are executed under Node's V8 coverage and MCR
// remaps the results back onto the original src/*.ts via the (source-content
// carrying) source maps. Emits an lcov report scoped to the library sources.
//
// Usage: node coverage_runner.mjs <output-lcov-path>
import { spawnSync } from "node:child_process";
import { mkdtempSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { CoverageReport } from "monocart-coverage-reports";

// Workspace-relative package prefix so the emitted lcov paths line up with the
// C++/Python reports in the aggregated coverage gate.
const PACKAGE_PREFIX = "packages/upr_typescript";

const here = import.meta.dirname;
const testFiles = readdirSync(here)
  .filter((f) => f.endsWith(".test.ts"))
  .sort()
  .map((f) => path.join(here, f));

const outFile = path.resolve(process.argv[2] ?? "coverage/lcov.info");
const outputDir = path.dirname(outFile);
const fileName = path.basename(outFile);

const covDir = mkdtempSync(path.join(tmpdir(), "upr-v8cov-"));

const run = spawnSync(
  process.execPath,
  ["--experimental-strip-types", "--test", ...testFiles],
  { stdio: "inherit", env: { ...process.env, NODE_V8_COVERAGE: covDir } },
);
if (run.status !== 0) {
  process.exit(run.status ?? 1);
}

const isLibSource = (sourcePath) =>
  /(^|\/)src\//.test(sourcePath) && sourcePath.endsWith(".ts");

const report = new CoverageReport({
  name: "universal-protocol-runtime (TypeScript)",
  outputDir,
  reports: [["lcovonly", { file: fileName }]],
  // Only remap the compiled runtime; ignore the test entry points and any
  // Node-internal scripts.
  entryFilter: (entry) => entry.url.includes("/dist/"),
  // Report against the original TypeScript sources only.
  sourceFilter: isLibSource,
  // Normalize remapped paths to be workspace-relative regardless of the build
  // action's working directory.
  sourcePath: (filePath) => {
    const idx = filePath.lastIndexOf("src/");
    const rel = idx === -1 ? filePath : filePath.slice(idx);
    return `${PACKAGE_PREFIX}/${rel}`;
  },
  cleanCache: true,
});

await report.addFromDir(covDir);
const results = await report.generate();

if (!results || !results.summary || results.summary.lines.total === 0) {
  console.error("monocart collected no TypeScript source coverage");
  process.exit(1);
}
