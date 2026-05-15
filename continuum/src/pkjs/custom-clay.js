module.exports = function(minified) {
  var clayConfig = this;

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
  // Color preview
  // ---------------------------------------------------------------------------

  // Convert a Clay color value (integer or "0xRRGGBB" string) to CSS hex.
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
    BACKGROUND_COLOR:     '#000000',
    INNER_RING_COLOR:     '#000000',
    SUB_INNER_RING_COLOR: '#000000',
    MIDDLE_RING_COLOR:    '#000000',
    OUTER_RING_COLOR:     '#000000',
    HIGHLIGHT_FILL_COLOR: '#FF0000',
    LINE_COLOR:           '#555555',
    NUMBER_COLOR:         '#AAAAAA',
    CENTER_TEXT_COLOR:    '#FFFFFF',
    HIGHLIGHT_NUMBER_COLOR: '#FFFFFF'
  };

  function getColors() {
    var colors = {};
    COLOR_KEYS.forEach(function(key) {
      var item = clayConfig.getItemByMessageKey(key);
      colors[key] = item ? toCSS(item.get()) : COLOR_DEFAULTS[key];
    });
    return colors;
  }

  // Draws a simplified Pebble Round watch face onto the canvas.
  // Ring layout mirrors the PBL_ROUND variant in main.c (180×180 display):
  //   rings[3] outer    r = 83
  //   rings[2] middle   r = 69
  //   rings[1] sub_inner r = 55
  //   rings[0] inner    r = 41
  // Highlight boxes appear at the 3 o'clock position on each ring.
  function drawPreview(canvas) {
    var ctx = canvas.getContext('2d');
    var SIZE = canvas.width; // 180
    var SCALE = SIZE / 180;
    var cx = SIZE / 2, cy = SIZE / 2;
    var c = getColors();

    // Ring radii (from main.c PBL_ROUND 180×180 rings, scaled)
    var R = [83, 69, 55, 41].map(function(r) { return Math.round(r * SCALE); });
    var ringFill = [c.OUTER_RING_COLOR, c.MIDDLE_RING_COLOR, c.SUB_INNER_RING_COLOR, c.INNER_RING_COLOR];

    ctx.clearRect(0, 0, SIZE, SIZE);

    // Clip to circular watch face
    ctx.save();
    ctx.beginPath();
    ctx.arc(cx, cy, Math.round(89 * SCALE), 0, Math.PI * 2);
    ctx.clip();

    // Background
    ctx.fillStyle = c.BACKGROUND_COLOR;
    ctx.fillRect(0, 0, SIZE, SIZE);

    // Rings: draw from outermost (R[0]) to innermost (R[3])
    for (var i = 0; i < R.length; i++) {
      ctx.beginPath();
      ctx.arc(cx, cy, R[i], 0, Math.PI * 2);
      ctx.fillStyle = ringFill[i];
      ctx.fill();
      ctx.strokeStyle = c.LINE_COLOR;
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    // Highlight boxes at 3 o'clock on each ring (right position)
    var HL = Math.round(15 * SCALE);
    ctx.fillStyle = c.HIGHLIGHT_FILL_COLOR;
    ctx.strokeStyle = c.LINE_COLOR;
    ctx.lineWidth = 1;
    for (var j = 0; j < R.length; j++) {
      var hx = cx + R[j], hy = cy;
      ctx.fillRect(hx - HL / 2, hy - HL / 2, HL, HL);
      ctx.strokeRect(hx - HL / 2, hy - HL / 2, HL, HL);
    }

    // Sample digits on the three outer ring bands (12, 6, 9 o'clock positions)
    var bandMid = [
      Math.round(((R[0] + R[1]) / 2)),  // outer band mid-radius
      Math.round(((R[1] + R[2]) / 2)),  // middle band mid-radius
      Math.round(((R[2] + R[3]) / 2))   // sub-inner band mid-radius
    ];
    var numAngles = [-Math.PI / 2, Math.PI / 2, Math.PI]; // 12, 6, 9 o'clock
    var fontSize = Math.round(10 * SCALE);
    ctx.font = fontSize + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = c.NUMBER_COLOR;
    bandMid.forEach(function(r) {
      numAngles.forEach(function(angle) {
        ctx.fillText('5', cx + r * Math.cos(angle), cy + r * Math.sin(angle));
      });
    });

    // Center time text
    ctx.font = 'bold ' + Math.round(13 * SCALE) + 'px monospace';
    ctx.fillStyle = c.CENTER_TEXT_COLOR;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('12:00', cx, cy);

    ctx.restore();
  }

  function createPreview() {
    var wrap = document.createElement('div');
    wrap.style.cssText = [
      'display:flex', 'flex-direction:column', 'align-items:center',
      'padding:14px 0 10px', 'background:#141414',
      'border-bottom:1px solid #2a2a2a', 'margin-bottom:4px'
    ].join(';');

    var lbl = document.createElement('p');
    lbl.textContent = 'Live Preview';
    lbl.style.cssText = 'margin:0 0 10px; color:#888; font-size:12px; letter-spacing:0.05em; text-transform:uppercase;';

    var canvas = document.createElement('canvas');
    canvas.width = 180;
    canvas.height = 180;
    canvas.style.cssText = 'border-radius:50%; display:block;';

    wrap.appendChild(lbl);
    wrap.appendChild(canvas);

    // Insert before the first element of the page body
    var body = document.body;
    body.insertBefore(wrap, body.firstChild);

    return canvas;
  }

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
      if (item) {
        item.on('change', function() { drawPreview(canvas); });
      }
    });
  });
};
