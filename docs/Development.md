# Development

## Contributing

Contributions, bug reports, serialization findings, and test assets are welcome.

Because package serialization can vary by Unreal Engine version, UObject type, cooking state, custom versions, editor or runtime context and native serialization implementations, please include the Unreal Engine version and asset type when reporting parsing issues.

## Automated tests

The plugin's automation tests run with `Scripts\RunAutomationTests.ps1`, which builds a host project that has the plugin, runs every test with `UnrealEditor-Cmd`, then runs the widget tests with the full editor (Slate is only initialized there; under `-Cmd` the widget tests skip themselves), and fails on a failed test, a run that did not finish its queue (a crash or a timeout) and a run that found no tests:

```
.\Scripts\RunAutomationTests.ps1 -EngineRoot D:\dev\UnrealEngine -HostProject D:\dev\ASIHost\ASIHost.uproject -LinkPlugin
```

`-LinkPlugin` links this repository into the host project's `Plugins` folder for the run (a junction, removed afterwards); without it the plugin must already be there. Each editor run goes through its tests twice in one editor session (`-Loops`, default 2): a test that leaves an object behind in memory, such as a Blueprint created in a package of a fixed name, passes the first time and is an assertion the second, which is how a second run of the tests in an open editor crashes it. The logs are copied to `Saved\AutomationLogs`. Close the editor first: an open editor locks the plugin's DLL.

There is no CI for the tests: they need an Unreal Engine built from source, which GitHub's hosted runners cannot provide, and a self-hosted runner on a public repository would run the code of any fork's pull request on its machine. Run the script before opening a pull request.

## Conventions

- Code is formatted with the `.clang-format` of the plugin, and `python Scripts/CheckIncludeOrder.py` checks the include blocks.
- Behavior changes update the page of [docs](.) that describes them ([Features](Features.md) or [Native data](NativeData.md)) and tick the [Roadmap](Roadmap.md) in the same pull request.
- The README stays short: details go to the pages above.
