"use strict";

const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const PAGE = path.join(__dirname, "..", "src", "dashboard_page.cpp");
const OPEN = 'R"HTML(';
const CLOSE = ')HTML"';
const EXPORTS = "return { Player, ERROR_TEXT };";

let failures = 0;
const check = (name, ok, detail) => {
  console.log(
    (ok ? "PASS " : "FAIL ") + name + (!ok && detail ? ": " + detail : ""),
  );
  if (!ok) failures++;
};

const text = fs.readFileSync(PAGE, "utf8").replace(/\r\n/g, "\n");
const literalStart = text.indexOf(OPEN) + OPEN.length;
const literalEnd = text.lastIndexOf(CLOSE);
check(
  "served literal found",
  text.indexOf(OPEN) >= 0 && literalEnd > literalStart,
);
const literal = text.slice(literalStart, literalEnd);
check("no " + CLOSE + " inside the served literal", !literal.includes(CLOSE));
console.log("served literal: " + Buffer.byteLength(literal, "utf8") + " bytes");

const scripts = [...literal.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(
  (match) => match[1],
);
check("dashboard has a script", scripts.length > 0);
scripts.forEach((script, index) => {
  const result = spawnSync(process.execPath, ["--check"], {
    input: script,
    encoding: "utf8",
  });
  check(
    "script " + (index + 1) + " passes node --check",
    result.status === 0,
    (result.stderr || String(result.error))
      .trim()
      .split("\n")
      .slice(0, 5)
      .join(" | "),
  );
});

const playerStart = literal.indexOf("const LivePlayer = (() => {");
const playerEnd = literal.indexOf("\n})();", playerStart) + "\n})();".length;
const player = playerStart >= 0 ? literal.slice(playerStart, playerEnd) : "";
check("LivePlayer found", player.includes(EXPORTS));
if (!player.includes(EXPORTS)) process.exit(1);
const { parseSps, lowDelaySps } = new Function(
  player
    .replace("const LivePlayer = ", "return ")
    .replace(EXPORTS, "return { parseSps, lowDelaySps };"),
)();

const hex = (value) =>
  Uint8Array.from(value.match(/../g), (byte) => parseInt(byte, 16));
const rewrite = (value) =>
  Buffer.from(lowDelaySps(hex(value), parseSps(hex(value)))).toString("hex");

const cases = [
  [
    "camera standard",
    "274d00339da8320e6840000003004000000621",
    "274d00339da8320e6840000003004000000623682211a8",
  ],
  [
    "camera high",
    "274d00339da8090028e840000003004000000621",
    "274d00339da8090028e840000003004000000623682211a8",
  ],
  [
    "already declared stays",
    "274d00339da8320e6840000003004000000623682211a8",
    "274d00339da8320e6840000003004000000623682211a8",
  ],
  ["vui absent", "274204349d9c0a07e4", "274204349d9c0a07e806d044233c"],
  [
    "cropping",
    "272c642a3e508c600c815769a802",
    "272c642a3e508c600c815769a806d0442319",
  ],
  [
    "aspect 255 and signal type",
    "275638330a10520de07f03c37ff302cab72aa90a",
    "275638330a10520de07f03c37ff302cab72aa90ed0442350",
  ],
  [
    "vcl hrd only",
    "272c88150a34506330603e6a0c3000076490000a4d5f033988",
    "272c88150a34506330603e6a0c3000076490000a4d5f03399b41108cd0",
  ],
  [
    "nal hrd",
    "2764a02acd0cc7804701a9da212853c8000f2030002ec6c001c840000dea98bd3e24",
    "2764a02acd0cc7804701a9da212853c8000f2030002ec6c001c840000dea98bd3e2da088463e",
  ],
  [
    "poc type other than 2 stays",
    "27584032090b8882a1a240",
    "27584032090b8882a1a240",
  ],
  [
    "baseline poc 0 vui absent",
    "2742c01eed01407b20",
    "2742c01eed01407b403682211a80",
  ],
  [
    "baseline poc 0 with vui",
    "2742e028965603c0113f2e02d40404050000030001000003003284",
    "2742e028965603c0113f2e02d4040405000003000100000300328da08846e0",
  ],
  [
    "baseline poc 1 vui absent",
    "27428015a42c8466202c12c8",
    "27428015a42c8466202c12d00da0884648",
  ],
  [
    "baseline poc 1 with vui and hrd",
    "2742c01fd7a014016e8400000fa40003a98189000bb9000bb8af7be0a0",
    "2742c01fd7a014016e8400000fa40003a98189000bb9000bb8af7be0ed044235",
  ],
  [
    "baseline already declared stays",
    "2742c01eed01407b403682211a80",
    "2742c01eed01407b403682211a80",
  ],
  [
    "declared other values stays",
    "274d401fda0280f684000003000400000300c83682211458",
    "274d401fda0280f684000003000400000300c83682211458",
  ],
  ["main poc 0 stays", "274d401fed00a00b72", "274d401fed00a00b72"],
  [
    "escape in rewritten tail",
    "274d401fda014016e840000444400100000301",
    "274d401fda014016e840000444400100000303682211a8",
  ],
];

for (const [name, input, expected] of cases) {
  let output;
  try {
    output = rewrite(input);
  } catch (error) {
    output = "error " + error.message;
  }
  check(name, output === expected, "got " + output + ", expected " + expected);
}

const size = (value) => {
  const info = parseSps(hex(value));
  return info.width + "x" + info.height;
};
check(
  "camera standard size",
  size(cases[0][1]) === "800x448",
  "got " + size(cases[0][1]),
);
check(
  "camera high size",
  size(cases[1][1]) === "2304x1296",
  "got " + size(cases[1][1]),
);

let truncated = "no error";
try {
  rewrite("274d00339da8320e68400000030040");
} catch (error) {
  truncated = error.message;
}
check(
  "truncated sps throws",
  truncated === "sps_truncated",
  "got " + truncated,
);

let trailing = "no error";
try {
  rewrite("274d00339da8320e684000000300400000062180");
} catch (error) {
  trailing = error.message;
}
check(
  "bits after stop bit throw",
  trailing === "sps_invalid",
  "got " + trailing,
);

console.log(failures ? failures + " check(s) failed" : "all checks passed");
process.exit(failures ? 1 : 0);
