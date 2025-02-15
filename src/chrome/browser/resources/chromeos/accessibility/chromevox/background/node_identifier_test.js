// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include test fixture.
GEN_INCLUDE(['../testing/chromevox_next_e2e_test_base.js']);

/**
 * Test fixture for NodeIdentifier.
 */
ChromeVoxNodeIdentifierTest = class extends ChromeVoxNextE2ETest {
  /**
   * Returns the start node of the current ChromeVox range.
   */
  getRangeStart() {
    return ChromeVoxState.instance.getCurrentRange().start.node;
  }

  /** @override */
  setUp() {
    window.RoleType = chrome.automation.RoleType;
  }

  get basicButtonDoc() {
    return `
      <p>Start here</p>
      <button id="apple-button">Apple</button>
      <button>Orange</button>
    `;
  }

  get duplicateButtonDoc() {
    return `
      <p>Start here</p>
      <button>Click me</button>
      <button>Click me</button>
    `;
  }

  get identicalListsDoc() {
    return `
      <p>Start here</p>
      <ul>
        <li>Apple</li>
        <li>Orange</li>
      </ul>
      <ul>
        <li>Apple</li>
        <li>Orange</li>
      </ul>
    `;
  }
};


// Tests that we can distinguish between two similar buttons.
TEST_F('ChromeVoxNodeIdentifierTest', 'BasicButtonTest', function() {
  this.runWithLoadedTree(this.basicButtonDoc, function(rootNode) {
    const appleNode =
        rootNode.find({role: RoleType.BUTTON, attributes: {name: 'Apple'}});
    const orangeNode =
        rootNode.find({role: RoleType.BUTTON, attributes: {name: 'Orange'}});
    assertFalse(!appleNode);
    assertFalse(!orangeNode);
    const appleId = new NodeIdentifier(appleNode);
    const duplicateAppleId = new NodeIdentifier(appleNode);
    const orangeId = new NodeIdentifier(orangeNode);

    assertTrue(appleId.equals(duplicateAppleId));
    assertFalse(appleId.equals(orangeId));
  });
});

// Tests that we can distinguish two buttons with the same name.
TEST_F('ChromeVoxNodeIdentifierTest', 'DuplicateButtonTest', function() {
  this.runWithLoadedTree(this.duplicateButtonDoc, function() {
    CommandHandler.onCommand('nextButton');
    const firstButton = this.getRangeStart();
    const firstButtonId = new NodeIdentifier(firstButton);
    CommandHandler.onCommand('nextButton');
    const secondButton = this.getRangeStart();
    const secondButtonId = new NodeIdentifier(secondButton);

    assertFalse(firstButtonId.equals(secondButtonId));
  });
});

// Tests that we can differentiate between the list items of two identical
// lists.
TEST_F('ChromeVoxNodeIdentifierTest', 'IdenticalListsTest', function() {
  this.runWithLoadedTree(this.identicalListsDoc, function() {
    // Create NodeIdentifiers for each item.
    CommandHandler.onCommand('nextObject');
    CommandHandler.onCommand('nextObject');
    const firstApple = new NodeIdentifier(this.getRangeStart());
    CommandHandler.onCommand('nextObject');
    CommandHandler.onCommand('nextObject');
    const firstOrange = new NodeIdentifier(this.getRangeStart());
    CommandHandler.onCommand('nextObject');
    CommandHandler.onCommand('nextObject');
    const secondApple = new NodeIdentifier(this.getRangeStart());
    CommandHandler.onCommand('nextObject');
    CommandHandler.onCommand('nextObject');
    const secondOrange = new NodeIdentifier(this.getRangeStart());

    assertFalse(firstApple.equals(secondApple));
    assertFalse(firstOrange.equals(secondOrange));
  });
});

// Tests that we can successfully stringify NodeIdentifiers.
TEST_F('ChromeVoxNodeIdentifierTest', 'ToStringTest', function() {
  this.runWithLoadedTree(this.basicButtonDoc, function(rootNode) {
    var appleNode =
        rootNode.find({role: RoleType.BUTTON, attributes: {name: 'Apple'}});
    var orangeNode =
        rootNode.find({role: RoleType.BUTTON, attributes: {name: 'Orange'}});
    assertFalse(!appleNode);
    assertFalse(!orangeNode);
    var appleId = new NodeIdentifier(appleNode);
    var orangeId = new NodeIdentifier(orangeNode);

    var expectedAppleString = [
      '{"attributes":{"id":"apple-button","name":"Apple","role":"button",',
      '"description":"","restriction":"","childCount":0,"indexInParent":1,',
      '"className":"","htmlTag":"button"},',
      '"pageUrl":"data:text/html,<!doctype html>%3Cp%3EStart%20here%3C%2Fp%3E',
      '%20%3Cbutton%20id%3D%22apple-button%22%3EApple%3C%2Fbutton%3E%20%3Cbut',
      'ton%3EOrange%3C%2Fbutton%3E","ancestry":[]}'
    ].join('');
    var expectedOrangeString = [
      '{"attributes":{"id":"","name":"Orange","role":"button",',
      '"description":"","restriction":"","childCount":0,"indexInParent":2,',
      '"className":"","htmlTag":"button"},"pageUrl":"data:text/html,<!doctype',
      ' html>%3Cp%3EStart%20here%3C%2Fp%3E%20%3Cbutton%20id%3D%22apple-button',
      '%22%3EApple%3C%2Fbutton%3E%20%3Cbutton%3EOrange%3C%2Fbutton%3E",',
      '"ancestry":[]}'
    ].join('');

    assertEquals(appleId.toString(), expectedAppleString);
    assertEquals(orangeId.toString(), expectedOrangeString);
  });
});
