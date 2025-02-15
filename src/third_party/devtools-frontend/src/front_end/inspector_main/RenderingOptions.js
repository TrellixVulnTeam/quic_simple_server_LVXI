/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

export class RenderingOptionsView extends UI.VBox {
  constructor() {
    super(true);
    this.registerRequiredCSS('inspector_main/renderingOptions.css');

    this._appendCheckbox(
        ls`Paint flashing`,
        ls
        `Highlights areas of the page (green) that need to be repainted. May not be suitable for people prone to photosensitive epilepsy.`,
        self.Common.settings.moduleSetting('showPaintRects'));
    this._appendCheckbox(
        ls`Layout Shift Regions`,
        ls
        `Highlights areas of the page (blue) that were shifted. May not be suitable for people prone to photosensitive epilepsy.`,
        self.Common.settings.moduleSetting('showLayoutShiftRegions'));
        this._appendCheckbox(
            ls`Layer borders`, ls`Shows layer borders (orange/olive) and tiles (cyan).`,
            self.Common.settings.moduleSetting('showDebugBorders'));
        this._appendCheckbox(
            ls`FPS meter`, ls`Plots frames per second, frame rate distribution, and GPU memory.`,
            self.Common.settings.moduleSetting('showFPSCounter'));
    this._appendCheckbox(
        ls`Scrolling performance issues`,
        ls
        `Highlights elements (teal) that can slow down scrolling, including touch & wheel event handlers and other main-thread scrolling situations.`,
        self.Common.settings.moduleSetting('showScrollBottleneckRects'));
        this._appendCheckbox(
            ls`Highlight ad frames`, ls`Highlights frames (red) detected to be ads.`,
            self.Common.settings.moduleSetting('showAdHighlights'));
        this._appendCheckbox(
            ls`Hit-test borders`, ls`Shows borders around hit-test regions.`,
            self.Common.settings.moduleSetting('showHitTestBorders'));
        this.contentElement.createChild('div').classList.add('panel-section-separator');

        this._appendSelect(
            ls`Forces media type for testing print and screen styles`,
            self.Common.settings.moduleSetting('emulatedCSSMedia'));
        this._appendSelect(
            ls`Forces CSS prefers-color-scheme media feature`,
            self.Common.settings.moduleSetting('emulatedCSSMediaFeaturePrefersColorScheme'));
        this._appendSelect(
            ls`Forces CSS prefers-reduced-motion media feature`,
            self.Common.settings.moduleSetting('emulatedCSSMediaFeaturePrefersReducedMotion'));
  }

  /**
   * @param {string} label
   * @param {string} subtitle
   * @param {!Common.Setting} setting
   */
  _appendCheckbox(label, subtitle, setting) {
    const checkboxLabel = UI.CheckboxLabel.create(label, false, subtitle);
    UI.SettingsUI.bindCheckbox(checkboxLabel.checkboxElement, setting);
    this.contentElement.appendChild(checkboxLabel);
  }

  /**
   * @param {string} label
   * @param {!Common.Setting} setting
   */
  _appendSelect(label, setting) {
    const control = UI.SettingsUI.createControlForSetting(setting, label);
    if (control) {
      this.contentElement.appendChild(control);
    }
  }
}
