(function() {
  "use strict";
  // Define functions one by one and do not override the whole
  // test_driver_internal as it masks the new testing fucntions
  // that will be added in the future.
  const leftButton = 0;

  function getInViewCenterPoint(rect) {
    var left = Math.max(0, rect.left);
    var right = Math.min(window.innerWidth, rect.right);
    var top = Math.max(0, rect.top);
    var bottom = Math.min(window.innerHeight, rect.bottom);

    var x = 0.5 * (left + right);
    var y = 0.5 * (top + bottom);

    return [x, y];
  }

  function getPointerInteractablePaintTree(element, frame) {
    var frameDocument = frame == window ? window.document : frame.contentDocument;
    if (!frameDocument.contains(element)) {
      return [];
    }

    var rectangles = element.getClientRects();
    if (rectangles.length === 0) {
      return [];
    }

    var centerPoint = getInViewCenterPoint(rectangles[0]);
    if ("elementsFromPoint" in document) {
      return frameDocument.elementsFromPoint(centerPoint[0], centerPoint[1]);
    } else if ("msElementsFromPoint" in document) {
      var rv = frameDocument.msElementsFromPoint(centerPoint[0], centerPoint[1]);
      return Array.prototype.slice.call(rv ? rv : []);
    } else {
      throw new Error("document.elementsFromPoint unsupported");
    }
  }

  function inView(element, frame) {
    var pointerInteractablePaintTree = getPointerInteractablePaintTree(element, frame);
    return pointerInteractablePaintTree.indexOf(element) !== -1 || element.contains(pointerInteractablePaintTree[0], frame);
  }

  function findElementInFrame(element, frame) {
    var foundFrame = frame;
    var frameDocument = frame == window ? window.document : frame.contentDocument;
    if (!frameDocument.contains(element)) {
      foundFrame = null;
      var frames = document.getElementsByTagName("iframe");
      for (let i = 0; i < frames.length; i++) {
        if (findElementInFrame(element, frames[i])) {
          foundFrame = frames[i];
          break;
        }
      }
    }
    return foundFrame;
  }

  window.test_driver_internal.click = function(element, coords) {
    return new Promise(function(resolve, reject) {
      if (window.chrome && chrome.gpuBenchmarking) {
        chrome.gpuBenchmarking.pointerActionSequence(
            [{
              source: 'mouse',
              actions: [
              {name: 'pointerMove', x: coords.x, y: coords.y},
              {name: 'pointerDown', x: coords.x, y: coords.y, button: leftButton},
              {name: 'pointerUp', button: leftButton}
              ]
            }],
            resolve);
      } else {
        reject(new Error("GPU benchmarking is not enabled."));
      }
    });
  };

  // https://w3c.github.io/webdriver/#element-send-keys
  window.test_driver_internal.send_keys = function(element, keys) {
    return new Promise((resolve, reject) => {
      element.focus();
      if (!window.eventSender)
        reject(new Error("No eventSender"));
      if (element.localName === 'input' && element.type === 'file') {
          element.addEventListener('drop', resolve);
          eventSender.beginDragWithFiles([keys]);
          const centerX = element.offsetLeft + element.offsetWidth / 2;
          const centerY = element.offsetTop + element.offsetHeight / 2;
          // Moving the mouse could interfere with the test, if it also tries to control
          // mouse movements. This can cause differences between tests run with run_web_tests
          // and tests run with wptrunner.
          eventSender.mouseMoveTo(centerX * devicePixelRatio, centerY * devicePixelRatio);
          eventSender.mouseUp();
          return;
      }
      if (keys.length > 1)
        reject(new Error("No support for a sequence of multiple keys"));
      let eventSenderKeys = keys;
      let charCode = keys.charCodeAt(0);
      // See https://w3c.github.io/webdriver/#keyboard-actions and
      // EventSender::KeyDown().
      if (charCode == 0xE004) {
        eventSenderKeys = "Tab";
      } else if (charCode == 0xE050) {
        eventSenderKeys = "ShiftRight";
      } else if (charCode == 0xE012) {
        eventSenderKeys = "ArrowLeft";
      } else if (charCode == 0xE013) {
        eventSenderKeys = "ArrowUp";
      } else if (charCode == 0xE014) {
        eventSenderKeys = "ArrowRight";
      } else if (charCode == 0xE015) {
        eventSenderKeys = "ArrowDown";
      } else if (charCode >= 0xE000 && charCode <= 0xF8FF) {
        reject(new Error("No support for this code: U+" + charCode.toString(16)));
      }
      window.requestAnimationFrame(() => {
        window.eventSender.keyDown(eventSenderKeys);
        resolve();
      });
    });
  };

  window.test_driver_internal.freeze = function() {
    return new Promise(function(resolve, reject) {
      if (window.chrome && chrome.gpuBenchmarking) {
        chrome.gpuBenchmarking.freeze();
        resolve();
      } else {
        reject(new Error("GPU benchmarking is not enabled."));
      }
    });
  };

  window.test_driver_internal.action_sequence = function(actions) {
    if (window.top !== window) {
      return Promise.reject(new Error("can only send keys in top-level window"));
    }

    var didScrollIntoView = false;
    for (let i = 0; i < actions.length; i++) {
      var last_x_position = 0;
      var last_y_position = 0;
      var first_pointer_down = false;
      for (let j = 0; j < actions[i].actions.length; j++) {
        if ('origin' in actions[i].actions[j]) {
          if (typeof(actions[i].actions[j].origin) === 'string') {
             if (actions[i].actions[j].origin == "viewport") {
               last_x_position = actions[i].actions[j].x;
               last_y_position = actions[i].actions[j].y;
             } else if (actions[i].actions[j].origin == "pointer") {
               return Promise.reject(new Error("pointer origin is not supported right now"));
             } else {
               return Promise.reject(new Error("pointer origin is not given correctly"));
             }
          } else {
            var element = actions[i].actions[j].origin;
            var frame = findElementInFrame(element, window);
            if (frame == null) {
              return Promise.reject(new Error("element in different document or iframe"));
            }

            if (!inView(element, frame)) {
              if (didScrollIntoView)
                return Promise.reject(new Error("already scrolled into view, the element is not found"));

              element.scrollIntoView({behavior: "instant",
                                      block: "end",
                                      inline: "nearest"});
              didScrollIntoView = true;
            }

            var pointerInteractablePaintTree = getPointerInteractablePaintTree(element, frame);
            if (pointerInteractablePaintTree.length === 0 ||
                !element.contains(pointerInteractablePaintTree[0])) {
              return Promise.reject(new Error("element click intercepted error"));
            }

            var rect = element.getClientRects()[0];
            var centerPoint = getInViewCenterPoint(rect);
            last_x_position = actions[i].actions[j].x + centerPoint[0];
            last_y_position = actions[i].actions[j].y + centerPoint[1];
            if (frame != window) {
              var frameRect = frame.getClientRects();
              last_x_position += frameRect[0].left;
              last_y_position += frameRect[0].top;
            }
          }
        }

        if (actions[i].actions[j].type == "pointerDown" || actions[i].actions[j].type == "pointerMove") {
          actions[i].actions[j].x = last_x_position;
          actions[i].actions[j].y = last_y_position;
        }

        if ('parameters' in actions[i] && actions[i].parameters.pointerType == "touch") {
          if (actions[i].actions[j].type == "pointerMove" && !first_pointer_down) {
            actions[i].actions[j].type = "pause";
          } else if (actions[i].actions[j].type == "pointerDown") {
            first_pointer_down = true;
          } else if (actions[i].actions[j].type == "pointerUp") {
            first_pointer_down = false;
          }
        }
      }
    }

    return new Promise(function(resolve, reject) {
      if (window.chrome && chrome.gpuBenchmarking) {
        chrome.gpuBenchmarking.pointerActionSequence(actions, resolve);
      } else {
        reject(new Error("GPU benchmarking is not enabled."));
      }
    });
  };

  let virtualAuthenticatorManager_;

  async function findAuthenticator(authenticatorManager, authenticatorId) {
    let authenticators = (await authenticatorManager.getAuthenticators()).authenticators;
    let foundAuthenticator;
    for (let authenticator of authenticators) {
      if ((await authenticator.getUniqueId()).id == authenticatorId) {
        foundAuthenticator = authenticator;
        break;
      }
    }
    if (!foundAuthenticator) {
      throw "Cannot find authenticator with ID " + authenticatorId;
    }
    return foundAuthenticator;
  }

  function loadVirtualAuthenticatorManager() {
    if (virtualAuthenticatorManager_) {
      return Promise.resolve(virtualAuthenticatorManager_);
    }

    return Promise.all([
      "/gen/layout_test_data/mojo/public/js/mojo_bindings_lite.js",
      "/gen/mojo/public/mojom/base/time.mojom-lite.js",
      "/gen/url/mojom/url.mojom-lite.js",
      "/gen/third_party/blink/public/mojom/webauthn/authenticator.mojom-lite.js",
      "/gen/third_party/blink/public/mojom/webauthn/virtual_authenticator.mojom-lite.js",
    ].map(dependency => new Promise((resolve, reject) => {
      let script = document.createElement("script");
      script.src = dependency;
      script.async = false;
      script.onload = resolve;
      document.head.appendChild(script);
    }))).then(() => {
      virtualAuthenticatorManager_ = new blink.test.mojom.VirtualAuthenticatorManagerRemote;
      Mojo.bindInterface(
        blink.test.mojom.VirtualAuthenticatorManager.$interfaceName,
        virtualAuthenticatorManager_.$.bindNewPipeAndPassReceiver().handle, "context", true);
      return virtualAuthenticatorManager_;
    });
  }

  function urlSafeBase64ToUint8Array(base64url) {
    let base64 = base64url.replace(/-/g, "+").replace(/_/g, "/");
    // Add padding to make the length of the base64 string divisible by 4.
    if (base64.length % 4 != 0)
      base64 += "=".repeat(4 - base64.length % 4);
    return Uint8Array.from(atob(base64), c => c.charCodeAt(0));
  }

  function uint8ArrayToUrlSafeBase64(array) {
    let binary = "";
    for (let i = 0; i < array.length; ++i)
      binary += String.fromCharCode(array[i]);

    return window.btoa(binary)
      .replace(/\+/g, "-")
      .replace(/\//g, "_")
      .replace(/=/g, "");
  }

  window.test_driver_internal.add_virtual_authenticator = async function(options) {
    let manager = await loadVirtualAuthenticatorManager();

    options = Object.assign({
      hasResidentKey: false,
      hasUserVerification: false,
      isUserConsenting: true,
      isUserVerified: false,
    }, options);
    let mojoOptions = {};
    switch (options.protocol) {
      case "ctap1/u2f":
        mojoOptions.protocol = blink.test.mojom.ClientToAuthenticatorProtocol.U2F;
        break;
      case "ctap2":
        mojoOptions.protocol = blink.test.mojom.ClientToAuthenticatorProtocol.CTAP2;
        break;
      default:
        throw "Unknown protocol "  + options.protocol;
    }
    switch (options.transport) {
      case "usb":
        mojoOptions.transport = blink.mojom.AuthenticatorTransport.USB;
        mojoOptions.attachment = blink.mojom.AuthenticatorAttachment.CROSS_PLATFORM;
        break;
      case "nfc":
        mojoOptions.transport = blink.mojom.AuthenticatorTransport.NFC;
        mojoOptions.attachment = blink.mojom.AuthenticatorAttachment.CROSS_PLATFORM;
        break;
      case "ble":
        mojoOptions.transport = blink.mojom.AuthenticatorTransport.BLE;
        mojoOptions.attachment = blink.mojom.AuthenticatorAttachment.CROSS_PLATFORM;
        break;
      case "internal":
        mojoOptions.transport = blink.mojom.AuthenticatorTransport.INTERNAL;
        mojoOptions.attachment = blink.mojom.AuthenticatorAttachment.PLATFORM;
        break;
      default:
        throw "Unknown transport "  + options.transport;
    }
    mojoOptions.hasResidentKey = options.hasResidentKey;
    mojoOptions.hasUserVerification = options.hasUserVerification;

    let authenticator = (await manager.createAuthenticator(mojoOptions)).authenticator;
    return (await authenticator.getUniqueId()).id;
  };

  window.test_driver_internal.add_credential = async function(authenticatorId, credential) {
    if (credential.isResidentCredential) {
      throw "The mojo virtual authenticator manager does not support resident credentials";
    }
    let manager = await loadVirtualAuthenticatorManager();
    let authenticator = await findAuthenticator(manager, authenticatorId);

    let registration = {
      keyHandle: urlSafeBase64ToUint8Array(credential.credentialId),
      privateKey: urlSafeBase64ToUint8Array(credential.privateKey),
      rpId: credential.rpId,
      counter: credential.signCount,
    };
    let addRegistrationResponse = await authenticator.addRegistration(registration);
    if (!addRegistrationResponse.added) {
      throw "Could not add credential";
    }
  };

  window.test_driver_internal.get_credentials = async function(authenticatorId) {
    let manager = await loadVirtualAuthenticatorManager();
    let authenticator = await findAuthenticator(manager, authenticatorId);

    let getCredentialsResponse = await authenticator.getRegistrations();
    return getCredentialsResponse.keys.map(key => ({
      credentialId: uint8ArrayToUrlSafeBase64(key.keyHandle),
      privateKey: uint8ArrayToUrlSafeBase64(key.privateKey),
      rpId: key.rpId,
      signCount: key.counter,
      isResidentCredential: false,
    }));
  };

  window.test_driver_internal.remove_credential = async function(authenticatorId, credentialId) {
    let manager = await loadVirtualAuthenticatorManager();
    let authenticator = await findAuthenticator(manager, authenticatorId);

    let removeRegistrationResponse = await authenticator.removeRegistration(
        urlSafeBase64ToUint8Array(credentialId));
    if (!removeRegistrationResponse.removed) {
      throw "Could not remove credential";
    }
  };

  window.test_driver_internal.remove_all_credentials = async function(authenticatorId) {
    let manager = await loadVirtualAuthenticatorManager();
    let authenticator = await findAuthenticator(manager, authenticatorId);
    await authenticator.clearRegistrations();
  }

  window.test_driver_internal.set_user_verified = async function(authenticatorId, options) {
    let manager = await loadVirtualAuthenticatorManager();
    let authenticator = await findAuthenticator(manager, authenticatorId);
    await authenticator.setUserVerified(options.isUserVerified);
  }

  window.test_driver_internal.remove_virtual_authenticator = async function(authenticatorId) {
    let manager = await loadVirtualAuthenticatorManager();
    let response = await manager.removeAuthenticator(authenticatorId);
    if (!response.removed)
      throw "Could not remove authenticator";
  }

  window.test_driver_internal.set_permission = function(permission_params) {
    // TODO(https://crbug.com/977612): Chromium currently lacks support for
    // |permission_params.one_realm| and will always consider it is set to false.
    return internals.setPermission(permission_params.descriptor,
                                   permission_params.state,
                                   location.origin, location.origin);
  }

  // Enable automation so we don't wait for user input on unimplemented APIs
  window.test_driver_internal.in_automation = true;

})();
