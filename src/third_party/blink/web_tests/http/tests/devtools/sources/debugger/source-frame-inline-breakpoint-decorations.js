// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult(`Checks that JavaScriptSourceFrame show inline breakpoints correctly\n`);
  await TestRunner.loadModule('sources_test_runner');
  await TestRunner.showPanel('sources');
  await TestRunner.evaluateInPagePromise(`
      function foo()
      {
          var p = Promise.resolve().then(() => console.log(42))
              .then(() => console.log(239));
          return p;
      }

      // some comment.


      // another comment.




      function boo() {
        return 42;
      }

      console.log(42);
      //# sourceURL=foo.js
    `);

  async function runAsyncBreakpointActionAndDumpDecorations(sourceFrame, action) {
    const waitPromise = SourcesTestRunner.waitDebuggerPluginBreakpoints(sourceFrame);
    await action();
    await waitPromise;
    SourcesTestRunner.dumpDebuggerPluginBreakpoints(sourceFrame);
  }

  Bindings.breakpointManager._storage._breakpoints = new Map();
  SourcesTestRunner.runDebuggerTestSuite([
    function testAddRemoveBreakpoint(next) {
      var javaScriptSourceFrame;
      SourcesTestRunner.showScriptSource('foo.js', addBreakpoint);

      function addBreakpoint(sourceFrame) {
        javaScriptSourceFrame = sourceFrame;
        TestRunner.addResult('Setting breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.createNewBreakpoint(javaScriptSourceFrame, 11, '', true)
        ).then(removeBreakpoint);
      }

      function removeBreakpoint() {
        TestRunner.addResult('Toggle breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.toggleBreakpoint(javaScriptSourceFrame, 11)
        ).then(next);
      }
    },

    function testAddRemoveBreakpointInLineWithOneLocation(next) {
      var javaScriptSourceFrame;
      SourcesTestRunner.showScriptSource('foo.js', addBreakpoint);

      function addBreakpoint(sourceFrame) {
        javaScriptSourceFrame = sourceFrame;
        TestRunner.addResult('Setting breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.createNewBreakpoint(javaScriptSourceFrame, 13, '', true)
        ).then(removeBreakpoint);
      }

      function removeBreakpoint() {
        TestRunner.addResult('Toggle breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.toggleBreakpoint(javaScriptSourceFrame, 13)
        ).then(next);
      }
    },

    function clickByInlineBreakpoint(next) {
      var javaScriptSourceFrame;
      SourcesTestRunner.showScriptSource('foo.js', addBreakpoint);

      function addBreakpoint(sourceFrame) {
        javaScriptSourceFrame = sourceFrame;
        TestRunner.addResult('Setting breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.createNewBreakpoint(javaScriptSourceFrame, 11, '', true)
        ).then(clickBySecondLocation);
      }

      function clickBySecondLocation() {
        TestRunner.addResult('Click by second breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.clickDebuggerPluginBreakpoint(
              javaScriptSourceFrame, 11, 1, next)
        ).then(clickByFirstLocation);
      }

      function clickByFirstLocation() {
        TestRunner.addResult('Click by first breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.clickDebuggerPluginBreakpoint(
              javaScriptSourceFrame, 11, 0, next)
        ).then(clickBySecondLocationAgain);
      }

      function clickBySecondLocationAgain() {
        TestRunner.addResult('Click by second breakpoint');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.clickDebuggerPluginBreakpoint(
              javaScriptSourceFrame, 11, 1, next)
        ).then(next);
      }
    },

    function toggleBreakpointInAnotherLineWontRemoveExisting(next) {
      var javaScriptSourceFrame;
      SourcesTestRunner.showScriptSource('foo.js', addBreakpoint);

      function addBreakpoint(sourceFrame) {
        javaScriptSourceFrame = sourceFrame;
        TestRunner.addResult('Setting breakpoint in line 4');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.toggleBreakpoint(javaScriptSourceFrame, 12, false)
        ).then(toggleBreakpointInAnotherLine);
      }

      function toggleBreakpointInAnotherLine() {
        TestRunner.addResult('Setting breakpoint in line 3');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
          SourcesTestRunner.toggleBreakpoint(javaScriptSourceFrame, 11, false)
        ).then(removeBreakpoints);
      }

      function removeBreakpoints() {
        TestRunner.addResult('Click by first inline breakpoints');
        runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () => {
          SourcesTestRunner.clickDebuggerPluginBreakpoint(
              javaScriptSourceFrame, 11, 0, next);
          SourcesTestRunner.clickDebuggerPluginBreakpoint(
              javaScriptSourceFrame, 12, 0, next);
        }).then(next);
      }
    },

    async function testAddRemoveBreakpointInLineWithoutBreakableLocations(next) {
      let javaScriptSourceFrame = await SourcesTestRunner.showScriptSourcePromise('foo.js');

      TestRunner.addResult('Setting breakpoint');
      await runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
        SourcesTestRunner.createNewBreakpoint(javaScriptSourceFrame, 16, '', true)
      );

      TestRunner.addResult('Toggle breakpoint');
      await runAsyncBreakpointActionAndDumpDecorations(javaScriptSourceFrame, () =>
        SourcesTestRunner.toggleBreakpoint(javaScriptSourceFrame, 28)
      );
      next();
    }
  ]);
})();
