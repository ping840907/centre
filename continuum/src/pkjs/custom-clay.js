module.exports = function(minified) {
  var clayConfig = this;
  var TMAX = 65536;

  // ---------------------------------------------------------------------------
  // Animation settings coupling
  // ---------------------------------------------------------------------------
  function toggleAnimationSettings() {
    var inertiaItem = clayConfig.getItemByMessageKey('INERTIA_TOGGLE');
    var fpsItem = clayConfig.getItemByMessageKey('ANIM_FPS');
    var enabled = this.get();
    if (inertiaItem) { enabled ? inertiaItem.show() : inertiaItem.hide(); }
    if (fpsItem) { enabled ? fpsItem.show() : fpsItem.hide(); }
  }

  // ---------------------------------------------------------------------------
  // Color utilities
  // ---------------------------------------------------------------------------
  function toCSS(val) {
    var n = (typeof val === 'string') ? parseInt(val.replace('0x', ''), 16) : parseInt(val);
    if (isNaN(n) || n < 0) n = 0;
    return '#' + ('000000' + (n & 0xFFFFFF).toString(16)).slice(-6);
  }

  var COLOR_KEYS = [
    'BACKGROUND_COLOR', 'INNER_RING_COLOR', 'SUB_INNER_RING_COLOR',
    'MIDDLE_RING_COLOR', 'OUTER_RING_COLOR', 'HIGHLIGHT_FILL_COLOR',
    'LINE_COLOR', 'NUMBER_COLOR', 'CENTER_TEXT_COLOR', 'HIGHLIGHT_NUMBER_COLOR'
  ];

  var COLOR_DEFAULTS = {
    BACKGROUND_COLOR: '#000000', INNER_RING_COLOR: '#000000',
    SUB_INNER_RING_COLOR: '#000000', MIDDLE_RING_COLOR: '#000000',
    OUTER_RING_COLOR: '#000000', HIGHLIGHT_FILL_COLOR: '#FF0000',
    LINE_COLOR: '#555555', NUMBER_COLOR: '#AAAAAA',
    CENTER_TEXT_COLOR: '#FFFFFF', HIGHLIGHT_NUMBER_COLOR: '#FFFFFF'
  };

  function getColors() {
    var colors = {};
    COLOR_KEYS.forEach(function(key) {
      var item = clayConfig.getItemByMessageKey(key);
      colors[key] = item ? toCSS(item.get()) : COLOR_DEFAULTS[key];
    });
    return colors;
  }

  // ---------------------------------------------------------------------------
  // Geometry: point on perimeter of a rounded rectangle
  // Mirrors get_point_on_rounded_rect() in main.c exactly.
  // angle: 0..TMAX  (0 = top-center, clockwise)
  // Returns {x, y} relative to the rectangle center.
  // ---------------------------------------------------------------------------
  function ptOnRect(w, h, cr, angle) {
    angle = ((angle % TMAX) + TMAX) % TMAX;
    var sH = w - 2 * cr, sV = h - 2 * cr;
    var qa = Math.PI / 2 * cr;                    // quarter-arc length
    var td = angle / TMAX * (2 * sH + 2 * sV + 4 * qa);
    var cd = 0;

    function s(a) { return Math.sin(a * 2 * Math.PI / TMAX) * cr; }
    function c(a) { return Math.cos(a * 2 * Math.PI / TMAX) * cr; }

    // 1. top-right straight half
    if (td <= cd + sH / 2) { return { x: td - cd, y: -h / 2 }; }
    cd += sH / 2;
    // 2. top-right corner
    if (td <= cd + qa) { var aa = (td-cd)/qa*TMAX/4; return { x: w/2-cr+s(aa), y: -h/2+cr-c(aa) }; }
    cd += qa;
    // 3. right edge
    if (td <= cd + sV) { return { x: w / 2, y: -h / 2 + cr + (td - cd) }; }
    cd += sV;
    // 4. bottom-right corner
    if (td <= cd + qa) { var aa = TMAX/4+(td-cd)/qa*TMAX/4; return { x: w/2-cr+s(aa), y: h/2-cr-c(aa) }; }
    cd += qa;
    // 5. bottom edge
    if (td <= cd + sH) { return { x: w/2-cr-(td-cd), y: h / 2 }; }
    cd += sH;
    // 6. bottom-left corner
    if (td <= cd + qa) { var aa = TMAX/2+(td-cd)/qa*TMAX/4; return { x: -w/2+cr+s(aa), y: h/2-cr-c(aa) }; }
    cd += qa;
    // 7. left edge
    if (td <= cd + sV) { return { x: -w / 2, y: h / 2 - cr - (td - cd) }; }
    cd += sV;
    // 8. top-left corner
    if (td <= cd + qa) { var aa = 3*TMAX/4+(td-cd)/qa*TMAX/4; return { x: -w/2+cr+s(aa), y: -h/2+cr-c(aa) }; }
    cd += qa;
    // 9. top-left straight half
    return { x: -w/2 + cr + (td - cd), y: -h / 2 };
  }

  // ---------------------------------------------------------------------------
  // Draw a rounded-rectangle path (arcTo, widely supported)
  // ---------------------------------------------------------------------------
  function rrPath(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.arcTo(x + w, y,     x + w, y + r,     r);
    ctx.lineTo(x + w, y + h - r);
    ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
    ctx.lineTo(x + r, y + h);
    ctx.arcTo(x,     y + h, x,     y + h - r,  r);
    ctx.lineTo(x, y + r);
    ctx.arcTo(x,     y,     x + r, y,           r);
    ctx.closePath();
  }

  // ---------------------------------------------------------------------------
  // Draw the watch face preview
  // Mirrors the drawing order in bg_update_proc() + ring_update_proc():
  //   1. background fill
  //   2. rings (outer → inner)
  //   3. highlight boxes (one per ring at highlight angle)
  //   4. digits (ring by ring, cur digit in highlight colour)
  //   5. centre text (weekday / month / day / battery)
  // ---------------------------------------------------------------------------
  function drawPreview(canvas) {
    var ctx = canvas.getContext('2d');
    var CW = canvas.width, CH = canvas.height;
    var CX = CW / 2, CY = CH / 2;
    var c = getColors();
    // Scale relative to standard Pebble Time (144 × 168)
    var S = CW / 144;

    // Ring definitions – outer first (drawing order matches C loop i=3..0).
    // cur: sample digit placed at highlight position (mimics time 15:48).
    var RINGS = [
      { w:130, h:154, r:18, n:10, color:c.OUTER_RING_COLOR,     cur:8, digs:'0123456789' },
      { w:102, h:126, r:15, n:6,  color:c.MIDDLE_RING_COLOR,    cur:4, digs:'012345'     },
      { w:74,  h:98,  r:11, n:10, color:c.SUB_INNER_RING_COLOR, cur:5, digs:'0123456789' },
      { w:46,  h:70,  r:7,  n:3,  color:c.INNER_RING_COLOR,     cur:1, digs:'012'        }
    ];

    var TA = TMAX / 4;  // POS_RIGHT = TRIG_MAX_ANGLE/4
    var HL = 15 * S;    // HIGHLIGHT_BOX_SIZE scaled

    ctx.clearRect(0, 0, CW, CH);

    // 1. Background
    ctx.fillStyle = c.BACKGROUND_COLOR;
    ctx.fillRect(0, 0, CW, CH);

    // 2. Rings
    RINGS.forEach(function(ring) {
      var w = ring.w*S, h = ring.h*S, r = ring.r*S;
      rrPath(ctx, CX - w/2, CY - h/2, w, h, r);
      ctx.fillStyle = ring.color;
      ctx.fill();
      ctx.strokeStyle = c.LINE_COLOR;
      ctx.lineWidth = 1;
      ctx.stroke();
    });

    // 3. Highlight boxes
    RINGS.forEach(function(ring) {
      var w = ring.w*S, h = ring.h*S, r = ring.r*S;
      var pt = ptOnRect(w, h, r, TA);
      ctx.fillStyle = c.HIGHLIGHT_FILL_COLOR;
      ctx.fillRect(CX + pt.x - HL/2, CY + pt.y - HL/2, HL, HL);
      ctx.strokeStyle = c.LINE_COLOR;
      ctx.lineWidth = 1;
      ctx.strokeRect(CX + pt.x - HL/2, CY + pt.y - HL/2, HL, HL);
    });

    // 4. Digits
    // angle_i = TA + (i - cur) * TMAX/n   (static, no animation)
    var numFS = Math.max(8, Math.round(10 * S));
    ctx.font = numFS + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    RINGS.forEach(function(ring) {
      var w = ring.w*S, h = ring.h*S, r = ring.r*S;
      for (var i = 0; i < ring.n; i++) {
        var angle = TA + (i - ring.cur) * TMAX / ring.n;
        var pt = ptOnRect(w, h, r, angle);
        ctx.fillStyle = (i === ring.cur) ? c.HIGHLIGHT_NUMBER_COLOR : c.NUMBER_COLOR;
        ctx.fillText(ring.digs[i], CX + pt.x, CY + pt.y - 2 * S);
      }
    });

    // 5. Centre text  (non-round layout: weekday / month / day / battery)
    var step  = 16 * S;
    var batH  = 8  * S;
    var itemW = 36 * S;
    var startY = CY - (3 * step + batH) / 2;

    // Background boxes use inner ring colour (same as Pebble TextLayer background)
    ctx.fillStyle = c.INNER_RING_COLOR;
    for (var k = 0; k < 3; k++) {
      ctx.fillRect(CX - itemW / 2, startY + k * step, itemW, step);
    }

    ctx.font = 'bold ' + Math.round(11 * S) + 'px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = c.CENTER_TEXT_COLOR;
    ['Fri', 'May', '15'].forEach(function(lbl, k) {
      ctx.fillText(lbl, CX, startY + k * step + step / 2);
    });

    // Battery icon
    var bw = 18 * S, bx = CX - bw / 2, by = startY + 3 * step;
    ctx.strokeStyle = c.CENTER_TEXT_COLOR;
    ctx.lineWidth = 1;
    ctx.strokeRect(bx, by, bw * 0.87, batH);                          // body
    ctx.fillStyle = c.CENTER_TEXT_COLOR;
    ctx.fillRect(bx + bw * 0.87, by + batH * 0.25, bw * 0.13, batH * 0.5); // terminal nub
    ctx.fillRect(bx + 1, by + 1, bw * 0.87 * 0.7 - 2, batH - 2);         // ~70% charge
  }

  // ---------------------------------------------------------------------------
  // Inject canvas into the Clay page
  // ---------------------------------------------------------------------------
  function createPreview() {
    var wrap = document.createElement('div');
    wrap.style.cssText = [
      'display:flex', 'flex-direction:column', 'align-items:center',
      'padding:14px 0 12px', 'background:#141414',
      'border-bottom:1px solid #2a2a2a', 'margin-bottom:4px'
    ].join(';');

    var lbl = document.createElement('p');
    lbl.textContent = 'Live Preview';
    lbl.style.cssText = 'margin:0 0 10px;color:#888;font-size:12px;letter-spacing:.05em;text-transform:uppercase;';

    // Canvas sized 1.111× the 144×168 Pebble Time display, with padding
    var canvas = document.createElement('canvas');
    canvas.width  = 160;
    canvas.height = 196;
    canvas.style.cssText = 'display:block;border:1px solid #333;';

    wrap.appendChild(lbl);
    wrap.appendChild(canvas);
    document.body.insertBefore(wrap, document.body.firstChild);
    return canvas;
  }

  // ---------------------------------------------------------------------------
  // Wire everything up after Clay builds the form
  // ---------------------------------------------------------------------------
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var animToggle = clayConfig.getItemByMessageKey('ANIMATION_TOGGLE');
    if (animToggle) {
      toggleAnimationSettings.call(animToggle);
      animToggle.on('change', toggleAnimationSettings);
    }

    var canvas = createPreview();
    drawPreview(canvas);

    COLOR_KEYS.forEach(function(key) {
      var item = clayConfig.getItemByMessageKey(key);
      if (item) { item.on('change', function() { drawPreview(canvas); }); }
    });
  });
};
