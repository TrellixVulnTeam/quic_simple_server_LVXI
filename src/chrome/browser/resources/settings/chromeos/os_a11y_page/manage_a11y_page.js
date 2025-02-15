// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-manage-a11y-page' is the subpage with the accessibility
 * settings.
 */
Polymer({
  is: 'settings-manage-a11y-page',

  behaviors: [WebUIListenerBehavior, settings.RouteOriginBehavior],

  properties: {
    /**
     * Preferences state.
     */
    prefs: {
      type: Object,
      notify: true,
    },

    screenMagnifierZoomOptions_: {
      readOnly: true,
      type: Array,
      value() {
        // These values correspond to the i18n values in settings_strings.grdp.
        // If these values get changed then those strings need to be changed as
        // well.
        return [
          {value: 2, name: loadTimeData.getString('screenMagnifierZoom2x')},
          {value: 4, name: loadTimeData.getString('screenMagnifierZoom4x')},
          {value: 6, name: loadTimeData.getString('screenMagnifierZoom6x')},
          {value: 8, name: loadTimeData.getString('screenMagnifierZoom8x')},
          {value: 10, name: loadTimeData.getString('screenMagnifierZoom10x')},
          {value: 12, name: loadTimeData.getString('screenMagnifierZoom12x')},
          {value: 14, name: loadTimeData.getString('screenMagnifierZoom14x')},
          {value: 16, name: loadTimeData.getString('screenMagnifierZoom16x')},
          {value: 18, name: loadTimeData.getString('screenMagnifierZoom18x')},
          {value: 20, name: loadTimeData.getString('screenMagnifierZoom20x')},
        ];
      },
    },

    autoClickDelayOptions_: {
      readOnly: true,
      type: Array,
      value() {
        // These values correspond to the i18n values in settings_strings.grdp.
        // If these values get changed then those strings need to be changed as
        // well.
        return [
          {
            value: 600,
            name: loadTimeData.getString('delayBeforeClickExtremelyShort')
          },
          {
            value: 800,
            name: loadTimeData.getString('delayBeforeClickVeryShort')
          },
          {value: 1000, name: loadTimeData.getString('delayBeforeClickShort')},
          {value: 2000, name: loadTimeData.getString('delayBeforeClickLong')},
          {
            value: 4000,
            name: loadTimeData.getString('delayBeforeClickVeryLong')
          },
        ];
      },
    },

    autoClickMovementThresholdOptions_: {
      readOnly: true,
      type: Array,
      value() {
        return [
          {
            value: 5,
            name: loadTimeData.getString('autoclickMovementThresholdExtraSmall')
          },
          {
            value: 10,
            name: loadTimeData.getString('autoclickMovementThresholdSmall')
          },
          {
            value: 20,
            name: loadTimeData.getString('autoclickMovementThresholdDefault')
          },
          {
            value: 30,
            name: loadTimeData.getString('autoclickMovementThresholdLarge')
          },
          {
            value: 40,
            name: loadTimeData.getString('autoclickMovementThresholdExtraLarge')
          },
        ];
      },
    },

    showExperimentalSwitchAccess_: {
      type: Boolean,
      value() {
        return loadTimeData.getBoolean(
            'showExperimentalAccessibilitySwitchAccess');
      },
    },

    /** @private */
    isGuest_: {
      type: Boolean,
      value() {
        return loadTimeData.getBoolean('isGuest');
      }
    },

    /**
     * |hasKeyboard_|, |hasMouse_|, and |hasTouchpad_| start undefined so
     * observers don't trigger until they have been populated.
     * @private
     */
    hasKeyboard_: Boolean,

    /** @private */
    hasMouse_: Boolean,

    /** @private */
    hasTouchpad_: Boolean,
  },

  observers: [
    'pointersChanged_(hasMouse_, hasTouchpad_)',
  ],

  /** settings.RouteOriginBehavior override */
  route_: settings.routes.MANAGE_ACCESSIBILITY,

  /** @override */
  attached() {
    this.addWebUIListener(
        'has-mouse-changed', this.set.bind(this, 'hasMouse_'));
    this.addWebUIListener(
        'has-touchpad-changed', this.set.bind(this, 'hasTouchpad_'));
    settings.DevicePageBrowserProxyImpl.getInstance().initializePointers();

    this.addWebUIListener(
        'has-hardware-keyboard', this.set.bind(this, 'hasKeyboard_'));
    chrome.send('initializeKeyboardWatcher');
  },

  /** @override */
  ready() {
    this.addWebUIListener(
        'startup-sound-enabled-updated',
        this.updateStartupSoundEnabled_.bind(this));
    chrome.send('getStartupSoundEnabled');

    const r = settings.routes;
    this.addFocusConfig_(r.MANAGE_TTS_SETTINGS, '#ttsSubpageButton');
    this.addFocusConfig_(r.MANAGE_CAPTION_SETTINGS, '#captionsSubpageButton');
    this.addFocusConfig_(
        r.MANAGE_SWITCH_ACCESS_SETTINGS, '#switchAccessSubpageButton');
    this.addFocusConfig_(r.DISPLAY, '#displaySubpageButton');
    this.addFocusConfig_(r.APPEARANCE, '#appearanceSubpageButton');
    this.addFocusConfig_(r.KEYBOARD, '#keyboardSubpageButton');
    this.addFocusConfig_(r.POINTERS, '#pointerSubpageButton');
  },

  /**
   * @param {boolean} hasMouse
   * @param {boolean} hasTouchpad
   * @private
   */
  pointersChanged_(hasMouse, hasTouchpad) {
    this.$.pointerSubpageButton.hidden = !hasMouse && !hasTouchpad;
  },

  /**
   * Updates the Select-to-Speak description text based on:
   *    1. Whether Select-to-Speak is enabled.
   *    2. If it is enabled, whether a physical keyboard is present.
   * @param {boolean} enabled
   * @param {boolean} hasKeyboard
   * @param {string} disabledString String to show when Select-to-Speak is
   *    disabled.
   * @param {string} keyboardString String to show when there is a physical
   *    keyboard
   * @param {string} noKeyboardString String to show when there is no keyboard
   * @private
   */
  getSelectToSpeakDescription_(
      enabled, hasKeyboard, disabledString, keyboardString, noKeyboardString) {
    return !enabled ? disabledString :
                      hasKeyboard ? keyboardString : noKeyboardString;
  },

  /**
   * @param {!CustomEvent<boolean>} e
   * @private
   */
  toggleStartupSoundEnabled_(e) {
    chrome.send('setStartupSoundEnabled', [e.detail]);
  },

  /**
   * @param {boolean} enabled
   * @private
   */
  updateStartupSoundEnabled_(enabled) {
    this.$.startupSoundEnabled.checked = enabled;
  },

  /** @private */
  onManageTtsSettingsTap_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.MANAGE_TTS_SETTINGS);
  },

  /** @private */
  onChromeVoxSettingsTap_() {
    chrome.send('showChromeVoxSettings');
  },

  /** @private */
  onCaptionsClick_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.MANAGE_CAPTION_SETTINGS);
  },

  /** @private */
  onSelectToSpeakSettingsTap_() {
    chrome.send('showSelectToSpeakSettings');
  },

  /** @private */
  onSwitchAccessSettingsTap_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.MANAGE_SWITCH_ACCESS_SETTINGS);
  },

  /** @private */
  onDisplayTap_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.DISPLAY,
        /* dynamicParams */ null, /* removeSearch */ true);
  },

  /** @private */
  onAppearanceTap_() {
    // Open browser appearance section in a new browser tab.
    window.open('chrome://settings/appearance');
  },

  /** @private */
  onKeyboardTap_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.KEYBOARD,
        /* dynamicParams */ null, /* removeSearch */ true);
  },

  /** @private */
  onMouseTap_() {
    settings.Router.getInstance().navigateTo(
        settings.routes.POINTERS,
        /* dynamicParams */ null, /* removeSearch */ true);
  },
});
