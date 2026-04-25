var Clay = require('pebble-clay');
var clayConfig = require('./config');

var customFn = function() {
  var animToggle = this.getItemByMessageKey('ANIMATION_TOGGLE');
  var inertiaToggle = this.getItemByMessageKey('INERTIA_TOGGLE');

  function syncInertia() {
    if (animToggle.get()) {
      inertiaToggle.show();
    } else {
      inertiaToggle.hide();
    }
  }

  syncInertia();
  animToggle.on('change', syncInertia);
};

var clay = new Clay(clayConfig, customFn, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function(e) {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {
    return;
  }
  var dict = clay.getSettings(e.response);

  // Theme overrides
  var theme = parseInt(dict['THEME'] || dict[Pebble.Enums.THEME] || 0);
  if (theme > 0) {
    var themes = {
      1: { // Ocean Blue
        'INNER_RING_COLOR': '0x000055',
        'SUB_INNER_RING_COLOR': '0x000080',
        'MIDDLE_RING_COLOR': '0x0000AA',
        'OUTER_RING_COLOR': '0x0000FF',
        'HIGHLIGHT_FILL_COLOR': '0x00FFFF',
        'LINE_COLOR': '0xFFFFFF',
        'NUMBER_COLOR': '0xFFFFFF',
        'CENTER_TEXT_COLOR': '0xFFFFFF',
        'HIGHLIGHT_NUMBER_COLOR': '0x00FFFF'
      },
      2: { // Forest Green
        'INNER_RING_COLOR': '0x005500',
        'SUB_INNER_RING_COLOR': '0x007700',
        'MIDDLE_RING_COLOR': '0x00AA00',
        'OUTER_RING_COLOR': '0x55FF00',
        'HIGHLIGHT_FILL_COLOR': '0xFFFF00',
        'LINE_COLOR': '0xFFFFFF',
        'NUMBER_COLOR': '0xFFFFFF',
        'CENTER_TEXT_COLOR': '0xFFFFFF',
        'HIGHLIGHT_NUMBER_COLOR': '0xFFFF00'
      },
      3: { // Cyberpunk
        'INNER_RING_COLOR': '0x000000',
        'SUB_INNER_RING_COLOR': '0x2B0055',
        'MIDDLE_RING_COLOR': '0x550055',
        'OUTER_RING_COLOR': '0xAA00AA',
        'HIGHLIGHT_FILL_COLOR': '0xFF00FF',
        'LINE_COLOR': '0xFFFF00',
        'NUMBER_COLOR': '0xFFFF00',
        'CENTER_TEXT_COLOR': '0xFFFF00',
        'HIGHLIGHT_NUMBER_COLOR': '0xFF00FF'
      }
    };

    var selectedTheme = themes[theme];
    if (selectedTheme) {
      for (var key in selectedTheme) {
        dict[key] = parseInt(selectedTheme[key]);
      }
    }
  }

  Pebble.sendAppMessage(dict, function(e) {
    console.log('Sent config data to Pebble');
  }, function(e) {
    console.log('Failed to send config data!');
    console.log(JSON.stringify(e));
  });
});
