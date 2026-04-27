module.exports = function(minified) {
  var clayConfig = this;

  // Clay injects activeWatchInfo (from Pebble.getActiveWatchInfo()) into
  // the config page via meta.  If the watch is not connected it may be null.
  var watchInfo = clayConfig.meta && clayConfig.meta.activeWatchInfo;
  var platform  = watchInfo ? watchInfo.platform : null;
  var isBW      = (platform === 'aplite' || platform === 'diorite');

  // Hide the Inertia Effect toggle while animations are disabled.
  function toggleInertia() {
    var inertiaItem = clayConfig.getItemByMessageKey('INERTIA_TOGGLE');
    if (inertiaItem) {
      if (this.get()) {
        inertiaItem.show();
      } else {
        inertiaItem.hide();
      }
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    // Existing animation/inertia coupling
    var animToggle = clayConfig.getItemByMessageKey('ANIMATION_TOGGLE');
    if (animToggle) {
      toggleInertia.call(animToggle);
      animToggle.on('change', toggleInertia);
    }

    if (isBW) {
      // B&W platforms: colour settings and themes are irrelevant.
      // Hide the entire Theme and Colors sections (id set in config.js).
      ['section-theme', 'section-colors'].forEach(function(id) {
        var item = clayConfig.getItemById(id);
        if (item) item.hide();
      });
    } else {
      // Colour platforms: the B&W inversion toggle is irrelevant.
      var invertBW = clayConfig.getItemByMessageKey('INVERT_BW');
      if (invertBW) invertBW.hide();
    }
  });
};
