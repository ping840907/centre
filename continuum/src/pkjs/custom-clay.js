module.exports = function(minified) {
  var clayConfig = this;

  // Hide the Inertia Effect toggle and FPS slider while animations are disabled.
  function toggleAnimationSettings() {
    var inertiaItem = clayConfig.getItemByMessageKey('INERTIA_TOGGLE');
    var fpsItem = clayConfig.getItemByMessageKey('ANIM_FPS');
    var enabled = this.get();

    if (inertiaItem) {
      if (enabled) {
        inertiaItem.show();
      } else {
        inertiaItem.hide();
      }
    }

    if (fpsItem) {
      if (enabled) {
        fpsItem.show();
      } else {
        fpsItem.hide();
      }
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    // Existing animation/inertia coupling
    var animToggle = clayConfig.getItemByMessageKey('ANIMATION_TOGGLE');
    if (animToggle) {
      toggleAnimationSettings.call(animToggle);
      animToggle.on('change', toggleAnimationSettings);
    }
  });
};
