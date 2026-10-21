/**
 * In-context Looking Glass swizzle for UZDoom WASM.
 *
 * Samples the engine quilt texture (same WebGL2 context) into the default
 * framebuffer at panel resolution — so the page canvas is already swizzled
 * and callers no longer round-trip a full quilt through a parent 2D canvas.
 */
(function () {
  "use strict";

  const VS = `#version 300 es
in vec2 a_position;
out vec2 v_texcoord;
void main() {
  gl_Position = vec4(a_position * 2.0 - 1.0, 0.0, 1.0);
  v_texcoord = a_position;
}`;

  // filter_mode 0 path from holoplay-core Shader(), uniforms instead of consts.
  const FS = `#version 300 es
precision mediump float;
uniform sampler2D u_texture;
uniform float u_pitch, u_tilt, u_center, u_subp;
uniform float u_cols, u_rows;
in vec2 v_texcoord;
out vec4 color;

vec2 quiltUv(vec2 tile_uv, int viewIndex) {
  float cols = max(u_cols, 1.0);
  float rows = max(u_rows, 1.0);
  float total = cols * rows;
  float view = clamp(float(viewIndex), 0.0, total - 0.00001);
  float tx = cols - 0.00001;
  float tileX = mod(view, tx);
  float tileY = floor(view / tx);
  return vec2((tileX + tile_uv.x) / cols, (tileY + tile_uv.y) / rows);
}

void main() {
  float tileCount = max(u_cols, 1.0) * max(u_rows, 1.0);
  vec3 views;
  views.r = v_texcoord.x + u_subp * 0.0;
  views.g = v_texcoord.x + u_subp * 1.0;
  views.b = v_texcoord.x + u_subp * 2.0;
  views += vec3(v_texcoord.y * u_tilt);
  views *= vec3(u_pitch);
  views -= vec3(u_center);
  views = vec3(1.0) - fract(views);
  views = clamp(views, vec3(0.00001), vec3(0.999999));

  color = vec4(0.0, 0.0, 0.0, 1.0);
  for (int ch = 0; ch < 3; ch++) {
    int viewIndex = int(views[ch] * tileCount);
    vec2 quv = quiltUv(v_texcoord, viewIndex);
    color[ch] = texture(u_texture, quv)[ch];
  }
}`;

  let gl = null;
  let program = null;
  let vao = null;
  let uPitch, uTilt, uCenter, uSubp, uCols, uRows, uTex;
  let config = null;
  let lastFail = "";

  function compile(type, src) {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      console.warn("[lg-swizzle]", gl.getShaderInfoLog(sh));
    }
    return sh;
  }

  function ensureProgram() {
    if (program) return true;
    program = gl.createProgram();
    gl.attachShader(program, compile(gl.VERTEX_SHADER, VS));
    gl.attachShader(program, compile(gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      console.warn("[lg-swizzle] link", gl.getProgramInfoLog(program));
      program = null;
      return false;
    }
    uTex = gl.getUniformLocation(program, "u_texture");
    uPitch = gl.getUniformLocation(program, "u_pitch");
    uTilt = gl.getUniformLocation(program, "u_tilt");
    uCenter = gl.getUniformLocation(program, "u_center");
    uSubp = gl.getUniformLocation(program, "u_subp");
    uCols = gl.getUniformLocation(program, "u_cols");
    uRows = gl.getUniformLocation(program, "u_rows");

    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1]),
      gl.STATIC_DRAW
    );
    const loc = gl.getAttribLocation(program, "a_position");
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.bindVertexArray(null);
    return true;
  }

  function setConfig(next) {
    config = next && typeof next === "object" ? next : null;
    if (typeof Module !== "undefined") {
      Module.__lgSwizzleReady = Boolean(
        config && config.screenW > 0 && config.screenH > 0
      );
      if (Module.__lgSwizzleReady) {
        Module.__lgPanelW = config.screenW | 0;
        Module.__lgPanelH = config.screenH | 0;
      }
    }
  }

  function getGl() {
    if (typeof Module === "undefined") return null;
    if (Module.ctx) return Module.ctx;
    if (typeof GL !== "undefined" && GL.currentContext)
      return GL.currentContext.GLctx;
    return null;
  }

  function resolveTexture(emTexId) {
    if (typeof GL === "undefined" || !GL.textures) return null;
    if (GL.textures[emTexId]) return GL.textures[emTexId];
    const n = emTexId | 0;
    if (GL.textures[n]) return GL.textures[n];
    return null;
  }

  function fail(reason) {
    if (reason !== lastFail) {
      lastFail = reason;
      console.warn("[lg-swizzle]", reason);
      try {
        parent.postMessage(
          { type: "lg-swizzle-status", ok: false, reason: String(reason) },
          "*"
        );
      } catch (e) {
        /* ignore */
      }
    }
    return false;
  }

  /**
   * Swizzle quilt → default framebuffer (panel-sized canvas).
   * C++ should bind the quilt to TEXTURE0 before calling (fallback if table lookup fails).
   * Do not resize canvas.width here — that destroys the WebGL context.
   */
  function present(emTexId, quiltW, quiltH, cols, rows) {
    gl = getGl();
    if (!gl) return fail("no GL context");
    if (!config) return fail("no lkg-swizzle-config yet");
    if (!(cols >= 1 && rows >= 1)) return fail("bad cols/rows " + cols + "x" + rows);

    const panelW = Math.max(1, config.screenW | 0);
    const panelH = Math.max(1, config.screenH | 0);
    Module.__lgPanelW = panelW;
    Module.__lgPanelH = panelH;
    Module.__lgQuiltW = quiltW | 0;
    Module.__lgQuiltH = quiltH | 0;
    Module.__lgSwizzleReady = true;

    const dbw = gl.drawingBufferWidth | 0;
    const dbh = gl.drawingBufferHeight | 0;
    if (dbw < 64 || dbh < 64 || (dbw <= 300 && dbh <= 150)) {
      return fail("drawable still " + dbw + "x" + dbh + " (need panel before GL)");
    }

    if (!ensureProgram()) return fail("shader compile/link failed");

    const tex = resolveTexture(emTexId);

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, dbw, dbh);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);

    gl.useProgram(program);
    gl.activeTexture(gl.TEXTURE0);
    if (tex) gl.bindTexture(gl.TEXTURE_2D, tex);
    // If table lookup failed, rely on C++ having bound the quilt already.
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.uniform1i(uTex, 0);
    gl.uniform1f(uPitch, Number(config.pitch) || 45);
    gl.uniform1f(uTilt, Number(config.tilt) || -5);
    gl.uniform1f(uCenter, Number(config.center) || -0.5);
    gl.uniform1f(uSubp, Number(config.subp) || 0.0001);
    gl.uniform1f(uCols, cols);
    gl.uniform1f(uRows, rows);

    gl.bindVertexArray(vao);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    gl.bindVertexArray(null);
    if (lastFail || !Module.__lgSwizzleOkLogged) {
      Module.__lgSwizzleOkLogged = 1;
      lastFail = "";
      try {
        parent.postMessage(
          {
            type: "lg-swizzle-status",
            ok: true,
            reason:
              "swizzled " +
              cols +
              "x" +
              rows +
              " → " +
              dbw +
              "x" +
              dbh +
              (tex ? "" : " (bound tex)"),
          },
          "*"
        );
      } catch (e) {
        /* ignore */
      }
    }
    lastFail = "";
    setPresentMode("swizzled");
    return true;
  }

  function setPresentMode(mode) {
    if (mode !== "flat" && mode !== "swizzled") return;
    if (typeof Module !== "undefined") Module.__lgPresentMode = mode;
    if (mode === lastPresentMode) return;
    lastPresentMode = mode;
    try {
      parent.postMessage({ type: "lg-present-mode", mode }, "*");
    } catch (e) {
      /* ignore */
    }
  }

  function onMessage(ev) {
    const d = ev.data;
    if (!d || d.type !== "lkg-swizzle-config") return;
    setConfig(d.config || null);
  }

  /** Size the drawable to the LG panel before WebGL is created (never after). */
  function sizeCanvasFromUrl() {
    try {
      const params = new URLSearchParams(location.search);
      const w = parseInt(params.get("lgsw") || "", 10);
      const h = parseInt(params.get("lgsh") || "", 10);
      if (!(w >= 64 && h >= 64)) return;
      const c =
        (typeof Module !== "undefined" && Module.canvas) ||
        document.getElementById("canvas");
      if (!c) return;
      if (c.width !== w || c.height !== h) {
        c.width = w;
        c.height = h;
      }
      if (typeof Module !== "undefined") {
        Module.__lgPanelW = w;
        Module.__lgPanelH = h;
      }
    } catch (e) {
      /* ignore */
    }
  }

  let lastPresentMode = "";
  window.addEventListener("message", onMessage);
  window.Module = window.Module || {};
  Module.lgSwizzle = { setConfig, present, setPresentMode };
  sizeCanvasFromUrl();
})();
